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

void
sema_init(struct semaphore* sema, unsigned value) {
	ASSERT(sema != NULL);

	sema->value = value;
	list_init(&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
	 to become positive and then atomically decrements it.

	 This function may sleep, so it must not be called within an
	 interrupt handler.  This function may be called with
	 interrupts disabled, but if it sleeps then the next scheduled
	 thread will probably turn interrupts back on. This is
	 sema_down function. */
void
sema_down(struct semaphore* sema) {
	enum intr_level old_level;

	ASSERT(sema != NULL);
	ASSERT(!intr_context());

	old_level = intr_disable();
	while (sema->value == 0) {
		list_insert_ordered(&sema->waiters, &thread_current()->elem, thread_compare_priority, NULL);
		thread_block();
	}

	sema->value--;
	intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
	 semaphore is not already 0.  Returns true if the semaphore is
	 decremented, false otherwise.

	 This function may be called from an interrupt handler. */
bool
sema_try_down(struct semaphore* sema) {
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
	 and wakes up one thread of those waiting for SEMA, if any.

	 This function may be called from an interrupt handler. */
void
sema_up(struct semaphore* sema) {
	enum intr_level old_level;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (!list_empty(&sema->waiters)) {
		list_sort(&sema->waiters, thread_compare_priority, NULL);
		thread_unblock(list_entry(list_pop_front(&sema->waiters),
			struct thread, elem));
	}
	sema->value++;

	thread_test_preemption();
	intr_set_level(old_level);
}

static void sema_test_helper(void* sema_);

/* Self-test for semaphores that makes control "ping-pong"
	 between a pair of threads.  Insert calls to printf() to see
	 what's going on. */
void
sema_self_test(void) {
	struct semaphore sema[2];
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void* sema_) {
	struct semaphore* sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
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
void
lock_init(struct lock* lock) {
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
}

bool thread_compare_donate_priority(const struct list_elem* a, const struct list_elem* b, void* aux UNUSED)
{
	struct thread* data_a = list_entry(a, struct thread, donation_elem);
	struct thread* data_b = list_entry(b, struct thread, donation_elem);
	return data_a->priority > data_b->priority;
}

void donate_priority(void) {
	// 'sema - waiters' 대기열에 들어가기 전에 우선순위 donation
	struct thread* curr = thread_current();

	// holder에게 우선순위 부여(중첩 고려)
	while (curr->wait_on_lock != NULL) {
		struct thread* holder = curr->wait_on_lock->holder;

		if (curr->priority > holder->priority) {
			holder->priority = curr->priority;
			curr = holder;
		}
		else {
			break;
		}
	}
}
/* Acquires LOCK, sleeping until it becomes available if
	 necessary.  The lock must not already be held by the current
	 thread.

	 This function may sleep, so it must not be called within an
	 interrupt handler.  This function may be called with
	 interrupts disabled, but interrupts will be turned back on if
	 we need to sleep. */
void
lock_acquire(struct lock* lock) {
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));

	struct thread* curr = thread_current();

	/* lock을 가진 스레드가 있다면 우선순위 기부 후, 대기 */
	if (lock->holder) {
		// holder의 donations에 현재 스레드 추가
		list_insert_ordered(&lock->holder->donations, &curr->donation_elem, thread_compare_donate_priority, NULL);

		// 현재 스레드가 실제로 기다리는 lock 추가
		curr->wait_on_lock = lock;

		// 현재 스레드의 우선순위보다 낮은 중첩 스레드에게 우선순위 기부
		donate_priority();
	}
	sema_down(&lock->semaphore);

	/* lock을 가질 순서가 되면, lock을 가진다. */
	curr->wait_on_lock = NULL;
	lock->holder = curr;
}

/* Tries to acquires LOCK and returns true if successful or false
	 on failure.  The lock must not already be held by the current
	 thread.

	 This function will not sleep, so it may be called within an
	 interrupt handler. */
bool
lock_try_acquire(struct lock* lock) {
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock));

	success = sema_try_down(&lock->semaphore);
	if (success)
		lock->holder = thread_current();
	return success;
}

void remove_with_lock(struct lock* lock) {
	// lock을 release 하는 시점에 해당 lock으로 나를 기다렸던 스레드의 우선순위 반환
	// (각자 다른 lock으로 나를 기다릴 수 있기 때문에(다중), 해당 lock을 가진 thread만 삭제)

	struct thread* curr = thread_current();
	struct list_elem* d_elem = list_begin(&curr->donations);

	while (d_elem != list_end(&curr->donations)) {
		struct thread* t = list_entry(d_elem, struct thread, donation_elem);
		if (lock == t->wait_on_lock) {
			list_remove(&t->donation_elem); // 이제 관련 없는 스레드는 지워줌
		}
		d_elem = list_next(d_elem);
	}
}

void refresh_priority(void) {
	// 내가 가진 donations 중에 가장 높은 우선순위를 기부받음
	struct thread* curr = thread_current();
	curr->priority = curr->original_priority;

	if (list_empty(&curr->donations)) {
		return;
	}

	list_sort(&curr->donations, thread_compare_donate_priority, NULL);

	struct thread* max_thread = list_entry(list_front(&curr->donations), struct thread, donation_elem);
	if (max_thread->priority > curr->priority) {
		curr->priority = max_thread->priority;
	}
}

/* Releases LOCK, which must be owned by the current thread.
	 This is lock_release function.

	 An interrupt handler cannot acquire a lock, so it does not
	 make sense to try to release a lock within an interrupt
	 handler. */
void
lock_release(struct lock* lock) {
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));

	/* 해당 lock으로 기부받은 우선순위 삭제(중첩 처리) */
	remove_with_lock(lock);

	/* 현재 스레드 우선순위 재계산 */
	refresh_priority();

	/* lock을 해제함 */
	lock->holder = NULL;
	sema_up(&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
	 otherwise.  (Note that testing whether some other thread holds
	 a lock would be racy.) */
bool
lock_held_by_current_thread(const struct lock* lock) {
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
	int priority;
};

/* Initializes condition variable COND.  A condition variable
	 allows one piece of code to signal a condition and cooperating
	 code to receive the signal and act upon it. */
void
cond_init(struct condition* cond) {
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
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
bool sema_compare_priority(const struct list_elem* a, const struct list_elem* b,
	void* aux UNUSED) {
	struct semaphore_elem* sema_a = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem* sema_b = list_entry(b, struct semaphore_elem, elem);

	return sema_a->priority > sema_b->priority;
}

void
cond_wait(struct condition* cond, struct lock* lock) {
	struct semaphore_elem waiter;
	waiter.priority = thread_current()->priority;

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	sema_init(&waiter.semaphore, 0);
	list_push_back(&cond->waiters, &waiter.elem);
	// list_insert_ordered(&cond->waiters, &waiter.elem, sema_compare_priority, NULL);

	lock_release(lock);
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
	 this function signals one of them to wake up from its wait.
	 LOCK must be held before calling this function.

	 An interrupt handler cannot acquire a lock, so it does not
	 make sense to try to signal a condition variable within an
	 interrupt handler. */

	 /* COND(LOCK으로 보호됨)를 대기 중인 스레드가 있는 경우,
	 이 함수는 해당 스레드 중 하나에 대기에서 깨어나도록 신호를 보냅니다.
	 이 함수를 호출하기 전에 LOCK을 유지해야 합니다.

	 인터럽트 핸들러는 잠금을 획득할 수 없으므로
	 인터럽트 핸들러 내에서 조건 변수에 신호를 보내는 것은 의미가 없습니다. */

void
cond_signal(struct condition* cond, struct lock* lock UNUSED) {
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	if (!list_empty(&cond->waiters))
	{
		list_sort(&cond->waiters, sema_compare_priority, NULL);

		sema_up(&list_entry(list_pop_front(&cond->waiters),
			struct semaphore_elem, elem)->semaphore);
	}
}
/* Wakes up all threads, if any, waiting on COND (protected by
	 LOCK).  LOCK must be held before calling this function.

	 An interrupt handler cannot acquire a lock, so it does not
	 make sense to try to signal a condition variable within an
	 interrupt handler. */
void
cond_broadcast(struct condition* cond, struct lock* lock) {
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}
