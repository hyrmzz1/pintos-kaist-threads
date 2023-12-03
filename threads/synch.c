/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
/* semaphore를 초기화하는 함수 */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);	/* sema 포인터가 NULL이 아님을 확인함. */

	sema->value = value;	/* sema 구조체의 value 멤버에 전달된 value를 할당함. 이는 초기 카운트를 설정하는 것 */
	list_init (&sema->waiters);	/* sema 구조체 내에 있는 waiter 리스트를 초기화함. 이 리스트는 세마포어를 기다리는 쓰레드들을 관리하는 데 사용됨. */
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
/* 세마 포어의 value를 낮추는 함수(세마포어를 매개 변수로 받음) */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;	/* 이전 인터럽트 레벨을 저장할 변수 */

	ASSERT (sema != NULL);	/* 전달된 세마포어 포인터가 NULL이 아닌 지 확인함 */
	ASSERT (!intr_context ());	/* 인터럽트 컨텍스트가 실행되지 않고 있는 지 확인함. */

	old_level = intr_disable ();	/* 현재 인터럽트를 비활성화하고 이전 인터럽트 레벨을 old_level에 저장함. */
	while (sema->value == 0) {	/* 세마포어의 값이 0이면 반복문이 실행됨. 세마포어 값이 0이라는 것은 해당 자원이 사용 중임을 의미함. */
		// list_push_back (&sema->waiters, &thread_current ()->elem);	/* 현재 쓰레드를 세마포어의 대기 목록에 추가함. */
		list_insert_ordered(&sema->waiters, &thread_current()->elem, cmp_thread_priority, NULL);	/* waiters에 쓰레드를 삽입할 때 priority가 높은 쓰레드가 앞부분에 위치하도록 정렬함. */
		thread_block ();	/* 현재 쓰레드를 block(대기 상태)함. 이는 세마포어가 사용 가능해질 때까지 쓰레드가 실행되지 않도록 함.*/
	}
	sema->value--;	/* 세마포어의 값을 감소시킴 */
	intr_set_level (old_level);	/* 이전 인터럽트 레벨을 복원함. */
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
/* 세마포어가 사용 가능한 경우(value가 0보다 큰 경우)에만 동기화를 수행함 */
/* 만약 세마포어가 사용 중이라면(value가 0 이하), 즉시 false를 반환함.*/
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;	/* 이전 인터럽트 레벨을 저장할 변수를 선언함. */
	bool success;	/* 동작의 성공 여부를 나타낼 변수를 선언함. */

	ASSERT (sema != NULL);	/* 전달된 세마포어 포인터가 NULL이 아닌지 확인 */

	old_level = intr_disable ();	/* 인터럽트를 비활성화하고 이전 인터럽트 레벨을 old_level에 저장함. */
	if (sema->value > 0)	/* 세마포어의 value가 0보다 큰지 확인, 이는 세마포어로 보호되는 자원이 사용 가능한지를 나타냄. */
	{
		sema->value--;	/* 세마포어의 value를 감소시킴 */
		success = true;	/* 동작이 성공적으로 수행되었음을 나타내기 위해 success 변수를 true로 설정 */
	}
	else
		success = false;	/* 세마포어의 value가 0이거나 그 이하이면, 동작은 실패했음을 나타내기 위해 success를 false로 설정 */
	intr_set_level (old_level);	/* 이전 인터럽트 레벨을 복원 */

	return success;	/* 동작의 성공 여부를 반환 */
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
/* 세마포어의 value를 증가시키고, 만약 대기 중인 쓰레드가 있다면 하나를 깨워 실행 가능 상태로 만듦. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;	/* 이전 인터럽트 레벨을 저장할 변수를 선언함. */

	ASSERT (sema != NULL);	/* 전달된 세마포어 포인터가 NULL이 아닌 지 확인함. */

	old_level = intr_disable ();	/* 현재 인터럽트를 비활성화하고, 이전 인터럽트 레벨을 old_level 변수에 저장함. */
	if (!list_empty (&sema->waiters))	/* 세마포어의 대기 목록이 비어있는 지 확인 */
		{
			list_sort(&sema->waiters, cmp_thread_priority, NULL);	/* 세마포어의 대기 목록(waiters)을 우선순위에 따라 정렬함. */
			thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem)); /* 대기 목록에서 첫 번째 쓰레드를 꺼내고, 이 쓰레드를 블록 해제하여 실행 가능 상태로 만듦. */
		}
	sema->value++;	/* 세마포어의 value를 증가시킴 (이는 세마포어로 보호되는 자원이 이제 사용 가능함을 의미함.) */
	preempt_priority();	/* 현재 실행 중인 쓰레드보다 높은 우선순위의 쓰레드가 대기 목록에 있으면 현재 쓰레드가 CPU를 양보하도록 함. */
	intr_set_level (old_level); /* 이전 인터럽트 레벨을 복원함. */
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
/* lock을 초기화하는 함수 */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);	/* lock 포인터가 NULL이 아님을 확인함. 즉, 유효한 메모리 주소를 가리키고 있음을 보장 */

	lock->holder = NULL;	/* lock 구조체의 holder 멤버를 NULL로 설정함. 아직 어떤 쓰레드도 이 lock을 소유하고 있지 않음을 나타냄. */
	sema_init (&lock->semaphore, 1);	/* lock 구조체 내에 있는 semaphore를 초기화함. 1로 초기화하며 이는 하나의 쓰레드만이 락을 소유할 수 있음을 의미함. */
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/* lock을 획득하는 함수 */
/* 우선순위 기부(priority donation) 메커니즘을 사용하여 락을 안전하게 획득하고, 우선순위 역전 문제를 방지 */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);	/* 전달된 락 포인터가 NULL이 아닌 지 확인함. */
	ASSERT (!intr_context ());	/* 현재 인터럽트 컨텍스트에서 실행되지 않고 있는 지 확인함. lock을 획득하는 동안 인터럽트가 발생하지 않도록 하기 위해 */
	ASSERT (!lock_held_by_current_thread (lock));	/* 현재 쓰레드가 이미 이 락을 소유하고 있는지 확인 */

	struct thread *curr = thread_current();	/* 현재 실행 중인 쓰레드의 포인터를 curr 변수에 저장 */
    if (lock->holder != NULL) // 이미 점유중인 락이라면
    {
        curr->wait_on_lock = lock; // 현재 쓰레드가 lock을 기다리고 있음을 나타내기 위해 wait_on_lock 설정
        // lock holder의 donors list에 현재 스레드 추가
        list_insert_ordered(&lock->holder->donations, &curr->donation_elem, cmp_donation_priority, NULL); /* 현재 스레드를 락의 보유자의 기부 목록(donations)에 추가합니다. 목록은 우선순위에 따라 정렬됨 */
        donate_priority(); // 현재 스레드의 priority를 lock holder에게 상속해줌 (우선 순위 역전 방지)
    }

	sema_down (&lock->semaphore);	/* 세마포어를 사용하여 lock을 점유함 */

	curr->wait_on_lock = NULL; // lock을 획득했으므로 wait_on_lock을 NULL로 설정하여 더 이상 어떤 lock도 기다리고 있지 않음을 나타냄

	lock->holder = thread_current ();	/* 현재 쓰레드를 락의 holder(소유자)로 변경 */
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
/* 락을 시도적으로 획득하는 함수 */
/* 시도적 획득 :락이 이미 다른 쓰레드에 의해 보유되고 있어서 즉시 사용할 수 없는 경우, 즉시 대기 상태로 들어가지 않고 */
/* 대신 락 획득에 실패했다는 결과를 반환함 */
bool
lock_try_acquire (struct lock *lock) {
	bool success;	/* 락 획득 시도의 성공 여부를 저장 */

	ASSERT (lock != NULL);	/* 전달된 lock 포인터가 NULL이 아님을 확인, 즉 유효한 락 객체를 가리키고 있어야 함 */
	ASSERT (!lock_held_by_current_thread (lock));	/* 현재 쓰레드가 이미 락을 보유하고 있지 않음을 확인함 */

	success = sema_try_down (&lock->semaphore);	/* sema_try_down 함수를 호출하여 락의 세마포어에 대해 시도적 감소 연산을 수행함. 연산이 성공하면 락을 획득하고 success를 true로 설정, 실패하면 false 반환 */
	if (success)
		lock->holder = thread_current ();	/* lock 획득에 성공했다면, 현재 쓰레드를 락의 보유자(holder)로 설정함. */
	return success;	/* 락 획득 시도의 성공 여부를 나타냄 */
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
/* lock 해제 함수 */
void lock_release(struct lock *lock)
{
    ASSERT(lock != NULL);	/* 전달받은 lock 포인터가 NULL 인지 확인 */
    ASSERT(lock_held_by_current_thread(lock));	/* 현재 쓰레드가 실제로 lock을 소유하고 있는 지 확인 */

    remove_donor(lock);	/* lock을 기다리는 쓰레드들의 기부 목록에서 해당 락을 제거함 */
    update_priority_for_donations();	/* 우선순위 기부에 의해 변경된 현재 쓰레드의 우선 순위를 업데이트함. */

    lock->holder = NULL;	/* lock holder를 NULL로 설정하여 락이 더 이상 소유되지 않음 */
    sema_up(&lock->semaphore);	/* 세마포어의 값을 증가시켜, 대기 중인 다른 쓰레드가 lock을 획득할 수 있게 함. */
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
/* 현재 쓰레드가 특정 lock을 보유하고 있는지 여부를 확인 */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);	/* lock 포인터가 NULL이 아닌지 확인 (lock 포인터가 유효한 주소를 가리키고 있는지 검증) */

	return lock->holder == thread_current ();	/* lock->holder는 현재 lock을 보유하고 있는 쓰레드를 가리키고, thread_current 함수는 현재 실행 중인 쓰레드를 반환함. */
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
/* condition 변수를 초기화하는 함수 */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);	/* cond 포인터가 NULL이 아님을 확인 즉, 유효한 조건 변수 객체를 가리키고 있어야 함. */

	list_init (&cond->waiters);	/* list_init 함수를 호출하여 조건 변수의 waiters 리스트를 초기화함. (waiters 리스트는 이 조건 변수를 기다리고 있는 쓰레드들의 목록) */
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/* condition 변수를 사용하여 쓰레드를 lock을 해제하고 대기 상태로 만듦. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;	/* 구조체 타입의 로컬 변수 waiter를 선언함. waiter는 대기 중인 쓰레드를 나타내며, 내부적으로 세마포어를 포함함 */

	ASSERT (cond != NULL);	/* cond 포인터가 NULL이 아님을 확인 */
	ASSERT (lock != NULL);	/* lock 포인터가 NULL이 아님을 확인 */
	ASSERT (!intr_context ());	/* 현재 인터럽트 컨텍스트 내에서 실행되지 않음을 확인하는 단언문 */
	ASSERT (lock_held_by_current_thread (lock));	/* 현재 쓰레드가 lock을 보유하고 있음을 확인 */

	sema_init (&waiter.semaphore, 0); /* waiter 내부의 세마포어를 초기화 */
	// list_push_back (&cond->waiters, &waiter.elem);	/* waiter를 조건 변수 cond의 대기자 목록에 추가함. */
	list_insert_ordered(&cond->waiters, &waiter.elem, cmp_sema_priority, NULL);	/* 현재 쓰레드를 조건 변수의 waiter 목록에 우선순위에 따라 정렬된 순서로 삽입함 */
	lock_release (lock);	/* lock을 해제함 */
	sema_down (&waiter.semaphore);	/* waiter의 세마포어에 대해 sema_down을 호출하여 쓰레드를 대기 상태로 만듦. */
	lock_acquire (lock);	/* 대기 상태에서 꺠어난 후 다시 lock 획득 */
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
/* 조건 변수에 신호를 보내어 대기 중인 쓰레드들 중 하나를 깨우는 역할 */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);	/* cond 포인터가 NULL이 아님을 확인 */
	ASSERT (lock != NULL);	/* look 포인터가 NULL이 아님을 확인 */
	ASSERT (!intr_context ());	/* 현재 인터럽트 컨텍스트 내에서 실행되지 않음을 확인 */
	ASSERT (lock_held_by_current_thread (lock));	/* 현재 쓰레드가 lock을 보유하고 있음을 확인 */

	if (!list_empty (&cond->waiters))	/* 조건 변수의 대기자 목록인 waiter가 비어 있지 않음을 확인 (대기 중인 쓰레드가 있는 지 검사) */
		{
			list_sort(&cond->waiters, cmp_sema_priority, NULL);	/* 조건 변수의 waiters를 우선 순위에 따라 정렬함. 따라서 우선 순위가 가장 높은 쓰레드가 먼저 깨어나도록*/
			sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
			/* list_pop_front : 대기자 목록에서 첫 번째 요소를 제거하고 반환 */
			/* sema_up : 세마포어에 sema_up 연산을 수행하여, 해당 세마포어를 기다리고 있던 쓰레드를 깨움. */
		}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
/* cond 조건 변수에 대기 중인 모든 쓰레드에 신호를 보내어 대기 상태에서 깨움. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);	/* cond 포인터가 NULL이 아님을 확인 */
	ASSERT (lock != NULL);	/* lock 포인터가 NULL이 아님을 확인 */

	while (!list_empty (&cond->waiters))	/* cond-waiters 리스트가 비어 있지 않는 동안 계속 반복 */
		cond_signal (cond, lock);	/* 조건 변수에 신호를 보내어 대기 중인 쓰레드들 중 하나를 깨우는 역할 */
}

/* 두 세마포어 요소의 우선순위를 비교하는 함수로 세마포어의 waiters의 우선 순위에 따라 정렬하는 데 사용됨 */
bool
cmp_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct semaphore_elem *sema_a = list_entry(a, struct semaphore_elem, elem);	/* list_entry 매크로를 사용하여 리스트에서 struct semaphore_elem 객체를 추출함. */
	struct semaphore_elem *sema_b = list_entry(b, struct semaphore_elem, elem);

	struct list *waiters_a = &(sema_a -> semaphore.waiters);	/* semaphore 구조체 내의 waiters 리스트를 참조함. */
	struct list *waiters_b = &(sema_b -> semaphore.waiters);

	struct thread *root_a = list_entry(list_begin(waiters_a), struct thread, elem);	/* waiters_a 리스트의 첫 번째 요소(가장 높은 우선순위의 쓰레드)를 struct thread 타입으로 가져옴. */
	struct thread *root_b = list_entry(list_begin(waiters_b), struct thread, elem);

	return root_a->priority > root_b-> priority;	/* root_a와 root_b의 우선순위를 비교하여 true, false를 반환 */
}

/* 두 쓰레드의 우선순위를 비교하는 데 사용됨. */
bool cmp_donation_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
    struct thread *st_a = list_entry(a, struct thread, donation_elem);	/* list_elem 포인터 a를 포함하는 thread 구조체로 변환함. */
    struct thread *st_b = list_entry(b, struct thread, donation_elem);
    return st_a->priority > st_b->priority;	/* 쓰레드간의 우선순위 결과를 true와 false로 반환 */
}

/* 현재 쓰레드가 기다리고 있는 lock을 소유한 쓰레드(holder)에게 우선 순위를 상속하는 기능을 수행 */
void donate_priority(void)
{
    struct thread *curr = thread_current(); /* 현재 실행 중인 쓰레드의 주소를 curr 변수에 저장 */
    struct thread *holder;	/* holder 변수는 현재 쓰레드가 기다리고 있는 lock을 소유하고 있는 쓰레드를 가리킬 때 사용됨. */

    int priority = curr->priority;	/* 현재 쓰레드의 우선순위를 priority 변수에 저장함. */

    for (int i = 0; i < 8; i++)	/* 최대 8단계까지 우선순위 상속을 수행함 */
    {
        if (curr->wait_on_lock == NULL) /* 현재 쓰레드가 더 이상 기다리고 있는 락이 없는 경우 */
            return;	/* 함수 종료 */
        holder = curr->wait_on_lock->holder; /* 현재 쓰레드가 기다리고 있는 lock을 소유한 쓰레드를 holder 변수에 저장 */
        holder->priority = priority;	/* holder 쓰레드의 우선순위를 현재 쓰레드의 우선순위로 설정함 */
        curr = holder;	/* 다음 단계의 우선순위 상속을 위해 curr를 holder로 설정함 */
    }
}

/* lock을 해제하는 과정에서 해당 락을 기다리는 쓰레드들의 우선 순위 기부를 제거함. */
void remove_donor(struct lock *lock)
{
    struct list *donations = &(thread_current()->donations);	/* 현재 쓰레드의 donations 리스트의 주소를 donations 변수에 저장함. 이 리스트는 다른 쓰레드로부터 받은 우선순위 기부를 포함하고 있음. */
    struct list_elem *donor_elem;	/* donations 리스트의 요소를 순회하기 위한 포인터 */
    struct thread *donor_thread;	/* 우선순위 기부자 쓰레드를 나타내는 변수 */

    if (list_empty(donations))	/* 기부자 리스트가 비어있다면 */
        return;	/* 함수 종료 */

    donor_elem = list_front(donations);	/* donations 리스트의 첫번째 요소로 donor_elem을 설정함 */

    while (1)	/* 무한 루프 시작 */
    {
        donor_thread = list_entry(donor_elem, struct thread, donation_elem);	/* donor_elem에 해당하는 thread 구조체를 donor_thread에 저장함 */
        if (donor_thread->wait_on_lock == lock)	/* donor_thread가 해제될 lock을 기다리고 있다면 */
            list_remove(&donor_thread->donation_elem); /* donor_thread의 donation_elem을 제거 */
        donor_elem = list_next(donor_elem);	/* donor_elem을 다음 요소로 이동 */
        if (donor_elem == list_end(donations))	/* donor_elem이 리스트의 끝에 도달했다면 */
            return;	/* 종료 */
    }
}

/* 다른 쓰레드로부터 우선순위를 기부받아 현재 쓰레드의 우선 순위를 업데이트 */
// void
// update_priority_for_donations(void)
// {
//     struct thread *curr = thread_current();  /* curr 변수에 현재 쓰레드를 담음 */
//     struct list *donations = &(thread_current()->donations);   /* 현재 쓰레드의 donations 리스트를 donations 포인터에 담음 */
//     struct thread *donations_root;  /* 가장 높은 우선순위의 쓰레드를 가리키는 donations_root 변수 선언 */

//     if (list_empty(donations)) /* donations 리스트가 비어 있는지 확인*/
//     {
//         curr->priority = curr->init_priority;   /* donations 리스트가 비어 있다면, 현재 curr 쓰레드의 우선 순위를 초기 우선 순위로 설정함. */
//         return;
//     }

//     donations_root = list_entry(list_front(donations), struct thread, donation_elem);  /* donations 리스트의 맨 앞에 있는 요소를 list_entry 함수를 통해 struct thraed 타입으로 변환하여 donations_root에 할당함. */
//     curr->priority = donations_root->priority;  /* 현재 쓰레드(curr)의 우선 순위를 donations_root 쓰레드의 우선 순위로 설정함 */
// }

void
update_priority_for_donations(void)
{
    struct thread *curr = thread_current();  /* curr 변수에 현재 쓰레드를 담음 */
    struct list *donations = &(thread_current()->donations);   /* 현재 쓰레드의 donations 리스트를 donations 포인터에 담음 */
    struct thread *donations_root;  /* 가장 높은 우선순위의 쓰레드를 가리키는 donations_root 변수 선언 */
   
   curr->priority = curr->init_priority;   /* donations 리스트가 비어 있다면, 현재 curr 쓰레드의 우선 순위를 초기 우선 순위로 설정함. */
    if (!list_empty(donations)) /* donations 리스트가 비어 있는지 확인*/
    {
       
      donations_root = list_entry(list_front(donations), struct thread, donation_elem);  /* donations 리스트의 맨 앞에 있는 요소를 list_entry 함수를 통해 struct thraed 타입으로 변환하여 donations_root에 할당함. */
      if(curr->priority < donations_root->priority)
         curr->priority = donations_root->priority;  /* 현재 쓰레드(curr)의 우선 순위를 donations_root 쓰레드의 우선 순위로 설정함 */
    }
}