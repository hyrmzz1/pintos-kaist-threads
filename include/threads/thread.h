#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */	/* 쓰레드 식별자 */
	enum thread_status status;          /* Thread state. */	/* 쓰레드의 현재 상태를 나타내는 열거형(enum) 변수 : RUNNING, READY, BLOCKED, DYING */
	char name[16];                      /* Name (for debugging purposes). */	/* 쓰레드의 이름 */
	int priority;                       /* Priority. */	/* 쓰레드의 우선 순위를 나타내는 정수, 스케줄링에서 활용됨. */
	/* Shared between thread.c Fand synch.c. */
	struct list_elem allelem;		/* */
	struct list_elem elem;              /* List element. */	/* 쓰레드를 여러 리스트에 연결하기 위한 구조체 */
	int64_t wakeup;	/* 쓰레드가 깨어나야 하는 시간(틱스)을 나타내는 변수 */
	
	int init_priority;	/* 쓰레드가 생성될 때, 혹은 thread_set_priority로 갖게된 쓰레드의 priority를 저장, donation이 종료될 때 기존의 priority로 돌아오기 위한 필드 */
	struct lock *wait_on_lock; /* 쓰레드가 현재 대기하고 있는 lock을 가리킴 */
	struct list donations;	/* 다른 쓰레드로부터 받은 우선순위 기부를 관리하는 리스트 */
	struct list_elem donation_elem;	/* 우선순위 기부 리스트 내에서 쓰레드의 위치를 표시 */

	int nice;	/* 쓰레드의 친절도를 나타내며 쓰레드의 nice 값은 다른 쓰레드에 대한 우선 순위를 결정하는 데 사용됨 */
	int recent_cpu;	/* 쓰레드가 최근에 사용한 CPU 시간의 양을 추적하며 이 값은 쓰레드의 우선 순위를 결정하는 데 중요한 요소임 */

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

void thread_sleep(int64_t ticks);
void thread_awake(int64_t ticks);

bool cmp_thread_ticks(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
bool cmp_thread_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);	/* 정렬에 활용할 cmp_thread_priority 함수 선언 */

void preempt_priority(void);	/* 현재 실행 중인 쓰레드의 우선 순위와 ready_list의 쓰레드 우선 순위를 비교하여, 필요한 경우 현재 쓰레드의 실행을 중단하고 더 높은 우선 순위의 쓰레드로 전환하는 로직 */

bool cmp_donation_priority(const struct list_elem *a, const struct list_elem *b, void *aux); /* donation_elem을 priority를 기준으로 내림차순 정렬하는 함수 */

void donate_priority(void);	/* 현재 쓰레드가 기다리고 있는 lock을 소유한 쓰레드(holder)에게 우선 순위를 상속하는 기능을 수행 */

void remove_donor(struct lock *lock);	/* 특정 lock에 대한 우선순위 기부를 제거함 */

void update_priority_before_donations(void);	/* 기부 처리 전에 쓰레드의 우선순위를 업데이트함 */

void mlfqs_priority(struct thread *t);	/* 주어진 쓰레드 t의 우선 순위를 계산함 */
void mlfqs_recent_cpu(struct thread *t);	/* 쓰레드 t의 recent_cpu 값을 갱신함 */
void mlfqs_load_avg(void);	/* 시스템의 평균 부하(load_avg)를 업데이트함. load_avg는 준비 상태에 있는 쓰레드들의 평균 수를 나타냄 */
void mlfqs_increment(void);	/* 현재 실행 중인 쓰레드의 recent_cpu 값을 증가시킴 */
void mflqs_recalc(void);	/* 시스템의 모든 쓰레드에 대해 recent_cpu와 우선 순위를 재계산함 */


void mlfqs_recalc_recent_cpu(void);
void mlfqs_recalc_priority(void);

#endif /* threads/thread.h */
