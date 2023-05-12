A quite simple intra-process communication library written in
plain, old C.

Here, the focus is the speed which requires some care while
using the library. The rule of thumb can be summarized as
"ALWAYS EXCHANGE ADDRESSES OF POINTERS TO MESSAGES THAT
RESIDE IN THE HEAP AREA, NEVER SEND ADDRESSES OF POINTERS
THAT CONTAIN ADDRESSES OF MESSAGES THAT RESIDE IN THE STACK
AREA". By doing that we are eliminating the copy overhead.
This improves the speed and decreases the need for memory.
Again, it should be used with pracaution and care, you may
think of it as summoning a demon, If you know how to control
it, it's quite powerful.

So, you have been warned.

OK, I know that the text above looks intimidating, but
actually all I'm saying is to use it simply like the
following code snippet:

```c
/* Example usage of circular queues */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <thread_comm.h>


int main() {
    int retval;

    circular_queue* cq = circular_queue_create(1);
    char * m1 = (char*) malloc(2*sizeof(char));
    assert(cq && m1);

    m1[0] = 'A';
    m1[1] = '\0';

    retval = circq_send_zc(cq, (void**)&m1, 2); // The ownership of the
                                                // message is now lost.
                                                // m1 is NULL (assuming
                                                // the call to sendto_cq
                                                // a success).
    assert(retval == 2);

    char* m2 = NULL;
    retval = circq_recv_zc(cq, (void**)&m2);    // And regained here via
                                                // the call recvfrom_cq
                                                // stored in m2.

    assert(retval == 2);
    assert(m2[0] == 'A');
    assert(m2[1] == '\0');

    free(m2); // Not m1, as it became NULL in sendto_cq()

    circular_queue_destroy(cq);

    return 0;
}
```
