#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// change 10. define RR time slice
// unit is millisecond
// depending on the complexity of instructions in f, a thread may not be swapped out
// but changing TIME_SLICE to 0 will guarantee yielding to another thread
double TIME_SLICE = 10.0;

enum {
  MaxGThreads = 4, // up to change, must be greater than 0
  StackSize = 0x400000,
};

// change 0: scheduler poligy type
enum { FIFO, RR, OTHER };
// default policy is other
short current_policy = OTHER;

struct gt {
  struct gtctx {
    uint64_t rsp;
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbx;
    uint64_t rbp;
  } ctx; // cpu state--> frozen execution context for the thread only when state
         // is READY
  enum {
    Unused,
    Running,
    Ready,
  } st;
  unsigned short priority; // change 1. added field to gt struct, range 1 - 99
  long long thread_queue_entrance; // change 8.
};

struct gt gttbl[MaxGThreads]; // green thread table with Max size
struct gt *gtcur;             // ptr to current green thread

// change 7. thread table entrance counter (for FIFO)
long long entrance_counter = 0;

void gtinit(int);
void gtret(int ret);
void gtswtch(struct gtctx *old, struct gtctx *new);
bool gtyield(void);
static void gtstop(void);
int gtgo(void (*f)(void),
         unsigned short); // change 4. changed forward declaration

void gtinit(
    int plcy) // change 5. Changed gtinit signature to specify schedule policy
{
  gtcur = &gttbl[0];
  gtcur->st = Running;
  current_policy = plcy;
}

void __attribute__((noreturn)) gtret(int ret) {
  if (gtcur != &gttbl[0]) {
    gtcur->st = Unused;
    gtyield();
    assert(!"reachable");
  }
  while (gtyield())
    ;
  exit(ret);
}

bool gtyield(void) {
  struct gt *p;
  struct gtctx *old, *new;

  // change 6
  // decide the succeeding thread to execute
  if (current_policy == OTHER) {
    // find the ready thread with highest policy
    unsigned short highest = 0;
    struct gt *highestPtr = &gttbl[1]; // table size must be greater than 0!
    while (highestPtr != &gttbl[MaxGThreads]) {
      if (highestPtr->st == Ready && highestPtr->priority > highest) {
        p = highestPtr;
        highest = highestPtr->priority;
      }
      highestPtr++;
    }
    if (! p) {
        return false;
    }
      
  } else if (current_policy == FIFO) {
    long long min = entrance_counter;
    struct gt *earliestPtr = &gttbl[1]; // table size must be greater than 0!
    while (earliestPtr != &gttbl[MaxGThreads]) {
      if (earliestPtr->st == Ready &&
          earliestPtr->thread_queue_entrance < min) {
        p = earliestPtr;
        min = earliestPtr->thread_queue_entrance;
      }
      earliestPtr++;
    }
    if (! p) {
        return false;
    }
  } else if (current_policy == RR) {

    // sees the thread table as a circular buffer and always get the next ready
    // slot
    // uses the original code
    p = gtcur;
    while (p->st != Ready) {
      if (++p == &gttbl[MaxGThreads]) {
        p = &gttbl[0];
      }
      if (p == gtcur) {
        return false;
      }
    }
  } else {
    assert(!"reachable");
  }

  if (p == gtcur) {
    return false;
  }

  if (gtcur->st != Unused)
    gtcur->st = Ready;
  p->st = Running;
  old = &gtcur->ctx;
  new = &p->ctx;
  gtcur = p;
  gtswtch(old, new);
  if (current_policy == FIFO) {
    // increment entrance counter and
    // set the succeeding thread to the highest so far
    // setting p's thread_queue_entrance here instead of deciding where p is
    // pointing is
    // to satisfy transactional behavior because error happening during switch
    // (assume there might be) will ruin p's chance to be scheduled properly,
    // e.g.
    // nondeterministically FILO...
    p->thread_queue_entrance = ++entrance_counter;
  }

  return true;
}

static void gtstop(void) { gtret(0); }

int gtgo(
    void (*f)(void),
    unsigned short prrty) // change 2. change signature of gtgo to pass priority
{



  char *stack;
  struct gt *p;

  for (p = &gttbl[0];; p++)
    if (p == &gttbl[MaxGThreads])
      return -1;
    else if (p->st == Unused)
      break;

  stack = malloc(StackSize);
  if (!stack)
    return -1;

  *(uint64_t *)&stack[StackSize - 8] = (uint64_t)gtstop;
  *(uint64_t *)&stack[StackSize - 16] = (uint64_t)f;
  p->ctx.rsp = (uint64_t)&stack[StackSize - 16];
  p->st = Ready;
  p->priority = prrty; // change 3. init the priority level of thread



  // change 9. track entry time if schedule policy if FIFO
  
  if (current_policy == FIFO) {
    p->thread_queue_entrance = ++entrance_counter;
    printf("entrance number is %d\n", p->thread_queue_entrance);

  } else {
    p->thread_queue_entrance = 0; // don't care in this case
    if (current_policy == OTHER) {
       printf("priority is %d\n", p->priority);
    }
  }

  return 0;
}

/* Now, let's run some simple threaded code. */

void f(void) {
  clock_t start_time = clock();
  static int x;
  int i, id;

  id = ++x;
  for (i = 0; i < 10; i++) {
    printf("%d %d\n", id, i);
    if (current_policy != RR ||
        (clock() - start_time) / (double)CLOCKS_PER_SEC >=
            TIME_SLICE * 1000.0) {
      gtyield();
    }
  }
}

int main(void) {
  //gtinit(OTHER);
  //gtinit(RR);
  //gtinit(FIFO);
  gtgo(f, 1);
  gtgo(f, 10);
  gtgo(f, 80);

  gtret(1);
}
