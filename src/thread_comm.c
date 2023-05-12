/*
MIT License

Copyright (c) 2018 Danis Ozdemir

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <thread_comm.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

#ifdef RUNNING_UNIT_TESTS
#include <assert.h>
#endif

const uint32_t max_allowed_cq_size = INT32_MAX;

typedef struct message {
  void* data;
  uint32_t size;
} message;

void add_duration_to_timespec(struct timespec* target,
                              struct timespec* duration) {
  static const long int max_nsecs = 1000000000;

  target->tv_sec += duration->tv_sec;

  long int gap = max_nsecs - target->tv_nsec;

  if (gap > duration->tv_nsec) {
    target->tv_nsec += duration->tv_nsec;
  } else {
    ++target->tv_sec;
    target->tv_nsec = duration->tv_nsec - gap;
  }
}

struct circular_queue {
  pthread_mutex_t mutex;
  pthread_cond_t read_cond;
  pthread_cond_t write_cond;

  uint32_t read_index;
  uint32_t write_index;
  uint32_t max_size;
  uint32_t msg_count;

  message* msg_array;
  bool writing_disabled;
};

circular_queue* circular_queue_create(uint32_t max_size) {
  if (max_size == 0) {
    fprintf(stderr, "max_size should be positive\n");
    return NULL;
  }

  if (max_size > max_allowed_cq_size) {
    fprintf(stderr, "max_size: %u - max_allowed_cq_size: %u\n", max_size,
            max_allowed_cq_size);
    return NULL;
  }

  circular_queue* cq = (circular_queue*)malloc(sizeof(circular_queue));
  if (!cq) {
    fprintf(stderr, "Failed to allocate memory for circular_queue\n");
    return NULL;
  }

  cq->msg_array = (message*)malloc(max_size * sizeof(message));
  if (!cq->msg_array) {
    free(cq);
    fprintf(stderr, "Failed to allocate memory for cq msg_array\n");
    return NULL;
  }

  pthread_mutex_init(&cq->mutex, NULL);
  pthread_cond_init(&cq->read_cond, NULL);
  pthread_cond_init(&cq->write_cond, NULL);
  cq->read_index = 0;
  cq->write_index = 0;
  cq->max_size = max_size;
  cq->msg_count = 0;
  cq->writing_disabled = false;

  return cq;
}

void __circular_queue_destroy(circular_queue* cq) {
  if (cq) {
    if (cq->msg_array) {
      free(cq->msg_array);
      cq->msg_array = NULL;
    }

    pthread_mutex_destroy(&cq->mutex);
    pthread_cond_destroy(&cq->read_cond);
    pthread_cond_destroy(&cq->write_cond);

    free(cq);
  }
}

// This function should always be called while holding the mutex.
// Please notice that it's not exposed to the caller via the header file.
int _sendto_cq(circular_queue* cq, void** msg, uint32_t msg_size) {
  cq->msg_array[cq->write_index].data = *msg;
  if (*msg == NULL) {
    msg_size = 0;
  }
  cq->msg_array[cq->write_index++].size = msg_size;
  *msg = NULL;  // The sender loses the ownership of the msg pointer.
  if (cq->write_index == cq->max_size) {
    cq->write_index = 0;
  }
  ++cq->msg_count;

  pthread_cond_signal(&cq->read_cond);

  return msg_size;
}

int verify_circq_send_zc_params(circular_queue* cq, void** msg,
                                uint32_t msg_size) {
  if (!cq) {
    fprintf(stderr, "verify_circq_send_zc_params - cq is NULL\n");
    return -1;
  }

  if (!msg) {
    fprintf(stderr, "verify_circq_send_zc_params - msg is NULL\n");
    return -1;
  }

  if (msg_size == 0 && *msg != NULL) {
    fprintf(
        stderr,
        "verify_circq_send_zc_params - msg_size is zero, msg is not NULL\n");
    return -1;
  }

  return 0;
}

int circq_send_zc(circular_queue* cq, void** msg, uint32_t msg_size) {
  if (verify_circq_send_zc_params(cq, msg, msg_size) != 0) {
    return -1;
  }

  pthread_mutex_lock(&cq->mutex);

  if (cq->writing_disabled) {
    pthread_mutex_unlock(&cq->mutex);
    return -1;
  }

  while (cq->msg_count == cq->max_size) {
    pthread_cond_wait(&cq->write_cond, &cq->mutex);
  }

  msg_size = _sendto_cq(cq, msg, msg_size);

  pthread_mutex_unlock(&cq->mutex);

  return msg_size;
}

int circq_try_send_zc(circular_queue* cq, void** msg, uint32_t msg_size) {
  if (verify_circq_send_zc_params(cq, msg, msg_size) != 0) {
    return -1;
  }

  // Assuming we won't have space for the new message.
  int result = -1;

  pthread_mutex_lock(&cq->mutex);

  if (cq->writing_disabled) {
    pthread_mutex_unlock(&cq->mutex);
    return -1;
  }

  if (cq->msg_count < cq->max_size) {
    // We have space for the new message, proceed.
    result = _sendto_cq(cq, msg, msg_size);
  }

  pthread_mutex_unlock(&cq->mutex);

  return result;
}

int circq_timed_send_zc(circular_queue* cq, void** msg, uint32_t msg_size,
                        struct timespec* timeout_duration) {
  if (verify_circq_send_zc_params(cq, msg, msg_size) != 0) {
    return -1;
  }

  pthread_mutex_lock(&cq->mutex);

  if (cq->writing_disabled) {
    pthread_mutex_unlock(&cq->mutex);
    return -1;
  }

  if (cq->msg_count == cq->max_size) {
    int retval;
    struct timespec abs_time;
    clock_gettime(CLOCK_REALTIME, &abs_time);
    add_duration_to_timespec(&abs_time, timeout_duration);

    while (cq->msg_count == cq->max_size) {
      if ((retval = pthread_cond_timedwait(&cq->write_cond, &cq->mutex,
                                           &abs_time)) != 0) {
        if (retval != ETIMEDOUT) {
          fprintf(stderr, "timed_sendto_cq failed, retval: %d\n", retval);
        }
        pthread_mutex_unlock(&cq->mutex);
        return -1;
      }
    }
  }

  msg_size = _sendto_cq(cq, msg, msg_size);

  pthread_mutex_unlock(&cq->mutex);

  return msg_size;
}

// This function should always be called while holding the mutex.
// Please notice that it's not exposed to the caller via the header file.
int _recvfrom_cq(circular_queue* cq, void** target_buf) {
  int msg_size = cq->msg_array[cq->read_index].size;
  *target_buf = cq->msg_array[cq->read_index++].data;
  if (cq->read_index == cq->max_size) {
    cq->read_index = 0;
  }

  --cq->msg_count;

  pthread_cond_signal(&cq->write_cond);

  return msg_size;
}

int verify_recvfrom_cq_zc_params(circular_queue* cq, void** target_buf) {
  if (!cq) {
    fprintf(stderr, "verify_recvfrom_cq_zc_params - cq is NULL\n");
    return -1;
  }

  if (!target_buf) {
    fprintf(stderr, "verify_recvfrom_cq_zc_params - target_buf is NULL\n");
    return -1;
  }

  return 0;
}

int circq_recv_zc(circular_queue* cq, void** target_buf) {
  if (verify_recvfrom_cq_zc_params(cq, target_buf) != 0) {
    return -1;
  }

  pthread_mutex_lock(&cq->mutex);

  while (cq->msg_count == 0) {
    pthread_cond_wait(&cq->read_cond, &cq->mutex);
  }

  int msg_size = _recvfrom_cq(cq, target_buf);

  pthread_mutex_unlock(&cq->mutex);

  return msg_size;
}

int circq_try_recv_zc(circular_queue* cq, void** target_buf) {
  if (verify_recvfrom_cq_zc_params(cq, target_buf) != 0) {
    return -1;
  }

  int result = -1;

  pthread_mutex_lock(&cq->mutex);

  if (cq->msg_count > 0) {
    result = _recvfrom_cq(cq, target_buf);
  }

  pthread_mutex_unlock(&cq->mutex);

  return result;
}

int circq_timed_recv_zc(circular_queue* cq, void** target_buf,
                        struct timespec* timeout) {
  if (verify_recvfrom_cq_zc_params(cq, target_buf) != 0) {
    return -1;
  }

  pthread_mutex_lock(&cq->mutex);

  if (cq->msg_count == 0) {
    int retval;
    struct timespec abs_time;
    clock_gettime(CLOCK_REALTIME, &abs_time);
    add_duration_to_timespec(&abs_time, timeout);

    while (cq->msg_count == 0) {
      if ((retval = pthread_cond_timedwait(&cq->read_cond, &cq->mutex,
                                           &abs_time)) != 0) {
        if (retval != ETIMEDOUT) {
          fprintf(stderr, "timed_recvfrom_cq failed, retval: %d\n", retval);
        }
        pthread_mutex_unlock(&cq->mutex);
        return -1;
      }
    }
  }

  int msg_size = _recvfrom_cq(cq, target_buf);

  pthread_mutex_unlock(&cq->mutex);

  return msg_size;
}

void circq_disable_sending(circular_queue* cq) {
  if (cq) {
    pthread_mutex_lock(&cq->mutex);
    cq->writing_disabled = true;
    pthread_mutex_unlock(&cq->mutex);
  } else {
    fprintf(stderr, "circq_disable_sending - cq is NULL\n");
  }
}

void circq_enable_sending(circular_queue* cq) {
  if (cq) {
    pthread_mutex_lock(&cq->mutex);
    cq->writing_disabled = false;
    pthread_mutex_unlock(&cq->mutex);
  } else {
    fprintf(stderr, "circq_disable_sending - cq is NULL\n");
  }
}

int circq_msg_count(circular_queue* cq) {
  int result = -1;

  if (cq) {
    pthread_mutex_lock(&cq->mutex);
    result = cq->msg_count;
    pthread_mutex_unlock(&cq->mutex);
  }

  return result;
}

// Dynamic queue related section starts here.
typedef struct dllist_node {
  struct dllist_node* prev;
  message msg;
  struct dllist_node* next;
} dllist_node;

struct dynamic_queue {
  pthread_mutex_t mutex;
  pthread_cond_t read_cond;

  uint32_t msg_count;

  dllist_node* head;
  dllist_node* tail;

  bool writing_disabled;
};

int append_msg_to_dq_tail(dynamic_queue* dq, void** data, uint32_t msg_size) {
  dllist_node* new_elem = (dllist_node*)malloc(sizeof(dllist_node));
  if (!new_elem) {
    fprintf(stderr, "Failed to allocate memory for new element\n");
    return -1;
  }

  if (*data == NULL) {
    msg_size = 0;
  }

  new_elem->msg.data = *data;
  *data = NULL;
  new_elem->msg.size = msg_size;
  new_elem->next = NULL;

  if (!dq->head) {
#ifdef RUNNING_UNIT_TESTS
    assert(!dq->tail);
#endif
    new_elem->prev = NULL;
    dq->head = new_elem;
    dq->tail = new_elem;
  } else {
#ifdef RUNNING_UNIT_TESTS
    assert(dq->tail && !dq->tail->next);
#endif
    new_elem->prev = dq->tail;
    dq->tail->next = new_elem;
    dq->tail = new_elem;
  }

  return msg_size;
}

int remove_msg_from_dq_head(dynamic_queue* dq, void** data_buf_ptr) {
  if (!dq->head) {
#ifdef RUNNING_UNIT_TESTS
    assert(!dq->tail);
#endif
    return -1;
  }

#ifdef RUNNING_UNIT_TESTS
  assert(!dq->head->prev);
  assert(dq->tail && !dq->tail->next);
#endif

  dllist_node* node_to_be_freed = dq->head;

  *data_buf_ptr = dq->head->msg.data;
  int msg_size = dq->head->msg.size;

  dq->head = dq->head->next;
  if (dq->head) {
    dq->head->prev = NULL;
  } else {
    // The head just became NULL, let's not forget about the tail
    dq->tail = NULL;
  }

  free(node_to_be_freed);

  return msg_size;
}

void destroy_dq_dllist(dynamic_queue* dq) {
  dllist_node* node_to_be_freed = NULL;
  while (dq->head) {
    node_to_be_freed = dq->head;
    dq->head = dq->head->next;
    free(node_to_be_freed);
  }
  dq->tail = NULL;
}

dynamic_queue* dynamic_queue_create() {
  dynamic_queue* dq = (dynamic_queue*)malloc(sizeof(dynamic_queue));
  if (!dq) {
    fprintf(stderr, "Failed to allocate memory for dynamic queue\n");
    return NULL;
  }

  pthread_mutex_init(&dq->mutex, NULL);
  pthread_cond_init(&dq->read_cond, NULL);
  dq->msg_count = 0;
  dq->head = NULL;
  dq->tail = NULL;
  dq->writing_disabled = false;

  return dq;
}

void __dynamic_queue_destroy(dynamic_queue* dq) {
  if (dq) {
    pthread_mutex_destroy(&dq->mutex);
    pthread_cond_destroy(&dq->read_cond);
    destroy_dq_dllist(dq);
    free(dq);
  }
}

int _sendto_dq(dynamic_queue* dq, void** msg, uint32_t msg_size) {
  int retval = append_msg_to_dq_tail(dq, msg, msg_size);

  if (retval != -1) {
    ++dq->msg_count;
    pthread_cond_signal(&dq->read_cond);
  }

  return retval;
}

int verify_dynmq_send_zc_params(dynamic_queue* dq, void** msg,
                                uint32_t msg_size) {
  if (!dq) {
    fprintf(stderr, "verify_circq_send_zc_params - cq is NULL\n");
    return -1;
  }

  if (!msg) {
    fprintf(stderr, "verify_circq_send_zc_params - msg is NULL\n");
    return -1;
  }

  if (msg_size == 0 && *msg != NULL) {
    fprintf(
        stderr,
        "verify_circq_send_zc_params - msg_size is zero, msg is not NULL\n");
    return -1;
  }

  return 0;
}

int dynmq_send_zc(dynamic_queue* dq, void** msg, uint32_t msg_size) {
  if (verify_dynmq_send_zc_params(dq, msg, msg_size) != 0) {
    return -1;
  }

  pthread_mutex_lock(&dq->mutex);

  if (dq->writing_disabled) {
    pthread_mutex_unlock(&dq->mutex);
    return -1;
  }

  msg_size = _sendto_dq(dq, msg, msg_size);

  pthread_mutex_unlock(&dq->mutex);

  return msg_size;
}

int _recvfrom_dq(dynamic_queue* dq, void** target_buf) {
  int retval = remove_msg_from_dq_head(dq, target_buf);

  if (retval != -1) {
    --dq->msg_count;
  }

  return retval;
}

int verify_recvfrom_dq_zc_params(dynamic_queue* dq, void** target_buf) {
  if (!dq) {
    fprintf(stderr, "verify_recvfrom_dq_zc_params - dq is NULL\n");
    return -1;
  }

  if (!target_buf) {
    fprintf(stderr, "verify_recvfrom_dq_zc_params - target_buf is NULL\n");
    return -1;
  }

  return 0;
}

int dynmq_recv_zc(dynamic_queue* dq, void** target_buf) {
  if (verify_recvfrom_dq_zc_params(dq, target_buf) != 0) {
    return -1;
  }

  pthread_mutex_lock(&dq->mutex);

  while (dq->msg_count == 0) {
    pthread_cond_wait(&dq->read_cond, &dq->mutex);
  }

  int msg_size = _recvfrom_dq(dq, target_buf);

  pthread_mutex_unlock(&dq->mutex);

  return msg_size;
}

int dynmq_try_recv_zc(dynamic_queue* dq, void** target_buf) {
  if (verify_recvfrom_dq_zc_params(dq, target_buf) != 0) {
    return -1;
  }

  int result = -1;

  pthread_mutex_lock(&dq->mutex);

  if (dq->msg_count > 0) {
    result = _recvfrom_dq(dq, target_buf);
  }

  pthread_mutex_unlock(&dq->mutex);

  return result;
}

int dynmq_timed_recv_zc(dynamic_queue* dq, void** target_buf,
                        struct timespec* timeout) {
  if (verify_recvfrom_dq_zc_params(dq, target_buf) != 0) {
    return -1;
  }

  pthread_mutex_lock(&dq->mutex);

  if (dq->msg_count == 0) {
    int retval;
    struct timespec abs_time;
    clock_gettime(CLOCK_REALTIME, &abs_time);
    add_duration_to_timespec(&abs_time, timeout);

    while (dq->msg_count == 0) {
      if ((retval = pthread_cond_timedwait(&dq->read_cond, &dq->mutex,
                                           &abs_time)) != 0) {
        if (retval != ETIMEDOUT) {
          fprintf(stderr, "timed_recvfrom_dq failed, retval: %d\n", retval);
        }
        pthread_mutex_unlock(&dq->mutex);
        return -1;
      }
    }
  }

  int msg_size = _recvfrom_dq(dq, target_buf);

  pthread_mutex_unlock(&dq->mutex);

  return msg_size;
}

void dynmq_disable_sending(dynamic_queue* dq) {
  if (dq) {
    pthread_mutex_lock(&dq->mutex);
    dq->writing_disabled = true;
    pthread_mutex_unlock(&dq->mutex);
  } else {
    fprintf(stderr, "dynmq_disable_sending - dq is NULL\n");
  }
}

void dynmq_enable_sending(dynamic_queue* dq) {
  if (dq) {
    pthread_mutex_lock(&dq->mutex);
    dq->writing_disabled = false;
    pthread_mutex_unlock(&dq->mutex);
  } else {
    fprintf(stderr, "dynmq_disable_sending - dq is NULL\n");
  }
}

int dynmq_msg_count(dynamic_queue* dq) {
  int result = -1;

  if (dq) {
    pthread_mutex_lock(&dq->mutex);
    result = dq->msg_count;
    pthread_mutex_unlock(&dq->mutex);
  }

  return result;
}

// Channel related section starts here.
struct channel {
  pthread_t owner_tid;
  circular_queue* owner_to_workers_cq;
  circular_queue* workers_to_owner_cq;
};

channel* channel_create(uint32_t max_size) {
  channel* ch = (channel*)malloc(sizeof(channel));
  if (!ch) {
    fprintf(stderr, "Failed to allocate memory for channel\n");
    return NULL;
  }

  ch->owner_to_workers_cq = circular_queue_create(max_size);
  if (!ch->owner_to_workers_cq) {
    fprintf(stderr, "Failed to create owner_to_workers_cq\n");
    free(ch);
    return NULL;
  }

  ch->workers_to_owner_cq = circular_queue_create(max_size);
  if (!ch->workers_to_owner_cq) {
    fprintf(stderr, "Failed to create workers_to_owner_cq\n");
    circular_queue_destroy(ch->owner_to_workers_cq);
    free(ch);
    return NULL;
  }

  ch->owner_tid = pthread_self();

  return ch;
}

void __channel_destroy(channel* ch) {
  if (ch) {
    circular_queue_destroy(ch->owner_to_workers_cq);
    circular_queue_destroy(ch->workers_to_owner_cq);
    free(ch);
  }
}

int chan_send_zc(channel* ch, void** msg, uint32_t msg_size) {
  if (!ch) {
    fprintf(stderr, "sendto_ch was called on a NULL channel\n");
    return -1;
  }

  if (pthread_self() == ch->owner_tid) {
    return circq_send_zc(ch->owner_to_workers_cq, msg, msg_size);
  }

  return circq_send_zc(ch->workers_to_owner_cq, msg, msg_size);
}

int chan_try_send_zc(channel* ch, void** msg, uint32_t msg_size) {
  if (!ch) {
    fprintf(stderr, "try_sendto_ch was called on a NULL channel\n");
    return -1;
  }

  if (pthread_self() == ch->owner_tid) {
    return circq_try_send_zc(ch->owner_to_workers_cq, msg, msg_size);
  }

  return circq_try_send_zc(ch->workers_to_owner_cq, msg, msg_size);
}

int chan_timed_send_zc(channel* ch, void** msg, uint32_t msg_size,
                       struct timespec* timeout) {
  if (!ch) {
    fprintf(stderr, "timed_sendto_ch was called on a NULL channel\n");
    return -1;
  }

  if (pthread_self() == ch->owner_tid) {
    return circq_timed_send_zc(ch->owner_to_workers_cq, msg, msg_size, timeout);
  }

  return circq_timed_send_zc(ch->workers_to_owner_cq, msg, msg_size, timeout);
}

int chan_recv_zc(channel* ch, void** target_buf) {
  if (!ch) {
    fprintf(stderr, "recvfrom_ch was called on a NULL channel\n");
    return -1;
  }

  if (pthread_self() == ch->owner_tid) {
    return circq_recv_zc(ch->workers_to_owner_cq, target_buf);
  }

  return circq_recv_zc(ch->owner_to_workers_cq, target_buf);
}

int chan_try_recv_zc(channel* ch, void** target_buf) {
  if (!ch) {
    fprintf(stderr, "try_recvfrom_ch was called on a NULL channel\n");
    return -1;
  }

  if (pthread_self() == ch->owner_tid) {
    return circq_try_recv_zc(ch->workers_to_owner_cq, target_buf);
  }

  return circq_try_recv_zc(ch->owner_to_workers_cq, target_buf);
}

int chan_timed_recv_zc(channel* ch, void** target_buf,
                       struct timespec* timeout) {
  if (!ch) {
    fprintf(stderr, "timed_recvfrom_ch was called on a NULL channel\n");
    return -1;
  }

  if (pthread_self() == ch->owner_tid) {
    return circq_timed_recv_zc(ch->workers_to_owner_cq, target_buf, timeout);
  }

  return circq_timed_recv_zc(ch->owner_to_workers_cq, target_buf, timeout);
}

int chan_disable_sending(channel* ch, channel_direction d) {
  if (!ch) {
    fprintf(stderr, "chan_disable_sending - ch is NULL\n");
    return -1;
  }

  if (d == owner_to_workers) {
    circq_disable_sending(ch->owner_to_workers_cq);
  } else if (d == workers_to_owner) {
    circq_disable_sending(ch->workers_to_owner_cq);
  } else {
    fprintf(stderr, "chan_disable_sending - unknown direction: %d\n", d);
    return -1;
  }

  return 0;
}

int chan_enable_sending(channel* ch, channel_direction d) {
  if (!ch) {
    fprintf(stderr, "chan_disable_sending - ch is NULL\n");
    return -1;
  }

  if (d == owner_to_workers) {
    circq_enable_sending(ch->owner_to_workers_cq);
  } else if (d == workers_to_owner) {
    circq_enable_sending(ch->workers_to_owner_cq);
  } else {
    fprintf(stderr, "chan_enable_sending - unknown direction: %d\n", d);
    return -1;
  }

  return 0;
}

int chan_msg_count(channel* ch, channel_direction d) {
  if (!ch) {
    fprintf(stderr, "chan_disable_sending - ch is NULL\n");
    return -1;
  }

  if (d == owner_to_workers) {
    return circq_msg_count(ch->owner_to_workers_cq);
  } else if (d == workers_to_owner) {
    return circq_msg_count(ch->workers_to_owner_cq);
  }

  fprintf(stderr, "chan_msg_count - unknown direction: %d\n", d);
  return -1;
}
