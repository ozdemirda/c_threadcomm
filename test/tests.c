#include <thread_comm.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>

#include <tau/tau.h>
TAU_MAIN()  // sets up Tau (+ main function)

// CIRCULAR_QUEUE TESTS

TEST(circular_queues, create_fails) {
  circular_queue* cq = circular_queue_create(0);
  REQUIRE_EQ((void*)cq, NULL);

  cq = circular_queue_create(-1);
  REQUIRE_EQ((void*)cq, NULL);

  cq = circular_queue_create((uint32_t)INT32_MAX + 1);
  REQUIRE_EQ((void*)cq, NULL);
}

TEST(circular_queues, create_and_destroy) {
  circular_queue* cq = circular_queue_create(1);
  REQUIRE_NE((void*)cq, NULL);

  circular_queue_destroy(cq);
  REQUIRE_EQ((void*)cq, NULL);
}

TEST(circular_queues, basic_send_and_receive) {
  circular_queue* cq = circular_queue_create(1);

  char* m1 = (char*)malloc(16 * sizeof(char));
  m1[0] = 'A';
  m1[1] = '\0';

  REQUIRE_EQ(circq_send_zc(cq, (void**)&m1, 16), 16);
  REQUIRE_EQ(m1, NULL);  // The ownership of the message is lost.

  char* m2 = NULL;
  REQUIRE_EQ(circq_recv_zc(cq, (void**)&m2), 16);

  REQUIRE_NE(m2, NULL);
  REQUIRE_EQ(m2[0], 'A');
  REQUIRE_EQ(m2[1], '\0');

  free(m2);
  circular_queue_destroy(cq);
}

TEST(circular_queues, msg_count) {
  circular_queue* cq = circular_queue_create(3);

  char* m1 = NULL;

  for (int i = 0; i < 3; ++i) {
    REQUIRE_EQ(circq_msg_count(cq), i);
    circq_send_zc(cq, (void**)&m1, 0);
    REQUIRE_EQ(circq_msg_count(cq), i + 1);
  }

  for (int i = 3; i > 0; --i) {
    REQUIRE_EQ(circq_msg_count(cq), i);
    circq_recv_zc(cq, (void**)&m1);
    REQUIRE_EQ(circq_msg_count(cq), i - 1);
  }

  circular_queue_destroy(cq);
}

TEST(circular_queues, basic_send_and_receive_NULL_msg) {
  circular_queue* cq = circular_queue_create(3);

  char* m1 = NULL;
  REQUIRE_EQ(circq_send_zc(cq, (void**)&m1, 0), 0);
  REQUIRE_EQ(m1, NULL);

  REQUIRE_EQ(circq_send_zc(cq, (void**)&m1, 16), 0);
  REQUIRE_EQ(m1, NULL);

  m1 = (char*)malloc(sizeof(char));
  REQUIRE_EQ(circq_send_zc(cq, (void**)&m1, 0), -1);
  REQUIRE_NE(m1, NULL);
  free(m1);
  m1 = NULL;

  char* m2 = NULL;
  REQUIRE_EQ(circq_recv_zc(cq, (void**)&m2), 0);
  REQUIRE_EQ(m2, NULL);

  REQUIRE_EQ(circq_recv_zc(cq, (void**)&m2), 0);
  REQUIRE_EQ(m2, NULL);

  circular_queue_destroy(cq);
}

TEST(circular_queues, try_send_and_try_receive) {
  circular_queue* cq = circular_queue_create(1);

  char* m1 = (char*)malloc(16 * sizeof(char));
  m1[0] = 'A';
  m1[1] = '\0';

  REQUIRE_EQ(circq_try_send_zc(cq, (void**)&m1, 16), 16);
  REQUIRE_EQ(m1, NULL);

  m1 = (char*)malloc(sizeof(char));
  REQUIRE_EQ(circq_try_send_zc(cq, (void**)&m1, 1), -1);
  REQUIRE_NE(m1, NULL);
  free(m1);
  m1 = NULL;

  char* m2 = NULL;
  REQUIRE_EQ(circq_try_recv_zc(cq, (void**)&m2), 16);
  REQUIRE_NE(m2, NULL);
  REQUIRE_EQ(m2[0], 'A');
  REQUIRE_EQ(m2[1], '\0');

  REQUIRE_EQ(circq_try_recv_zc(cq, (void**)&m1), -1);
  REQUIRE_EQ(m1, NULL);

  free(m2);
  circular_queue_destroy(cq);
}

#define getWallTime(A) clock_gettime(CLOCK_REALTIME, &A);
#define diffTimeUSec(A, B) \
  (B.tv_sec - A.tv_sec) * 1000000 + (B.tv_nsec - A.tv_nsec) / 1000

TEST(circular_queues, timed_send_and_timed_receive) {
  circular_queue* cq = circular_queue_create(1);

  char* m1 = (char*)malloc(16 * sizeof(char));
  m1[0] = 'A';
  m1[1] = '\0';

  struct timespec timeout;
  timeout.tv_sec = 0;           // 0  secs
  timeout.tv_nsec = 100000000;  // 100 msecs

  struct timespec before;
  struct timespec after;

  getWallTime(before);
  REQUIRE_EQ(circq_timed_send_zc(cq, (void**)&m1, 16, &timeout), 16);
  getWallTime(after);
  REQUIRE_LT(diffTimeUSec(before, after), 10000);
  REQUIRE_EQ(m1, NULL);

  m1 = (char*)malloc(sizeof(char));
  getWallTime(before);
  REQUIRE_EQ(circq_timed_send_zc(cq, (void**)&m1, 16, &timeout), -1);
  getWallTime(after);
  REQUIRE_GE(diffTimeUSec(before, after), 100000);
  REQUIRE_NE(m1, NULL);
  free(m1);
  m1 = NULL;

  char* m2 = NULL;

  getWallTime(before);
  REQUIRE_EQ(circq_timed_recv_zc(cq, (void**)&m2, &timeout), 16);
  getWallTime(after);
  REQUIRE_LT(diffTimeUSec(before, after), 10000);
  REQUIRE_NE(m2, NULL);
  REQUIRE_EQ(m2[0], 'A');
  REQUIRE_EQ(m2[1], '\0');

  getWallTime(before);
  REQUIRE_EQ(circq_timed_recv_zc(cq, (void**)&m1, &timeout), -1);
  getWallTime(after);
  REQUIRE_GE(diffTimeUSec(before, after), 100000);
  REQUIRE_EQ(m1, NULL);

  free(m2);
  circular_queue_destroy(cq);
}

TEST(circular_queues, enable_disable_sending) {
  circular_queue* cq = circular_queue_create(1);

  char* m1 = (char*)malloc(16 * sizeof(char));
  m1[0] = 'A';
  m1[1] = '\0';

  circq_disable_sending(cq);

  REQUIRE_EQ(circq_send_zc(cq, (void**)&m1, 16), -1);
  REQUIRE_NE(m1, NULL);

  REQUIRE_EQ(circq_try_send_zc(cq, (void**)&m1, 16), -1);
  REQUIRE_NE(m1, NULL);

  REQUIRE_EQ(circq_timed_send_zc(cq, (void**)&m1, 16,
                                 &(struct timespec){.tv_sec = 1, .tv_nsec = 0}),
             -1);
  REQUIRE_NE(m1, NULL);

  circq_enable_sending(cq);

  REQUIRE_EQ(circq_send_zc(cq, (void**)&m1, 16), 16);
  REQUIRE_EQ(m1, NULL);

  char* m2 = NULL;

  REQUIRE_EQ(circq_recv_zc(cq, (void**)&m2), 16);
  REQUIRE_NE(m2, NULL);
  REQUIRE_EQ(m2[0], 'A');
  REQUIRE_EQ(m2[1], '\0');

  free(m2);
  circular_queue_destroy(cq);
}

// DYNAMIC_QUEUE TESTS

TEST(dynamic_queues, create_and_destroy) {
  dynamic_queue* dq = dynamic_queue_create();
  REQUIRE_NE((void*)dq, NULL);

  dynamic_queue_destroy(dq);
  REQUIRE_EQ((void*)dq, NULL);
}

TEST(dynamic_queues, basic_send_and_receive) {
  dynamic_queue* dq = dynamic_queue_create();

  char* m1 = (char*)malloc(16 * sizeof(char));
  m1[0] = 'A';
  m1[1] = '\0';

  REQUIRE_EQ(dynmq_send_zc(dq, (void**)&m1, 16), 16);
  REQUIRE_EQ(m1, NULL);  // The ownership of the message is lost.

  char* m2 = NULL;
  REQUIRE_EQ(dynmq_recv_zc(dq, (void**)&m2), 16);

  REQUIRE_NE(m2, NULL);
  REQUIRE_EQ(m2[0], 'A');
  REQUIRE_EQ(m2[1], '\0');

  free(m2);
  dynamic_queue_destroy(dq);
}

TEST(dynamic_queues, msg_count) {
  dynamic_queue* dq = dynamic_queue_create();

  char* m1 = NULL;

  for (int i = 0; i < 3; ++i) {
    REQUIRE_EQ(dynmq_msg_count(dq), i);
    dynmq_send_zc(dq, (void**)&m1, 0);
    REQUIRE_EQ(dynmq_msg_count(dq), i + 1);
  }

  for (int i = 3; i > 0; --i) {
    REQUIRE_EQ(dynmq_msg_count(dq), i);
    dynmq_recv_zc(dq, (void**)&m1);
    REQUIRE_EQ(dynmq_msg_count(dq), i - 1);
  }

  dynamic_queue_destroy(dq);
}

TEST(dynamic_queues, destroy_queue_with_items_in_it) {
  dynamic_queue* dq = dynamic_queue_create();

  char* m1 = NULL;

  for (int i = 0; i < 3; ++i) {
    REQUIRE_EQ(dynmq_msg_count(dq), i);
    dynmq_send_zc(dq, (void**)&m1, 0);
    REQUIRE_EQ(dynmq_msg_count(dq), i + 1);
  }

  dynamic_queue_destroy(dq);
  REQUIRE_EQ((void*)dq, NULL);
}

TEST(dynamic_queues, basic_send_and_receive_NULL_msg) {
  dynamic_queue* dq = dynamic_queue_create();

  char* m1 = NULL;
  REQUIRE_EQ(dynmq_send_zc(dq, (void**)&m1, 0), 0);
  REQUIRE_EQ(m1, NULL);

  REQUIRE_EQ(dynmq_send_zc(dq, (void**)&m1, 16), 0);
  REQUIRE_EQ(m1, NULL);

  m1 = (char*)malloc(sizeof(char));
  REQUIRE_EQ(dynmq_send_zc(dq, (void**)&m1, 0), -1);
  REQUIRE_NE(m1, NULL);
  free(m1);
  m1 = NULL;

  char* m2 = NULL;
  REQUIRE_EQ(dynmq_recv_zc(dq, (void**)&m2), 0);
  REQUIRE_EQ(m2, NULL);

  REQUIRE_EQ(dynmq_recv_zc(dq, (void**)&m2), 0);
  REQUIRE_EQ(m2, NULL);

  REQUIRE_EQ(dynmq_send_zc(dq, (void**)&m1, 16), 0);
  REQUIRE_EQ(m1, NULL);

  REQUIRE_EQ(dynmq_recv_zc(dq, (void**)&m2), 0);
  REQUIRE_EQ(m2, NULL);

  dynamic_queue_destroy(dq);
}

TEST(dynamic_queues, send_and_try_receive) {
  dynamic_queue* dq = dynamic_queue_create();

  char* m1 = (char*)malloc(16 * sizeof(char));
  m1[0] = 'A';
  m1[1] = '\0';

  REQUIRE_EQ(dynmq_send_zc(dq, (void**)&m1, 16), 16);
  REQUIRE_EQ(m1, NULL);

  char* m2 = NULL;
  REQUIRE_EQ(dynmq_try_recv_zc(dq, (void**)&m2), 16);
  REQUIRE_NE(m2, NULL);
  REQUIRE_EQ(m2[0], 'A');
  REQUIRE_EQ(m2[1], '\0');

  REQUIRE_EQ(dynmq_try_recv_zc(dq, (void**)&m1), -1);
  REQUIRE_EQ(m1, NULL);

  free(m2);
  dynamic_queue_destroy(dq);
}

TEST(dynamic_queues, send_and_timed_receive) {
  dynamic_queue* dq = dynamic_queue_create();

  char* m1 = (char*)malloc(16 * sizeof(char));
  m1[0] = 'A';
  m1[1] = '\0';

  struct timespec timeout;
  timeout.tv_sec = 0;           // 0  secs
  timeout.tv_nsec = 100000000;  // 100 msecs

  struct timespec before;
  struct timespec after;

  REQUIRE_EQ(dynmq_send_zc(dq, (void**)&m1, 16), 16);

  char* m2 = NULL;

  getWallTime(before);
  REQUIRE_EQ(dynmq_timed_recv_zc(dq, (void**)&m2, &timeout), 16);
  getWallTime(after);
  REQUIRE_LT(diffTimeUSec(before, after), 10000);
  REQUIRE_NE(m2, NULL);
  REQUIRE_EQ(m2[0], 'A');
  REQUIRE_EQ(m2[1], '\0');

  getWallTime(before);
  REQUIRE_EQ(dynmq_timed_recv_zc(dq, (void**)&m1, &timeout), -1);
  getWallTime(after);
  REQUIRE_GE(diffTimeUSec(before, after), 100000);
  REQUIRE_EQ(m1, NULL);

  free(m2);
  dynamic_queue_destroy(dq);
}

TEST(dynamic_queues, enable_disable_sending) {
  dynamic_queue* dq = dynamic_queue_create();

  char* m1 = (char*)malloc(16 * sizeof(char));
  m1[0] = 'A';
  m1[1] = '\0';

  dynmq_disable_sending(dq);

  REQUIRE_EQ(dynmq_send_zc(dq, (void**)&m1, 16), -1);
  REQUIRE_NE(m1, NULL);

  dynmq_enable_sending(dq);

  REQUIRE_EQ(dynmq_send_zc(dq, (void**)&m1, 16), 16);
  REQUIRE_EQ(m1, NULL);

  char* m2 = NULL;

  REQUIRE_EQ(dynmq_recv_zc(dq, (void**)&m2), 16);
  REQUIRE_NE(m2, NULL);
  REQUIRE_EQ(m2[0], 'A');
  REQUIRE_EQ(m2[1], '\0');

  free(m2);
  dynamic_queue_destroy(dq);
}

// CHANNEL TESTS

TEST(channels, create_fails) {
  channel* ch = channel_create(0);
  REQUIRE_EQ((void*)ch, NULL);

  ch = channel_create(-1);
  REQUIRE_EQ((void*)ch, NULL);

  ch = channel_create((uint32_t)INT32_MAX + 1);
  REQUIRE_EQ((void*)ch, NULL);
}

TEST(channels, create_and_destroy) {
  channel* ch = channel_create(1);
  REQUIRE_NE((void*)ch, NULL);

  channel_destroy(ch);
  REQUIRE_EQ((void*)ch, NULL);
}

void* thr_for_channels_basic_send_and_receive(void* args) {
  // Using direct assertions in helper threads
  channel* ch = (channel*)args;

  char* msg = NULL;

  assert(chan_recv_zc(ch, (void**)&msg) == 1);

  assert(*msg == 'A');

  *msg = 'B';

  assert(chan_send_zc(ch, (void**)&msg, 1) == 1);

  assert(msg == NULL);

  return NULL;
}

TEST(channels, basic_send_and_receive) {
  channel* ch = channel_create(1);

  pthread_t tid;
  pthread_create(&tid, NULL, thr_for_channels_basic_send_and_receive, ch);

  char* m1 = (char*)malloc(sizeof(char));
  *m1 = 'A';
  REQUIRE_EQ(chan_send_zc(ch, (void**)&m1, 1), 1);
  REQUIRE_EQ(m1, NULL);

  char* m2 = NULL;
  REQUIRE_EQ(chan_recv_zc(ch, (void**)&m2), 1);
  REQUIRE_NE(m2, NULL);
  REQUIRE_EQ(*m2, 'B');

  free(m2);
  pthread_join(tid, NULL);
  channel_destroy(ch);
}

void* thr_for_channels_msg_count(void* args) {
  // Using direct assertions in helper threads
  channel* ch = (channel*)args;

  char* msg = NULL;

  for (int i = 3; i > 0; --i) {
    assert(chan_msg_count(ch, owner_to_workers) == i);
    chan_recv_zc(ch, (void**)&msg);
    assert(chan_msg_count(ch, owner_to_workers) == i - 1);
  }

  for (int i = 0; i < 3; ++i) {
    assert(chan_msg_count(ch, workers_to_owner) == i);
    chan_send_zc(ch, (void**)&msg, 1);
    assert(chan_msg_count(ch, workers_to_owner) == i + 1);
  }

  return NULL;
}

TEST(channels, msg_count) {
  channel* ch = channel_create(3);

  char* m1 = NULL;

  for (int i = 0; i < 3; ++i) {
    REQUIRE_EQ(chan_msg_count(ch, owner_to_workers), i);
    chan_send_zc(ch, (void**)&m1, 0);
    REQUIRE_EQ(chan_msg_count(ch, owner_to_workers), i + 1);
  }

  pthread_t tid;
  pthread_create(&tid, NULL, thr_for_channels_msg_count, ch);

  usleep(100000);

  for (int i = 3; i > 0; --i) {
    REQUIRE_EQ(chan_msg_count(ch, workers_to_owner), i);
    chan_recv_zc(ch, (void**)&m1);
    REQUIRE_EQ(chan_msg_count(ch, workers_to_owner), i - 1);
  }

  pthread_join(tid, NULL);
  channel_destroy(ch);
}

void* thr_for_channels_try_send_and_try_receive(void* args) {
  channel* ch = (channel*)args;

  usleep(50000);  // 50 msecs

  char* msg = NULL;
  assert(chan_try_recv_zc(ch, (void**)&msg) == 1);
  assert(msg != NULL);
  assert(*msg == 'A');

  *msg = 'B';

  char* m2 = NULL;
  assert(chan_try_recv_zc(ch, (void**)&m2) == -1);
  assert(m2 == NULL);

  assert(chan_try_send_zc(ch, (void**)&msg, 1) == 1);
  assert(msg == NULL);

  m2 = (char*)malloc(sizeof(char));
  assert(chan_try_send_zc(ch, (void**)&m2, 1) == -1);
  assert(m2 != NULL);
  free(m2);

  return NULL;
}

TEST(channels, try_send_and_try_receive) {
  channel* ch = channel_create(1);

  pthread_t tid;
  pthread_create(&tid, NULL, thr_for_channels_try_send_and_try_receive, ch);

  char* m1 = (char*)malloc(sizeof(char));
  *m1 = 'A';
  REQUIRE_EQ(chan_try_send_zc(ch, (void**)&m1, 1), 1);
  REQUIRE_EQ(m1, NULL);

  m1 = (char*)malloc(sizeof(char));
  REQUIRE_EQ(chan_try_send_zc(ch, (void**)&m1, 1), -1);
  REQUIRE_NE(m1, NULL);
  free(m1);
  m1 = NULL;

  usleep(100000);  // 100 msecs

  char* m2 = NULL;
  REQUIRE_EQ(chan_try_recv_zc(ch, (void**)&m2), 1);
  REQUIRE_NE(m2, NULL);
  REQUIRE_EQ(*m2, 'B');

  REQUIRE_EQ(chan_try_recv_zc(ch, (void**)&m1), -1);
  REQUIRE_EQ(m1, NULL);

  free(m2);
  pthread_join(tid, NULL);
  channel_destroy(ch);
}

void* thr_for_channels_timed_send_and_timed_receive(void* args) {
  channel* ch = (channel*)args;

  struct timespec timeout;
  timeout.tv_sec = 0;
  timeout.tv_nsec = 10000000;  // 10 msecs

  usleep(40000);  // 40 msecs

  struct timespec before;
  struct timespec after;

  char* msg = NULL;
  getWallTime(before);
  assert(chan_timed_recv_zc(ch, (void**)&msg, &timeout) == 1);
  getWallTime(after);
  assert(diffTimeUSec(before, after) < 3000);
  assert(msg != NULL);
  assert(*msg == 'A');

  *msg = 'B';

  char* m2 = NULL;
  getWallTime(before);
  assert(chan_timed_recv_zc(ch, (void**)&m2, &timeout) == -1);
  getWallTime(after);
  assert(diffTimeUSec(before, after) >= 10000);
  assert(m2 == NULL);

  getWallTime(before);
  assert(chan_timed_send_zc(ch, (void**)&msg, 1, &timeout) == 1);
  getWallTime(after);
  assert(diffTimeUSec(before, after) < 3000);
  assert(msg == NULL);

  m2 = (char*)malloc(sizeof(char));
  getWallTime(before);
  assert(chan_timed_send_zc(ch, (void**)&m2, 1, &timeout) == -1);
  getWallTime(after);
  assert(diffTimeUSec(before, after) >= 10000);
  assert(m2 != NULL);
  free(m2);

  return NULL;
}

TEST(channels, timed_send_and_timed_receive) {
  channel* ch = channel_create(1);

  pthread_t tid;
  pthread_create(&tid, NULL, thr_for_channels_timed_send_and_timed_receive, ch);

  char* m1 = (char*)malloc(sizeof(char));
  *m1 = 'A';

  struct timespec before;
  struct timespec after;

  struct timespec timeout;
  timeout.tv_sec = 0;
  timeout.tv_nsec = 10000000;  // 10 msecs

  getWallTime(before);
  REQUIRE_EQ(chan_timed_send_zc(ch, (void**)&m1, 1, &timeout), 1);
  getWallTime(after);
  REQUIRE_LT(diffTimeUSec(before, after), 3000);
  REQUIRE_EQ(m1, NULL);

  m1 = (char*)malloc(sizeof(char));
  getWallTime(before);
  REQUIRE_EQ(chan_timed_send_zc(ch, (void**)&m1, 1, &timeout), -1);
  getWallTime(after);
  REQUIRE_GE(diffTimeUSec(before, after), 10000);
  REQUIRE_NE(m1, NULL);
  free(m1);
  m1 = NULL;

  usleep(90000);  // 90 msecs

  char* m2 = NULL;
  getWallTime(before);
  REQUIRE_EQ(chan_timed_recv_zc(ch, (void**)&m2, &timeout), 1);
  getWallTime(after);
  REQUIRE_LT(diffTimeUSec(before, after), 3000);
  REQUIRE_NE(m2, NULL);
  REQUIRE_EQ(*m2, 'B');

  getWallTime(before);
  REQUIRE_EQ(chan_timed_recv_zc(ch, (void**)&m1, &timeout), -1);
  getWallTime(after);
  REQUIRE_GE(diffTimeUSec(before, after), 10000);
  REQUIRE_EQ(m1, NULL);

  free(m2);
  pthread_join(tid, NULL);
  channel_destroy(ch);
}

void* thr_for_enable_disable_sending(void* args) {
  channel* ch = (channel*)args;

  char* msg = NULL;
  assert(chan_recv_zc(ch, (void**)&msg) == 1);
  assert(msg != NULL);
  assert(*msg == 'A');
  *msg = 'B';

  chan_disable_sending(ch, workers_to_owner);
  assert(chan_send_zc(ch, (void**)&msg, 1) == -1);
  assert(msg != NULL);

  chan_enable_sending(ch, workers_to_owner);
  assert(chan_send_zc(ch, (void**)&msg, 1) == 1);
  assert(msg == NULL);

  return NULL;
}

TEST(channels, enable_disable_sending) {
  channel* ch = channel_create(1);

  pthread_t tid;
  pthread_create(&tid, NULL, thr_for_enable_disable_sending, ch);

  char* m1 = (char*)malloc(sizeof(char));
  m1[0] = 'A';

  chan_disable_sending(ch, owner_to_workers);

  REQUIRE_EQ(chan_send_zc(ch, (void**)&m1, 1), -1);
  REQUIRE_NE(m1, NULL);

  chan_enable_sending(ch, owner_to_workers);

  REQUIRE_EQ(chan_send_zc(ch, (void**)&m1, 1), 1);
  REQUIRE_EQ(m1, NULL);

  char* m2 = NULL;

  REQUIRE_EQ(chan_recv_zc(ch, (void**)&m2), 1);
  REQUIRE_NE(m2, NULL);
  REQUIRE_EQ(*m2, 'B');

  free(m2);
  pthread_join(tid, NULL);
  channel_destroy(ch);
}
