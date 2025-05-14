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
static struct list ready_list;

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

void thread_preempt(void);
bool thread_priority_desc(const struct list_elem *a, const struct list_elem *b, void *aux);
/* Returns true if T appears to point to a valid thread. */
// t가 유효한 스레드를 가지고 있는가?
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
// 현재 실행중인 스레드를 반환
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


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

// 운영체제 실행시 한 번 실행되는 쓰레드 시스템 초기화 함수
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	// 부팅 초기에는 가짜 쓰레드가 있다고 한다. 이 쓰레드를 진짜쓰레드로 바꾼다.
	/* Init the globla thread context */
	// 스레드 id 할당을 위한 lock
	lock_init (&tid_lock);
	// 실행 대기중인 스레드를 관리할 리스트 초기화
	list_init (&ready_list);
	// 스레드 종료 처리 대기 리스트 초기화
	list_init (&destruction_req);
	

	/* Set up a thread structure for the running thread. */
	// 현재 실행중인 스레드 넣기
	initial_thread = running_thread ();
	// 현재 스레드를 "main"이라는 이름과 기본 우선순위로 초기화
	init_thread (initial_thread, "main", PRI_DEFAULT);
	// 현재 스레드가 실행중이라고 설정
	initial_thread->status = THREAD_RUNNING;
	// 현재 스레드에 고유한 스레드id 할당
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
// 스레드 스케줄링을 처음으로 활성화하는 함수
void
thread_start (void) {
	//printf("thread start cur: %s\n", thread_current()->name);
	/* Create the idle thread. */
	// idle 스레드가 준비되었는지 확인하는 세마포
	struct semaphore idle_started;
	// 처음에 0으로 초기화
	sema_init (&idle_started, 0);
	// idle 스레드 생성 (idle함수는 시스템이 놀 때 실행되는 백업 스레드)
	thread_create ("idle", PRI_MIN, idle, &idle_started);
	// 인터럽트 허용: 문맥전환이 가능해짐
	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	// idle 쓰레드가 초기화될 때까지 대기함
	// 아마도 타이머인터럽트(점유시간 초과)로 idle쓰레드가 실행되기 전에 sema down이 먼저 도착하게 되는듯하다.
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
// 운영체제가 일정 시간마다 타이머 인터럽트를 발생시켜 이 함수를 호출한다. 
// 운영체제가 시간 흐름을 감지한다.
void
thread_tick (void) {
	//printf("thread tick\n");
	// 현재 실행 중인 스레드를 가져옴
	struct thread *t = thread_current ();

	// 현재 스레드가 idle 스레드라면 cpu유휴시간++
	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	// 사용자 프로그램이라면 user_ticks++
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
	// 커널 코드라면 kernel_ticks 증가
		kernel_ticks++;

	/* Enforce preemption. */
	// 한 스레드가 CPU를 점유할 수 있는 시간 제한을 넘기면 
	// intr_yield_on_return()으로 CPU 양보를 예약
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
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
// 새로운 커널 스레드를 만든다.
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;
	
	// 실행할 함수가 null이 아닌지 판단
	ASSERT (function != NULL);
	
	// 스레드를 위한 메모리(1페이지)를 확보.
	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	// 이름과 우선순위를 설정하고, 고유한 스레드 ID를 부여한다.
	// init_thread함수에서는 쓰레드 상태를 BLOCKED로 설정한다.
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread; // pc레지스터: 다음에 실행할 명령어의 주소
	t->tf.R.rdi = (uint64_t) function; // 첫 번째 인자는 rdi
	t->tf.R.rsi = (uint64_t) aux; // 두 번째 인자는 rsi, 즉 kernel_thread(function, aux) 처럼 인자를 넘긴다.
	t->tf.ds = SEL_KDSEG; // 데이터 세그먼트
	t->tf.es = SEL_KDSEG; // 추가 세그먼트
	t->tf.ss = SEL_KDSEG; // 스택 세그먼트
	t->tf.cs = SEL_KCSEG; // 코드 세그먼트
	t->tf.eflags = FLAG_IF; // CPU의 상태를 담는 레지스터, FALG_IF: 인터럽트를 허용
	
	// 새 스레드를 ready 큐에 넣는다. 스케줄러가 이 스레드를 선택할 수 있게 된다.
	/* Add to run queue. */
	thread_unblock (t);
	thread_preempt();
	// 생성한 스레드의 tid를 반환
	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
// 현재 실행 중인 스레드를 잠자게(sleep) 만드는 함수.
// thread_unblock()을 호출해야만 깨어날 수 있다.
void
thread_block (void) {
	ASSERT (!intr_context ());
	// 인터럽트가 꺼진 상태에서만 호출되어야 한다
	ASSERT (intr_get_level () == INTR_OFF);
	// 현재 실행중인 쓰레드의 상태를 THREAD_BLOCKED로 설정한다.
	// 이렇게 설정하면 스케줄을 방지할 수 있다.
	thread_current ()->status = THREAD_BLOCKED;
	// 이제 다른 스레드를 선택해서 CPU에 올리는 작업
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */

// block중인 쓰레드를 unblock하는 함수.
void
thread_unblock (struct thread *t) {

	enum intr_level old_level;

	// 유효한 스레드인가?
	ASSERT (is_thread (t));

	// 인터럽트를 끈다. 그리고 끄기 전 인터럽트 상태를 저장한다.
	old_level = intr_disable ();
	// 쓰레드가 THREAD_BLOCKED상태여야만 unblock가능하다.
	ASSERT (t->status == THREAD_BLOCKED);
	
	// // 밑의 두 줄 순서가 중요한가?
	// // 쓰레드를 대기중엔 리스트에 추가한다.
	// list_push_back (&ready_list, &t->elem);
	// 우선순위 스케줄링
	list_insert_ordered(&ready_list, &t->elem, thread_priority_desc, NULL);
	// 해당 쓰레드의 상태를 THREAD_READY로 변경한다.
	t->status = THREAD_READY;
	// 인터럽트를 복구시킨다.
	// 현재 쓰레드의 우선순위보다 더 높은 우선순위일 경우 현재 스레드 양보
	// 현재 쓰래드가 idle_thread일때도 yield해버리면 idle쓰레드가 ready_list에 
	// 들어가버린다.
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
// 현재 실행중인 쓰레드를 리턴한다.
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
// 실행중인 쓰레드의 tid를 리턴.
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
// 현재 쓰레드를 종료시킨다.
void
thread_exit (void) {
	// 인터럽트 콘텍스트가 아닌지 체크, 스레드의 상태를 변경시키는 것은 
	// 중요하기 때문에 동기화 문제가 발생하지 않아야 한다.
	// 인터럽트 컨텍스트는 하드웨어 인터럽트가 발생하여 현재 실행 중인 프로세스를 일시적으로 중단하고, 
	// 시스템이 인터럽트를 처리하는 동안의 특별한 상태입니다.
	ASSERT (!intr_context ());
// 사용자 프로그램이라면
#ifdef USERPROG
	// 프로세스를 종료한다.
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	// 쓰레드의 상태를 변경하고, 스케줄링을 한다.
	// 인터럽트를 끈다.
	intr_disable ();
	// 현재 쓰레드의 상태를 THREAD_DYING으로 설정한다.
	// 다음 실행할 쓰레드를 실행하는 등의 기능.
	do_schedule (THREAD_DYING);
	// 이 코드까지는 절대 도달하지 않는다는 뜻.
	// do_schedule()을 호출한 뒤에는 현재 스레드가 종료되므로
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	// 현재 실행중인 스레드를 가져온다.
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	
	// 인터럽트는 보통 중요한 작업을 처리한다.
	// 중요한 작업이 서로 겹치치 않도록 한다.
	ASSERT (!intr_context ());
	
	// 인터럽트를 끈다.
	old_level = intr_disable ();
	// 현재 쓰레드가 idle가 아니면, 다시 ready상태로 만들어주기 위해 ready_list에 추가
	if (curr != idle_thread)
		list_insert_ordered(&ready_list, &curr->elem, thread_priority_desc, NULL);
	// 스케줄러를 호출해 다른 쓰레드에게 CPU를 넘김
	do_schedule (THREAD_READY);
	// 이전 인터럽트 상태로 복구
	intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority;
	thread_preempt();
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
// 아무 할 일이 없을 때 CPU가 실행하는 대기 전용 스레드, ready_list가 비어있을 때
// 이 함수는 idle쓰레드가 하는 일을 정의한 함수이다.
// 이 함수가 부팅 후 자동 실행되고, thread block을 계속 실행시킴으로써
// block함수 내에서 schedule함수를 실행하게 되고, ready list에 쓰레드가 등록될 때 까지
// 기다린다.
static void
idle (void *idle_started_ UNUSED) {
	// idle함수의 인자로 세마포 포인터가 들어온다.
	// 해당 포인터를 명시적으로 캐스팅한다.
	struct semaphore *idle_started = idle_started_;

	// 현재 실행중인 스레드를 idle스레드에 등록
	idle_thread = thread_current ();
	// sema_down()으로 기다리고 있는 다른 스레드(보통 thread_start() 함수 내부)가 
	//다시 실행될 수 있도록 세마포어를 signal하는 부분
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		// 현재 쓰레드를 스케줄러에서 제외(잠시 멈춤)
		// block함수에서 ready_list가 비어있다면 아무것도 하지않고 다시 돌아오게된다.
		// 비어있지 않다면
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

		// sti: 인터럽트를 다시 킨다.
		// hlt: cpu를 일시 정지
		// 타이머 등등의 인터럽트 (대부분의 인터럽트)를 받으면 깸
		// 깨서 for문이 돌기때문에 무한루프가 필요함
		// ready list가 채워졌는지 계속 확인하기 위함.
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	// 다시 인터럽트를 킨다.
	intr_enable ();       /* The scheduler runs with interrupts off. */
	// 실제 스레드가 수행하는 부분
	function (aux);       /* Execute the thread function. */
	// function이 끝까지 실행됐다면, 그 쓰레드는 종료시킨다.
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	// 해당 속성들이 유효한지 검사
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	// t구조체의 내용을 전부 0으로 초기화한다.
	memset (t, 0, sizeof *t);
	// 처음 생성된 스레드는 실행 준비 상태가 아니므로 THREAD_BLOCKED로 설정
	t->status = THREAD_BLOCKED;
	// 스레드 이름을 구조체에 복사
	strlcpy (t->name, name, sizeof t->name);
	// 스택 포인터를 설정, 우선순위 설정, 디버깅용 magic 설정
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */

// 실행할 준비가 된 스레드들 중 가장 앞에 있는 쓰레드를 꺼내서 반환
// 만약 없다면 idle 쓰레드 반환
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
// 쓰레드 복귀 및 전환
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

// 스레드 전환 함수
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
// 현재 실행 중인 스레드의 상태를 변경하고, 새로운 쓰레드를 찾아서 실행한다.
static void
do_schedule(int status) {
	// 실행중인 쓰레드이면서, 인터럽트가 꺼져있는 상태를 보장
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	// 소멸 대기 중인 쓰레드 목록에서 스레드를 하나씩 꺼내서 
	// 메모리 할당을 해제한다(한꺼번에 종료된 쓰레드들을 정리하는 것이 안전하다.)
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	// status 상태로 해당 쓰레드의 상태를 바꾼다.
	thread_current ()->status = status;
	// 새로운 쓰레드를 선택하고 전환한다.
	schedule ();
}

// 다음 실행할 쓰레드를 선택하고 실행한다.
static void
schedule (void) {

	// 현재 실행중인 쓰레드
	struct thread *curr = running_thread ();
	// 다음에 실행될 쓰레드
	struct thread *next = next_thread_to_run ();

	// 인터럽트는 꺼져있으면서, 현재 스레드는 실행중이 아니여야 함
	// 다음 스레드가 유효한 쓰레드인지 확인
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	// 다음 실행할 쓰레드의 상태를 변경
	next->status = THREAD_RUNNING;
	/* Start new time slice. */
	// 새로운 타임슬라이스 시작
	thread_ticks = 0;

// 사용자 프로세스라면
#ifdef USERPROG
	
	/* Activate the new address space. */
	process_activate (next);
#endif
	// curr == next == idle_thread 라면, 아무것도 하지 않고 종료하기 위해서 필요(?)

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		   // curr쓰레드가 죽은 상태일 경우 그 쓰레드를 소멸 대기 목록에 추가한다.
		   // 이 작업은 쓰레드가 종료된 이후에 메모리를 해제할 수 있도록 한다. (do schedule?)
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		// 쓰레드 전환
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
// 새로운 쓰레드를 생성할 때, 고유한 tid를 생성하여 반환
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;
	// 경쟁상태 방지
	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

bool thread_priority_desc(const struct list_elem *a, const struct list_elem *b, void *aux){
	// a: 추가할 쓰레드, b: 리스트안의 쓰레드
	struct thread *t1 = list_entry(a, struct thread, elem);
	struct thread *t2 = list_entry(b, struct thread, elem);

	if(t1->priority > t2->priority) return true;
	else return false;
}


void thread_preempt(){
	struct thread *curr = thread_current();
	//if(curr == idle_thread) return;
	if(list_empty(&ready_list)) return;
	struct thread *t = list_entry(list_front(&ready_list), struct thread, elem);
	if(curr->priority < t->priority){
		thread_yield();
	}
}


struct thread *get_highest_priority_ready_thread(){
	struct list_elem *e = list_pop_front(&ready_list);
	return list_entry(e, struct thread, elem);
}

bool get_is_readylist_empty(){
	return list_empty(&ready_list);
}

