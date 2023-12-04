#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/fixed_point.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;	/* ready 상태의 thread를 관리하는 list */

static struct list all_list;	/* 모든 thread를 관리하는 list */


/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);
static struct list sleep_list;	/* 자는 쓰레드들을 저장할 sleep_list 추가 */

/* Returns true if T appears to point to a valid thread. */
/* 포인터가 유효한 쓰레드 객체를 가리키는 지 확인함. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
/* 현재 실행 중인 쓰레드를 반환함. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))

#define NICE_DEFAULT 0;
#define RECENT_CPU_DEFAULT 0;
#define LOAD_AVG_DEFAULT 0;

int load_avg;

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
/* 전체 쓰레드 초기화(초기 설정) */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);	/* 인터럽트가 비활성화 되어 있는지 확인 */

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);	/* 쓰레드 ID에 대한 접근을 동기화하기 위한 락을 초기화 */
	list_init (&ready_list);	/* 실행 대기 중인 쓰레드들을 관리하기 위한 리스트를 초기화함. */
	list_init (&destruction_req);	/* 파괴 요청을 받은 쓰레드들을 관리하기 위한 리스트를 초기화함. */
	list_init (&sleep_list);	/* 쓰레드 생성할 때 슬립 리스트 생성 */
	list_init (&all_list);	/* 모든 쓰레드를 추적하기 위한 연결 리스트 생성 */

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
/* 쓰레드가 실제로 동작하기 시작 */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);
	load_avg = LOAD_AVG_DEFAULT;

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* 쓰레드 재우기 */
void
thread_sleep (int64_t ticks)
{
  struct thread *curr;	/* 현재 쓰레드를 가리키는 포인터 cur을 선언함. */
  enum intr_level old_level;	/* 인터럽트 상태를 저장하기 위한 변수 old_level은 선언함. */

  old_level = intr_disable ();	/* 현재 인터럽트 상태를 old_level에 저장하고, 인터럽트를 비활성화함. (원자성 보장)_인터럽트 저장 후 비활성화 */
  curr = thread_current ();	/* 현재 실행 중인 쓰레드를 cur 변수에 할당함. */
  
  ASSERT (curr != idle_thread);	/* 현재 쓰레드가 idle_thread가 아님을 확인함. (이는 아이들 쓰레드가 sleep 상태로 가지 않는 것을 보장함.) */

  curr->wakeup = ticks;			/* 현재 쓰레드에 일어날 시간을 저장 */
  list_insert_ordered(&sleep_list, &curr->elem, cmp_thread_ticks, NULL);	/* sleep_list에  */
//   list_push_back (&sleep_list, &cur->elem);	// sleep_list 에 추가
  thread_block ();				// block 상태로 변경

  intr_set_level (old_level);	// 인터럽트 on
}

/* 쓰레드 깨우기 */
void
thread_awake (int64_t ticks)
{
  struct list_elem *e = list_begin (&sleep_list);

  while (e != list_end (&sleep_list)){
    struct thread *t = list_entry (e, struct thread, elem);
    if (t->wakeup <= ticks){	/* 쓰레드가 일어날 시간이 되었는지 확인	(깨어날 시간이 ticks 이하면 깨어날 시간이 되었음.) */
      e = list_remove (e);	// sleep list 에서 제거
      thread_unblock (t);	// 스레드 unblock하고 ready_list에 추가
	  if (!thread_mlfqs)
	  	preempt_priority();	/* 깨어난 쓰레드(unlock 상태)가 현재 실행 중인 쓰레드보다 높은 우선순위를 가지고 있을 경우, 현재 쓰레드의 실행을 중단하고 더 높은 우선 순위의 쓰레드에게 CPU를 양보함. */
    }
    else /* 깨어날 시간이 ticks 보다 크면 깨어날 시간이 되지 않음. */
      e = list_next (e);	/*  */
  }
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
/* 새로운 쓰레드를 생성하고, 필요한 리소스를 할당하며, 쓰레드를 실행 준비 상태로 만듦. */
/* 쓰레드 이름, 우선 순위, 실행할 함수, 추가적인 인자를 매개변수로 받음. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;	/* 쓰레드를 가리킬 포인터 t */
	tid_t tid;	/* 쓰레드 ID를 저장할 변수 tid */

	ASSERT (function != NULL);	/* 유효한 함수가 제공되었는지 검증하는 안전 체크 */

	/* Allocate thread. */
	/* 새 쓰레드에 대한 메모리를 페이지 단위로 할당하고, 할당된 메모리를 0으로 초기화함. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)	/* 페이지 할당에 실패한 경우 */
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);	/* 할당된 쓰레드 t를 초기화하고, 쓰레드의 이름과 우선 순위가 설정됨. */
	tid = t->tid = allocate_tid ();		/* 쓰레드에 고유한 쓰레드 ID를 할당하고, 이 값을 tid 변수에 저장함. */

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	thread_unblock (t);	/* 생성된 쓰레드를 실행 가능 상태로 만듦 */
	preempt_priority();	/* 새로 생성된 쓰레드가 현재 실행 중인 쓰레드보다 높은 우선 순위를 가질 경우, 현재 쓰레드를 양보하고 새 쓰레드에게 실행 기회를 주기 위함 */

	return tid;	/* 생성된 쓰레드의 ID를 반환함. */
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
/* 현재 thread를 차단(blocked) 상태로 전환함. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);	/* 인터럽트가 비활성화되어 있음을 확인함. (쓰레드 상태 변경이 인터럽트에 의해 중단되지 않도록 보장) */
	thread_current ()->status = THREAD_BLOCKED;	/* 현재 실행 중인 쓰레드의 상태를 THREAD_BLOCKED로 설정 */
	schedule ();	/* 스케줄러 함수를 호출하여 다음 실행할 쓰레드를 선택하고 전환함. 현재는 쓰레드가 차단된 상태이므로, 실행 가능한 다른 쓰레드를 찾음. */
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
/* 차단된 쓰레드를 다시 실행 가능한 상태로 변경 */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;	/* old_level이라는 변수 선언, 이 변수는 인터럽트 상태를 임시 저장하기 위해 사용됨. */

	ASSERT (is_thread (t));	/* t가 유효한 쓰레드인지 확인함, is_thread 함수는 t가 유효한 쓰레드 구조체를 가리키는 검증 */

	old_level = intr_disable ();	/* 현재의 인터럽트 레벨을 old_level에 저장하고, 인터럽트를 비활성화함. (이 함수가 실행되는 동안 인터럽트에 의해 방해받지 않도록) */
	ASSERT (t->status == THREAD_BLOCKED);	/* t 쓰레드의 상태가 THREAD_BLOCKED 인지 확인함. (실제로 차단된 상태인지를 검증) */
	list_insert_ordered(&ready_list, &t->elem, cmp_thread_priority, NULL);	/* 쓰레드를 우선순위에 따라 정렬된 ready_list에 삽입함 */
	t->status = THREAD_READY;	/* t 쓰레드의 상태를 THREAD_READY로 변경하여 실행 준비 상태로 만듦 */
	intr_set_level (old_level);	/* 이전에 저장된 인터럽트 레벨로 시스템의 인터럽트 상태를 복원함. */
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
/* 현재 실행 중인 쓰레드의 포인터를 반환하는 함수 */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();	/* running_thread 함수를 호출하여 현재 CPU에서 실행 중인 쓰레드의 포인터를 가져옴. */

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));	/* is_thread 함수를 사용하여 t가 유효한 쓰레드인지 확인함. */
	ASSERT (t->status == THREAD_RUNNING);	/* 쓰레드의 상태가 THREAD_RUNNING 인지 확인함. (즉, 실제로 실행 중인 상태인지 확인) */

	return t;	/* 검증이 완료된 현재 실행 중인 쓰레드의 포인터를 반환함. */
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
/* 현재 실행 중인 thread를 종료시킴. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
/* 현재 실행 중인 thread가 CPU를 다른 준비 상태에 있는 thread에게 양보함. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();	/* 현재 실행 중인 쓰레드를 가리키는 struct thread 타입의 포인터 curr를 선언하고 초기화함. */
	enum intr_level old_level;	/* 인터럽트 레벨을 저장하기 위한 old_level 변수를 선언함. */
	ASSERT (!intr_context ());	/* 인터럽트 컨텍스트가 호출되지 않았음을 보장 */
	old_level = intr_disable ();	/* 인터럽트를 비활성화하고, 이전 인터럽트 레벨을 old_level 변수에 저장 */

	if (curr != idle_thread)	/* idle 쓰레드가 아님을 확인 */
		list_insert_ordered(&ready_list, &curr->elem, cmp_thread_priority, NULL);	/* 현재 쓰레드를 우선 순위에 따라 정렬되어 있는 ready_list에 삽입 */

	do_schedule (THREAD_READY);	/* do_schedule 함수를 호출하여 쓰레드의 상태를 THREAD_READY로 변경함 */
	intr_set_level (old_level);	/* 이전에 저장된 인터럽트 레벨을 복원함. */
}

/* 특정 쓰레드의 우선 순위를 계산 */
/* MLFQS에서 사용되는 우선순위 계산을 위한 함수 */
/* 여기서 우선순위는 recent_cpu 값과 nice 값에 따라 결정됨 (이 방식으로 MLFQS는 CPU 자원을 공평하게 분배하고자 함)*/
void mlfqs_priority (struct thread *t)
{
	// /* 해당 쓰레드가 idle_thread가 아닌지 검사 */
	// ASSERT (t != idle_thread);
	// /* priority 계산식을 구현 (fixed_point.h의 계산함수 이용) */
	// t->priority = PRI_MAX - (div_mixed(t->recent_cpu, 4)) - (t->nice * 2);

	/* 해당 쓰레드가 idle_thread가 아닌지 검사 */
	/* priority 계산식을 구현 (fixed_point.h의 계산함수 이용) */
	/* priority = PRI_MAX - (recent_cpu / 4) - (nice * 2) */
	if (t != idle_thread) {	/* 현재 쓰레드가 아이들 쓰레드가 아닌지 확인 : 아이들 쓰레드는 우선 순위를 재계산할 필요가 없기 때문에 */
		int rec_by_4 = div_mixed(t->recent_cpu, 4);	/* recent_cpu / 4 */
		int nice2 = 2 * t->nice;	/* (nice * 2) */
		int to_sub = add_mixed(rec_by_4, nice2);	/* rec_by_4와 nice 2를 더한 값을 to_sub에 저장 */
		int tmp = sub_mixed(to_sub, PRI_MAX);	/* to_sub에 PRI_MAX를 뺀 값을 tmp에 저장함 */
		int pri_result = fp_to_int(sub_fp(0, tmp));	/* tmp를 0에서 빼서 고정 소수점 결과를 얻고, 그 결과를 정수로 변환하여 pri_result에 저장함 */
		if (pri_result < PRI_MIN)
			pri_result = PRI_MIN;
		if (pri_result > PRI_MAX)
			pri_result = PRI_MAX;
		t->priority = pri_result;	/* 계산된 우선 순위가 정해진 범위 밖에 있으면, 그 범위 안으로 조정함 */
	}
}

/* 주어진 쓰레드 t의 recent_cpu 값을 재계산 */
void mlfqs_recent_cpu (struct thread *t)
{
	// /* 해당 쓰레드가 idle_thread가 아닌지 검사 */
	// ASSERT (t != idle_thread);
	// /* recent_cpu 계산식을 구현 (fixed_point.h의 계산함수 이용) */
	// t->recent_cpu = mult_mixed(mult_mixed(2, load_avg) / (mult_mixed(2, load_avg) + 1), t->recent_cpu) + t->nice;

	/* 해당 쓰레드가 idle_thread가 아닌지 검사 */
	/* recent_cpu 계산식을 구현(fixed_point.h의 계산함수 이용) */
	/* recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice */
	if (t != idle_thread) {	/* 현재 쓰레드가 아이들 쓰레드인지 확인 : 아이들 쓰레드가 아닌 경우에만 계산을 진행 */
		int load_avg_2 = mult_mixed(load_avg, 2);	/* 시스템의 load average를 2배 한 값을 계산함. */
		int load_avg_2_1 = add_mixed(load_avg_2, 1); /* 이전에 계산한 값에 1을 더해, 나눗셈에서 분모를 구성함 */
		int frac = div_fp(load_avg_2, load_avg_2_1);	/* load_avg의 두 배에 1을 더한 값으로 load_avg의 두 배를 나눔 : recent_cpu 계산에 사용될 비율을 구함 */
		int tmp = mult_fp(frac, t->recent_cpu);	/* 계산된 비율 frac과 쓰레드의 recent_cpu를 곱함 */
		int result = add_mixed(tmp, t->nice);	/* 이전 단계의 결과에 쓰레드의 nice를 값을 더함 */
		if ((result >> 31) == (-1) >> 31) {	/* result가 음수인 경우를 검사하고, 그런 경우 result를 0으로 설정함 */
			result = 0;
		}
		t->recent_cpu = result;	/* 계산된 result_cpu 값을 쓰레드의 recent_cpu 필드에 저장 */
	}
}

void mlfqs_load_avg (void)
{	
	// int ready_threads = list_size(&ready_list);
	// struct thread *t = thread_current();

	// if(t != idle_thread)
	// 	ready_threads++;
	// /* load_avg계산식을 구현 (fixed_point.h의 계산함수 이용) */
	// load_avg = (59/60) * load_avg + (1/60) * ready_threads;
	// /* load_avg 는 0 보다 작아질 수 없다. */
	// ASSERT(load_avg >= 0);

	/* load_avg 계산식을 구현 (fixed_point.h의 계산함수 이용) */
	/* load_avg = (59/60) * load_avg + (1/60) * ready_threads */
	/* load_avg는 0 보다 작아질 수 없다. */
	int a = div_fp(int_to_fp(59), int_to_fp(60));	/* 고정 소수점으로 변환된 59/60을 계산함 */
	int b = div_fp(int_to_fp(1), int_to_fp(60));	/* 고정 소수점으로 변환된 1/60을 계산함 */
	int load_avg2 = mult_fp(a, load_avg);	/* 현재 load average에 59/60을 곱함 */
	int ready_thread = (int)list_size(&ready_list);	/* 준비 리스트의 크기를 가져옴 */
	ready_thread = (thread_current() == idle_thread) ? ready_thread : ready_thread + 1;	/* 현재 쓰레드가 아이들 쓰레드가 아니라면 준비 쓰레드의 수에 1을 더함 */
	int ready_thread2 = mult_mixed(b, ready_thread);	/* 준비 쓰레드의 1/60을 곱함 */
	int result = add_fp(load_avg2, ready_thread2);	/* 이전에 계산된 load average에 새로운 준비 쓰레드 수를 더함 */
	load_avg = result;	/* 최종 결과를 load average에 저장 */
	if (load_avg < 0)	/* load_avg는 0보다 작아질 수 없다. */
		load_avg = LOAD_AVG_DEFAULT;
}

/* 타이머 인터럽트가 발생할 때마다 호출되어, 현재 실행 중인 쓰레드가 시간을 사용했음을 반영하여 recent_cpu 값을 증가시키는 역할 */
void mlfqs_increment (void)
{
	// struct thread *t = thread_current();
	
	// /* 해당 스레드가 idle_thread 가 아닌지 검사 */
	// ASSERT(t != idle_thread);
	// /* 현재 스레드의 recent_cpu 값을 1증가 시킨다. */
	// add_mixed(t->recent_cpu, 1);

	/* 해당 쓰레드가 idle_thread가 아닌지 검사 */
	/* 현재 스레드의 recent_cpu 값을 1증가 시킨다. */
	if (thread_current() != idle_thread) {	/* 현재 실행 중인 쓰레드가 아이들 쓰레드가 아닐 경우에만 recent_cpu를 증가시킴 */
		thread_current()->recent_cpu = add_mixed(thread_current()->recent_cpu, 1);	/* 현재 쓰레드의 recent_cpu 값에 1을 더하여 업데이트 */
	}

}

void mlfqs_recalc_recent_cpu(void) {
    for (struct list_elem *tmp = list_begin(&all_list); tmp != list_end(&all_list); tmp = list_next(tmp)) {
        mlfqs_recent_cpu(list_entry(tmp, struct thread, allelem));
    }
}

void mlfqs_recalc_priority(void) {
    for (struct list_elem *tmp = list_begin(&all_list); tmp != list_end(&all_list); tmp = list_next(tmp)) {
        mlfqs_priority(list_entry(tmp, struct thread, allelem));
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
/* 현재 실행 중인 쓰레드의 우선 순위를 변경하는 기능을 수행 */
void
thread_set_priority (int new_priority) {
	if (thread_mlfqs) return;	/* mlfqs 스케줄러일때 우선 순위를 임의로 변경할 수 없도록 함 */
	thread_current ()->priority = new_priority;	/* 쓰레드의 우선 순위를 new_priority로 변경 */
	thread_current ()->init_priority = new_priority;
	update_priority_for_donations();	/* 쓰레드의 우선 순위가 변경될 때, 이 함수는 우선 순위 기부 메커니즘에 의해 영향을 받을 수 있는 상황을 고려함. 우선 순위 기부 메커니즘은 높은 우선 순위의 쓰레드가 낮은 우선 순위의 쓰레드에게 자신의 우선 순위를 기부할 수 있게 함. */
	preempt_priority();	/* 현재 쓰레드(우선 순위 변경된)와 ready_list의 쓰레드 우선 순위를 비교하여, 필요한 경우 더 높은 우선 순위의 쓰레드로 변경 */
}

/* Returns the current thread's priority. */
/* 현재 실행 중인 쓰레드의 우선 순위를 반환함. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
/* 현재 실행 중인 쓰레드의 nice 값을 변경하고, 이 변경에 따라 쓰레드의 우선 순위를 재계산 후, 필요하다면 스케줄링을 다시 수행 */
void
thread_set_nice (int nice UNUSED) {
	/* 현재 스레드의 nice 값을 변경한다. */
	/* nice 값 변경 후에 현재 스레드의 우선순위를 재계산하고 우선순위에 의해 스케줄링 한다. */
	/* 해당 작업중에 인터럽트는 비활성화 해야 한다. */
	struct thread *t = thread_current();	/* 현재 실행 중인 쓰레드의 포인터를 t에 저장 */
	enum intr_level old_level;	/* 이전 인터럽트 상태를 저장할 변수를 선언 */

	old_level = intr_disable();	/* 현재 인터럽트 상태를 저장하고 인터럽트를 비활성화함 */
	t->nice = nice;	/* 현재 쓰레드의 nice 값을 새로운 값으로 설정 */
	mlfqs_priority(t);	/* 변경된 nice 값에 따라 현재 쓰레드의 우선 순위를 재계산함 */
	preempt_priority();	/* 우선 순위 변경에 따라 현재 쓰레드가 CPU를 양보할 필요가 있는지 검사 */
	intr_set_level(old_level);	/* 작업이 종료되면 이전 인터럽트 상태로 복원 */
}

/* Returns the current thread's nice value. */
/* 현재 실행 중인 쓰레드의 nice 값을 반환함 */
int
thread_get_nice(void)
{
	/* 현재 스레드의 nice 값을 반환한다. */
	/* 해당 작업중에 인터럽트는 비활성되어야 한다. */
	struct thread *t = thread_current();	/* 현재 실행 중인 쓰레드의 포인터를 t에 저장 */
	enum intr_level old_level;	/* 이전 인터럽트 상태를 저장할 변수를 선언함 : nice 값 읽기 작업 동안 인터럽트에 의해 방해받지 않도록 하기 위함. */

	old_level = intr_disable();	/* 현재 인터럽트 상태를 저장하고 인터럽트를 비활성화함 */
	int nice_val = t->nice;	/* 현재 쓰레드의 nice 값을 nice_val 변수에 저장 */
	intr_set_level(old_level);	/* 작업이 완료되면 이전 인터럽트 상태로 복원 */

	return nice_val;	/* 저장된 nice 값 반환 */
}

/* Returns 100 times the system load average. */
/* 현재 시스템의 load average 값을 계산하여 반환함 */
/* load average는 시스템의 평균 부하를 나타내며 100배로 확장되어 반환됨 */
int
thread_get_load_avg(void)
{
	/* load_avg에 100을 곱해서 반환 한다.
	   해당 과정중에 인터럽트는 비활성되어야 한다. */
	/* timer_ticks() % TIMER_FREQ == 0 */
	enum intr_level old_level;	/* 이전 인터럽트 상태를 저장하기 위한 변수 선언 */

	old_level = intr_disable();	/* 현재 인터럽트 상태를 저장하고 인터럽트를 비활성화 */
	int new_load_avg = fp_to_int(mult_mixed(load_avg, 100));	/* 시스템의 load average 값을 100배 확장하고, 고정 소수점 표현에서 정수로 변환 */
	intr_set_level(old_level);	/* 인터럽트 상태를 이전 상태로 복원 */

	return new_load_avg;	/* 계산된 load average 값을 반환 */
}

/* Returns 100 times the current thread's recent_cpu value. */
/* 현재 실행 중인 쓰레드의 recent_cpu 값을 반환하는 것 */
/* recent_cpu 값은 100배로 확장되어 반환됨 */
int
thread_get_recent_cpu(void)
{
	/* recent_cpu에 100을 곱해서 반환 한다.
	   해당 과정중에 인터럽트는 비활성되어야 한다. */
	enum intr_level old_level;	/* 현재 인터럽트 상태를 저장하기 위한 변수 */

	old_level = intr_disable();	/* 현재 인터럽트 상태를 저장하고 인터럽트를 비활성화 */
	int new_recent_cpu = fp_to_int(mult_mixed(thread_current()->recent_cpu, 100));	/* 현재 쓰레드의 recent_cpu 값을 100배 확장하고, 고정 소수점 표현에서 정수로 변환함. */
	intr_set_level(old_level);	/* 인터럽트 상태를 이전 상태로 복원 */

	return new_recent_cpu;	/* recent_cpu 값을 반환 */
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
/* 쓰레드를 초기화할 때 사용 */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);	/* 입력 파라미터인 t가 NULL 포인터인지 확인 */
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);	/* priority가 최소값(PRI_MIN)과 최대값(PRI_MAX 사이에 있는 지 확인) */
	ASSERT (name != NULL);	/* 쓰레드의 이름을 나타내는 name 포인터가 NULL이 아닌지 확인 */

	memset (t, 0, sizeof *t);	/* 쓰레드 객체 t를 0으로 초기화 */
	t->status = THREAD_BLOCKED;	/* 쓰레드의 상태를 THREAD_BLOCKED로 설정하여, 대기 상태임을 나타냄 */
	strlcpy (t->name, name, sizeof t->name);	/* 쓰레드의 이름을 name에서 t->name으로 복사 */
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;	/* 쓰레드의 우선순위를 입력받은 priority 값으로 설정*/
	t->magic = THREAD_MAGIC;	/* 쓰레드의 magic 필드를 THREAD_MAGIC 값으로 설정하여, 쓰레드 구조체의 유효성을 추후에 확인할 수 있음 */

	t->init_priority = priority;	/* 쓰레드의 init_priority 필드를 입력받은 priority 값으로 설정, 쓰레드의 원래 우선 순위를 저장하는 역할 */
	t->wait_on_lock = NULL;	/* 쓰레드가 현재 기다리고 있는 lock을 가리키는 포인터를 NULL로 설정함 */
	list_init(&(t->donations));	/* 쓰레드가 받은 우선 순위 기부 목록을 초기화함. */

	t->nice = NICE_DEFAULT;
	t->recent_cpu = RECENT_CPU_DEFAULT;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
/* do_schedule : 현재 실행 중인 쓰레드의 상태를 변경하고, 더 이상 필요없는 쓰레드의 자원을 해제하는 역할 */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);	/* 인터럽트 비활성화 확인, 쓰레드 스케줄링 수행 전에 인터럽트가 비활성화되어 있어야 원자성 보장 */
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

/* schedule : 현재 실행 중인 쓰레드를 다음 쓰레드로 교체하는 역할 */
static void
schedule (void) {
	struct thread *curr = running_thread ();	/* 현재 실행 중인 쓰레드의 포인터를 가져와 curr 변수에 저장 */
	struct thread *next = next_thread_to_run ();	/* 다음으로 선택된 쓰레드의 포인터를 얻어와 next 변수에 저장 */

	ASSERT (intr_get_level () == INTR_OFF);	/* 인터럽트가 비활성화되어 있는 지 확인 (이는 스케줄링 동안 인터럽트가 발생하지 않도록) */
	ASSERT (curr->status != THREAD_RUNNING); /* 현재 실행 중인 쓰레드가 이미 실행 상태가 아니라는 것을 확인함. */
	ASSERT (is_thread (next));	/* next 변수가 유효한 쓰레드 구조체를 가리키고 있는 지 검사함. */
	/* Mark us as running. */
	next->status = THREAD_RUNNING;	/* 다음 쓰레드의 상태를 THREAD_RUNNING으로 설정 */

	/* Start new time slice. */
	thread_ticks = 0;	/* 쓰레드의 타이머 카운터를 0으로 초기화함. */

#ifdef USERPROG	/* USERPROG가 정의되었을 때만 컴파일되는 조건부 컴파일 지시문 */
	/* Activate the new address space. */
	process_activate (next);	/* 다음 쓰레드에 대한 프로세스 환경을 활성화하는 함수 호출 */
#endif	/* 조건부 컴파일 종료를 의미함. */

	if (curr != next) {	/* 현재 쓰레드와 다음 쓰레드가 다를 경우에만 아래의 코드를 실행함. */
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {	/* 현재 쓰레드가 유효하고, 그 상태가 THREAD_DYING이며, 초기 쓰레드가 아닐 경우에만 실행 */
			ASSERT (curr != next);	/* 현재 쓰레드와 다음 쓰레드가 다른지 확인 */
			list_push_back (&destruction_req, &curr->elem);	/* 현재 쓰레드를 destruction_req에 추가함. */
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);	/* 다음 쓰레드를 실행함.*/
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

/* cmp_thread_priority 함수 : ready_list에 priority가 높은 쓰레드가 앞 부분에 위치하도록 정렬하는 함수 */
/* list_insert_ordered 같은 함수에 의해 호출되어, 쓰레드를 우선 순위에 따라 정렬된 순서로 ready_list에 삽입하는 데 사용됨. */
bool cmp_thread_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
    struct thread *st_a = list_entry(a, struct thread, elem);	/* list_entry 매크로를 사용하여 list_elem 구조체를 thread 구조체로 변환 */
    struct thread *st_b = list_entry(b, struct thread, elem);	/* a와 b 각각에 해당하는 쓰레드의 실제 데이터에 접근할 수 있음. */
    return st_a->priority > st_b->priority;	/* 우선순위 비교 : 두 쓰레드의 우선순위를 비교하여 전자가 큰 경우 true, 아닌 경우 false */
}

bool
cmp_thread_ticks(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *st_a = list_entry(a, struct thread, elem);
	struct thread *st_b = list_entry(b, struct thread, elem);
	return st_a->wakeup < st_b->wakeup;
}

/* preempt_priority(양보) : 현재 실행 중인 쓰레드와 ready_list에 있는 쓰레드들의 우선순위를 비교하여,
					  만약 ready_list에 우선 순위가 높은 쓰레드가 있다면 현재 실행 중인 쓰레드를 양보하고
					  스케줄링을 통해 ready_list의 가장 높은 우선 순위 쓰레드가 실행되도록 함. */
void preempt_priority(void)
{
    if (thread_current() == idle_thread)	/* 현재 실행 중인 쓰레드가 idle 쓰레드인 경우 (즉, 아무런 쓰레드도 실행 가능한 상태가 아닌 경우) */
        return;								/* 함수를 바로 종료함. 아무런 쓰레드도 실행할 수 없는 상황에서 어떤 쓰레드도 양보할 필요가 없기 때문에 */
    if (list_empty(&ready_list))			/* ready_list가 비어있는 경우 함수를 종료 (실행 가능한 쓰레드가 없으면 양보할 쓰레드가 없기 때문에) */	
        return;
    struct thread *curr = thread_current();	/*  */
    struct thread *ready = list_entry(list_front(&ready_list), struct thread, elem);
    if (curr->priority < ready->priority) // ready_list에 현재 실행중인 스레드보다 우선순위가 높은 스레드가 있으면
        thread_yield();
}


