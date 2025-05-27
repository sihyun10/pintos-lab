#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "lib/stdio.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);


/* General process initializer for initd and other process. */
static void
process_init (void) {
	// 현재 실행중인 스레드를 가져온다.
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
// 첫 번째 사용자 프로그램(initd)을 실행하는 함수.
// 사용자 프로세스를 시작하는 가장 첫 단계
// process_execute()?
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	// 한 페이지만큼 메모리를 할당해서 프로그램 이름을 저장할 준비를 함.
	fn_copy = palloc_get_page (0);
	
	// 페이지 할당 실패
	if (fn_copy == NULL)
		return TID_ERROR;
	//파일이름을 안전하게 복사해서 fn_copy에 저장
	strlcpy (fn_copy, file_name, PGSIZE);
	
	char *save_ptr;
	strtok_r(file_name, " ", &save_ptr);
	/* Create a new thread to execute FILE_NAME. */
	// initd라는 함수를 시작점으로 하는 쓰레드를 만든다.
	
	struct child_status *ch_st = calloc(1, sizeof(struct child_status));
	void **args = calloc(2, sizeof(void *));
	args[0] = fn_copy;
	args[1] = ch_st;
	list_push_back(&thread_current()->child_list, &ch_st->elem);
	

	tid = thread_create (file_name, PRI_DEFAULT, initd, args);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	//printf("process create done.\n");
	//printf("sema val: %d\n", ch_st->sema_wait.value);
	// process_create_initd 할 때는 아직 main쓰레드임.
	// fork는 아니지만 예외적으로 유저프로그램을 실행하는 쓰레드를 main쓰레드의 자식으로 설정
	//list_init(&thread_current()->child_list);
	
	//ch_st->wait_called = true;
	

	
	return tid;
}

/* A thread function that launches first user process. */
// 첫 번째 사용자 프로세스를 실제로 실행시키는 함수
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif
	
	process_init ();
	// 사용자 프로그램을 메모리에 로드하고 실행한다.
	// 실패시 커널 패닉
	if (process_exec (&f_name[0]) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
// name이라는 이름을 가진 자식 프로세스(쓰레드)를 생성하고, 그 tid를 리턴한다.
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/
	printf("process_fork\n");

	tid_t tid = thread_create (name, PRI_DEFAULT, __do_fork, thread_current ());
	struct child_status *ch_st = calloc(1, sizeof(struct child_status));
	ch_st->tid = tid;
	ch_st->wait_called = false;
	sema_init(&ch_st->sema_fork, 0);
	list_push_back(&thread_current()->child_list, &ch_st->elem);
	// 자식이 sema up을 먼저하고 sema down해도 ok
	sema_down(&ch_st->sema_fork);
	
	return tid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
// 부모 프로세스의 페이지 테이블을 자식에게 복사하는 함수
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	// 부모 쓰레드의 페이지 테이블을 순회함
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */

	/* 2. Resolve VA from the parent's page map level 4. */
	// 해당 가상 주소와 연결된 물리주소의 커널 가상주소
	parent_page = pml4_get_page (parent->pml4, va);
	if(is_kernel_vaddr(parent_page)) return false;

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */

	// 유저풀에서 한페이지만큼 할당, 커널 가상 주소
	newpage = palloc_get_page(PAL_USER);

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		palloc_free_page(newpage);
		return false;
	}
	return true;
}
#endif



/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
// 자식 프로세스가 부모의 상태를 복사한 뒤 실행을 시작하는 함수
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	// fork를 호출한 쓰레드(부모 쓰레드)
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();



	current->isforked = true;

	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	// 부모 쓰레드가 전환 전에 저장했던 레지스터 정보를 받아옴
	struct intr_frame *parent_if = &parent->tf;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	// 자식 프로세스에 부모 프로세스 레지스터 정보를 복사함
	memcpy (&if_, parent_if, sizeof (struct intr_frame));

	/* 	자식 프로세스는 fork 리턴값으로 0
		callee saved register 설정
	*/
	if_.R.rax = 0;
	if_.R.rbx = parent_if->R.rbx;
	if_.rsp = parent_if->rsp;
	if_.R.rbp = parent_if->R.rbp;
	if_.R.r12 = parent_if->R.r12;
	if_.R.r13 = parent_if->R.r13;
	if_.R.r14 = parent_if->R.r14;
	if_.R.r15 = parent_if->R.r15;


	/* 2. Duplicate PT */
	// 새로운 페이지 테이블을 생성
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;
	
	// 부모 페이지 테이블을 자식 페이지 테이블에 복사
	pml4_for_each(parent_if, duplicate_pte, parent);

		
	// 페이지테이블과 필요한 세팅 
	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	// 파일 디스크립터 리스트 복사
	for(int fd=3; fd<FD_MAX; fd++){

		if(parent->fd_table->fd_entries[fd] == NULL) continue;
		struct file* copied_file = file_duplicate(parent->fd_table->fd_entries[fd]);
		current->fd_table->fd_entries[fd] = copied_file;
	}

	// 자식 프로세스가 준비를 마쳤다는것을 알리기
	struct list *child_list = &parent->child_list;
	struct child_status* ch_st = NULL;
	for(struct list_elem *e = list_begin(child_list); e != list_end(child_list); e = list_next(e)){
		if(list_entry(e, struct child_status, elem)->tid == current->tid){
			ch_st = list_entry(e, struct child_status, elem);
		}
	}

	if(!ch_st) goto error;
	current->child_status = ch_st;
	sema_up(&ch_st->sema_fork);


	process_init ();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	thread_exit ();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
// 현재 실행 중인 프로세스(쓰레드)의 실행 이미지를 새로운 유저 프로그램으로 바꾸는 함수
// start_process()
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;
	//printf("exec: %s\n", file_name);
	//printf("exec tid: %d\n", thread_current()->child_status->tid);
	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	

	/* We first kill the current context */
	// 현재 프로세스에서 사용하던 자원 제거
	process_cleanup ();

	/* And then load the binary */
	// 실행파일을 메모리에 적재
	success = load (file_name, &_if);

	//hex_dump(_if.rsp, _if.rsp, USER_STACK - (uint64_t)_if.rsp, true);
	
	/* If load failed, quit. */
	//실패시 종료
	palloc_free_page (file_name);
	if (!success)
		return -1;



	/* Start switched process. */
	// cpu제어권이 커널 -> 사용자 모드로 전환
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
// 자식 프로세스가 종료될 때 까지 기다리고, 자식이 반환한 종료 상태를 받아온다.
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	struct thread* curr = thread_current();
	struct child_status *ch_st = NULL;
	struct list *child_list = &curr->child_list;
	for(struct list_elem *e = list_begin(child_list); e != list_end(child_list); e = list_next(child_list)){
		struct child_status *tmp = list_entry(e, struct child_status, elem);
		if(tmp->tid == child_tid) {
			ch_st = tmp;
			break;
		}
	}
	//printf("process_wait\n");

	if(ch_st == NULL) return -1;
	if(ch_st->wait_called) return -1;

	ch_st->wait_called = true;
	//printf("wait tid: %d\n", ch_st->tid);
	//printf("wait sema val: %d\n", ch_st->sema_wait.value);
	// list_init(&ch_st->sema_wait);
	// exit할 때 가지 기다림
	sema_down(&ch_st->sema_wait);
	
	// exit 후
	if(!ch_st->has_exited)
		return -1;

	int exit_status = ch_st->exit_status;
	// 반환
	free(ch_st);

	return exit_status;

	// for(int i=0; i<2000000000; i++){}
	// //while(1){}
	// return -1;
}

/* Exit the process. This function is called by thread_exit (). */
// 현재 실행 중인 프로세스를 정상적으로 종료합니다.
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */

	process_cleanup ();
}

/* Free the current process's resources. */
// 프로세스가 종료될 때 그 프로세스가 사용하던 메모리 리소스를 해제
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
// 유저 프로세스가 실행되기 전에 CPU에 해당 프로세스에 맞는 설정을 적용하는 함수
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	// 해당 페이지 테이블을 cpu가 사용하는 페이지 테이블로 설정
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	// 커널 스텍 포인터를 설정
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
// e_ident 배열의 크기
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
// ELF 실행 파일의 맨 앞에 있는 헤더
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
/* 사용자 프로그램(ELF 실행파일)을 디스크에서 메모리로 적재하고, 
 시작주소(rip)와 초기 스택 포인터(rsp)를 설정한다.*/
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;
	
	// 파싱
	char *argv[128];
	char *argv_stack[128];
	char *token, *save_ptr;
	char s[128];
	strlcpy(s, (char *)file_name, sizeof(s));
	int argc = 0;
	for(token = strtok_r(s, " ", &save_ptr); token != NULL; token = strtok_r(NULL, " ", &save_ptr)){
		argv[argc++] = token;
	}
	//printf("load name: %s\n",argv[1]);

	/* Allocate and activate page directory. */
	// 현재 스레드에 대한 사용자 주소 공간을 생성한다
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	// 사용자 프로그램을 파일 시스템에서 연다
	file = filesys_open (argv[0]);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}
	
	/* Read and verify executable header. */
	// ELF파일의 첫 부분(header)을 읽어 구조체 ehdr에 저장후 검증
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	// program header table을 순회한다.
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			/*로딩이 필요한 세그먼트 처리*/
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					// 페이지를 할당하고 파일에서 내용을 읽거 메모리에 적재
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	// 스택 셋업
	if (!setup_stack (if_))
		goto done;
		/* Start address. */
		// 시작 주소 설정
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	
	// 파싱한 명령어들
	for(int i=argc-1; i>=0; i--){
		size_t len = strlen(argv[i]) + 1;
		if_->rsp -= len;
		memcpy((void *)if_->rsp, argv[i], len);
		argv_stack[i] = (char *)if_->rsp;
	}
	// padding
	// int padding = if_->rsp % 8;
	// if_->rsp -= padding;
	while(if_->rsp % 16 != 0){
		if_->rsp -= 1;
		memset((void*)if_->rsp, 0, sizeof(char));
	}
	//memset((void *)if_->rsp, 0, padding);
	// argv[argc]
	size_t len = sizeof (char *);
	if_->rsp -= len;
	memset((void *)if_->rsp, 0, len);
	

	//파싱한 문자열들 주소
	for(int i=argc-1; i>=0; i--){

		if_->rsp -= len;

		memcpy((void *)if_->rsp, &argv_stack[i], len);
	}
	// fake return addresss
	if_->rsp -= len; 
	memset((void *)if_->rsp, 0, len);
	if_->R.rsi = (char *)if_->rsp + 8;
	if_->R.rdi = argc;


	// 지성
	// size_t len = strlen(argv[0]) + 1;
	// if_->rsp -= len;
	// memcpy((void *)if_->rsp, argv[0], len);
	// argv_stack[0] = (char *)if_->rsp;
	// while(if_->rsp % 16 != 0){
	// 	if_->rsp -= 1;
	// 	memset((void*)if_->rsp, 0, sizeof(char));
	// }
	// len = sizeof (char *);
	// memcpy((void *)if_->rsp, &argv_stack[0], len);
	// //if_->R.rsi = if_->rsp;
	// if_->R.rdi = argc;

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	file_close (file);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
// ELF 실행 파일의 프로그램 헤더가 유효하고 메모리에 적재할 수 있는 세그먼트인지 검사
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
// 사용자 프로세스의 초기 스택을 설정
// 유저스택주소는 elf파일에 없고 os가 지정함
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;
	// 1페이지 크기만큼 유저 메모리풀에서 할당함, 할당된 페이지를 0으로 초기화
	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		// 준비한 페이지를 가상 주소 공간에서 스택 위치에 매핑합니다.
		// 커널에서 할당한 물리 페이지를 사용자 주소 공간의 스택 가장 위에 해당하는 가상주소와 매핑
		// uint8_t 포인터로 캐스팅한 이유는 바이트단위의 연산을 위함.
		// 스택은 거꾸로 자라기 때문에 스택포인터에서 - 페이지사이즈만큼 간 주소에 할당한 1페이지 물리주소 연결
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */
