#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* A counting semaphore. */
/* semaphore 구조체 */
struct semaphore {
	unsigned value;             /* Current value. */	/* 세마포어의 현재 값을 나타내는 unsigned 정수, 예를 들어 value가 0이면 자원이 사용 중이거나 사용 가능한 자원이 없음을 의미함. */
	struct list waiters;        /* List of waiting threads. */	/* 세마포어에 의해 차단된(자원을 기다리고 있는) 쓰레드들의 목록을 나타냄. */
};

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

/* Lock. */
/* lock 구조체 */
struct lock {
	struct thread *holder;      /* Thread holding lock (for debugging). */ /* lock을 소유하고 있는 쓰레드 */
	struct semaphore semaphore; /* Binary semaphore controlling access. */	/* lock을 제어하는 데 사용되는 이진 세마포어 */
};

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* Condition variable. */
struct condition {
	struct list waiters;        /* List of waiting threads. */
};

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

bool cmp_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux);	/* 연결 리스트에서 두 세마포어 요소(a, b)의 우선 순위를 비교함*/

/* Optimization barrier.
 *
 * The compiler will not reorder operations across an
 * optimization barrier.  See "Optimization Barriers" in the
 * reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
