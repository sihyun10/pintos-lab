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

void sync_preempt(void);
bool sync_priority_desc(const struct list_elem *a, const struct list_elem *b, void *aux);

/*
	struct semaphore {
		unsigned value;             //Current value. 
		struct list waiters;        //List of waiting threads. 
	};
*/


/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
// 세마포어 값을 초기화하는 함수
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);
	// 세마포 구조체에 값을 설정
	sema->value = value;
	// 세마포 구조체에 있는 waiters 리스트를 초기화
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	// sema의 value가 0이면 자원이 없다는 뜻
	// 자원이 없을때 sema_down하면 대기하게 됨
	while (sema->value == 0) {
		// 세마포 구조체에서 대기 쓰레드 목록에 호출한 쓰레드 추가
		//list_push_back (&sema->waiters, &thread_current ()->elem);
		list_insert_ordered(&sema->waiters, &thread_current()->elem, sync_priority_desc, NULL);
		// 호출한 쓰레드 블록, 이 시점에서 호출한 쓰레드는 잠자게 되어
		// 다음으로 넘어가지 않음
		thread_block ();
	}
	// sema up이 호출되면 양수로 바뀌어서 -1 가능.
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
// 현재 자원을 쓸 수 있으면 true를 반환하고 사용
// 못한다면 false를 반환, 기다리지 않는다.
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
// 세마포의 값을 1 증가시키면서 세마포에 의해 대기중인 쓰레드를 unblock
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	// 대기하고있는 쓰레드가 없다면 넘어감
	if (!list_empty (&sema->waiters)){
		// 대기하고있는 쓰레드 unblock해줌
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	}
	// 값 +1
	sema->value++;
	// sema값이 양수가 되기 전에 양보돼서 down되던 쓰레드가 깨어나는데, 그 쓰레드에서 세마값이
	// 아직 양수가 안돼서 또다시 블록되고, 다시 세마업쓰레드로 오면서 값이 올라가지만, 이미
	// sema down됐던 쓰레드는 다시 레디리스트에 올라갈 수 없게 된다.
	thread_preempt();
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
// 세마포가 잘 작동하는지 확인하는 코드
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
// 락을 초기화
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);
	// 락을 가지고 있는 쓰레드
	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
// 락을 얻는 함수
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	// 인터럽트 핸들러 안에서 호출되면 잠들 수 있으므로 안된다.
	ASSERT (!intr_context ());
	// 이미 락을 호출한 쓰레드가 가지고 있으면 안됨
	ASSERT (!lock_held_by_current_thread (lock));

	// 락 얻음, 혹은 대기
	sema_down (&lock->semaphore);
	// 얻게 되면 락의 소유자를 기록
	lock->holder = thread_current ();
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
// 락 반환
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	// 락의 소유자를 NULL로
	lock->holder = NULL;
	// 락을 반환
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
// 락을 호출한 쓰레드가 락을 가지고 있는지
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	// elem은 thread구조체를 저장하기 위함
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
	int priority;
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
// condition 구조체를 초기화한다.
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
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
void
cond_wait (struct condition *cond, struct lock *lock) {
	// 세마포를 저장할 구조체, 리스트에 넣기 위해 포장
	struct semaphore_elem waiter;
	waiter.priority = thread_current()->priority;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	// 세마포를 0으로 초기화
	sema_init (&waiter.semaphore, 0);
	// 조건 변수 대기자 명단에 추가
	// 쓰레드 마다 각자의 세마포가 있다.
	//list_push_back (&cond->waiters, &waiter.elem);
	list_insert_ordered(&cond->waiters, &waiter.elem, sync_priority_desc, NULL);
	// 락 반환
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters))

		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
// 모든 대기자를 깨운다.
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

bool sync_priority_desc(const struct list_elem *a, const struct list_elem *b, void *aux){
	// a: 추가할 쓰레드, b: 리스트안의 쓰레드
	struct semaphore_elem *e1 = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *e2 = list_entry(b, struct semaphore_elem, elem);

	if(e1->priority > e2->priority) return true;
	else return false;
}


bool cond_priority_desc(const struct list_elem *a, const struct list_elem *b, void *aux){
	// a: 추가할 쓰레드, b: 리스트안의 쓰레드
	struct thread *t1 = list_entry(a, struct thread, elem);
	struct thread *t2 = list_entry(b, struct thread, elem);

	if(t1->priority > t2->priority) return true;
	else return false;
}



