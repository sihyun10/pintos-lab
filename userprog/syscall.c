#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/user/syscall.h"
#include "userprog/process.h"
#include "include/lib/string.h"
#include "threads/palloc.h"
#include "threads/synch.h"
// #include "filesys/inode.h"
// #include "threads/malloc.h"
// /* An open file. */
// struct file {
// 	struct inode *inode;        /* File's inode. */
// 	off_t pos;                  /* Current position. */
// 	bool deny_write;            /* Has file_deny_write() been called? */
// };

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void halt(void);
void exit(int status);
pid_t fork (const char *thread_name);
int exec (const char *file);
int wait (pid_t pid);
bool create(const char *file, unsigned initial_size);
bool remove (const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close (int fd);
bool isValidAddress(const void *ptr);
bool isValidString(const char *str);

struct lock filesys_lock;

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.

	// 시스템 콜 번호
	uint64_t syscall_num = f->R.rax;

	switch(syscall_num){
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			// f->R.rax = fork((char *)f->R.rdi);
			f->R.rax = process_fork((char *)f->R.rdi, f); 
			break;
		case SYS_EXEC:
			f->R.rax = exec((char *)f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = wait((pid_t)f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create((char *)f->R.rdi, (unsigned)f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove ((char *)f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = open((char *)f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize((int)f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read((int)f->R.rdi, (void *)f->R.rsi, (unsigned)f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write((int)f->R.rdi, (void *)f->R.rsi, (unsigned)f->R.rdx);
			break;
		case SYS_SEEK:
			seek((int)f->R.rdi, (unsigned)f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell((int)f->R.rdi);
			break;
		case SYS_CLOSE:
			close((int)f->R.rdi);
			break;
		default:
			thread_exit();
	}
	

}

void halt(void){
	power_off();
}

void exit(int status){
	struct thread* curr = thread_current();
	//부모프로세스에서 자식프로세스의 종료 정보를 저장
	if(curr->user_prog != NULL) {
		file_allow_write(curr->user_prog);
		file_close(curr->user_prog);
	}

	struct file **fd_entries = curr->fd_table->fd_entries;
	for(int i=0; i<FD_MAX; i++){
		if(fd_entries[i] == NULL) continue;
		file_close(fd_entries[i]);
	}
	free(curr->fd_table);

	struct list *child_list = &curr->child_list;
	while (!list_empty(child_list)) {
		struct list_elem *e = list_pop_front(child_list);
		struct child_status *ch = list_entry(e, struct child_status, elem);
		free(ch); // 부모가 종료되므로, 자식 상태 정보 해제
	}

	struct child_status *ch_st = curr->child_status;
	if(ch_st != NULL){

		//printf("exit tid: %d\n", ch_st->tid);
		ch_st->exit_status = status;
		ch_st->has_exited = true;
		printf("%s: exit(%d)\n", curr->name, status);
		sema_up(&ch_st->sema_wait);

		if(!list_empty(&ch_st->sema_fork.waiters)){
			sema_up(&ch_st->sema_fork);
		}
		if(!list_empty(&ch_st->sema_wait.waiters)){
			sema_up(&ch_st->sema_wait);
		}
	}
	else{
		
		printf("%s: exit(%d)\n", curr->name, status);
		
	}
	
	thread_exit();
}

pid_t fork (const char *thread_name){
	tid_t child_tid = process_fork(thread_name, &thread_current()->tf);
	
	return child_tid;
}

int exec(const char *file_name){
	//printf("exec file name: %s, addr: %p\n", file_name, file_name);
	if(!isValidString(file_name)) exit(-1);
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if(fn_copy == NULL) return -1;
	strlcpy(fn_copy, file_name, PGSIZE);
	//printf("fn_copy: %s\n", fn_copy);
	//printf("curr magic: 0x%x\n", thread_current()->magic);

	int tid = process_exec(fn_copy);
	//palloc_free_page(fn_copy);
	if(tid == -1) exit(-1);
	return tid;
}


int wait (pid_t pid){
	return process_wait(pid);
}


// all done. process wait 구현해야 완전 통과
bool create(const char *file, unsigned initial_size) {
	if(file == NULL) exit(-1);
	if(!isValidAddress(file)) exit(-1);
	bool success = filesys_create(file, initial_size);
	return success;
}

bool remove (const char *file){
	
	bool result = filesys_remove(file);
	return result;
}

// open done.
int open(const char *file){
	if(!isValidString(file)) exit(-1);
	if(file == NULL) return -1;
	struct file* f = filesys_open(file);
	

	//printf("f address: %p\n", f);
	if(f == NULL) return -1;
	

	struct thread* curr = thread_current();
	// 0, 1, 2는 예약된 fd
	for(int fd = 3; fd < FD_MAX; fd++){
		if(curr->fd_table->fd_entries[fd] == NULL){
			curr->fd_table->fd_entries[fd] = f;
			return fd;
		}
	}
	// fd table 꽉 찼을 때
	file_close(f);
	return -1;
}

int filesize(int fd){
	int size = 0;
	struct thread* curr = thread_current();
	struct file* f = curr->fd_table->fd_entries[fd];
	size = file_length(f);
	return size;
}

// read done. rox빼고
int read(int fd, void *buffer, unsigned size){
	//printf("read fd: %d\n", fd);
	if(size == 0) return 0;
	if(fd < 0) exit(-1);
	if(fd == 1) exit(-1);
	if(fd >= FD_MAX) exit(-1);
	check_valid_buffer(buffer, size);
	if(fd == 0){
		char c;
		int i=0;
		for(; i<size; i++){
			c = input_getc();
			((char *)buffer)[i] = c;
			if(c == '\n') break;
		}
		return i+1;
	}
	else if(fd >= 3){
		struct thread* curr = thread_current();
		struct file* f = curr->fd_table->fd_entries[fd];
		if(f == NULL) return -1;
		//printf("inode pointer: %p\n", f->inode);
		lock_acquire(&f->inode->inode_lock);
		//printf("buffer: %p, size: %u\n", buffer, size);
		int result = file_read(f, buffer, size);
		lock_release(&f->inode->inode_lock);
		//printf("file_read returned %d\n", result);
		return result;
	}
}

// write done
int write(int fd, const void *buffer, unsigned size){
	//printf("write\n");
	if(fd == 0) exit(-1);
	if(fd < 0) exit(-1);
	if(fd >= FD_MAX) exit(-1);
	
	// 표준 출력
	check_valid_buffer(buffer, size);
	if(fd == 1 || fd == 2){
		putbuf(buffer, (size_t)size);
		return (int)size;
	}
	else{
		struct thread* curr = thread_current();
		struct file* f = curr->fd_table->fd_entries[fd];
		if(f == NULL) exit(-1);
		return file_write(f, buffer, size);
	}
	return 0;
}

void seek(int fd, unsigned position){
	struct file *file = thread_current()->fd_table->fd_entries[fd];
	file_seek(file, (off_t)position);
}

unsigned tell(int fd){
	struct file* file = thread_current()->fd_table->fd_entries[fd];
	off_t offset = file_tell(file);
	return offset;
}

void close(int fd){
	if(fd < 0) exit(-1);
	if(fd == 0 || fd == 1 || fd == 2) exit(-1);
	if(fd >= FD_MAX) exit(-1);
	struct thread *curr = thread_current();
	struct file *file = curr->fd_table->fd_entries[fd];
	curr->fd_table->fd_entries[fd] = NULL;
	file_close(file);
}

bool isValidAddress(const void *ptr){
	if(is_user_vaddr(ptr)){
		if(pml4_get_page(thread_current()->pml4, ptr) != NULL)
			return true;
	}
	return false;
}

bool isValidString(const char *str){
	while(isValidAddress(str)){
		if(*str == '\0') return true;
		str++;
	}
	return false;
}

void check_valid_buffer(const void *buffer, unsigned size) {
    uint8_t *ptr = (uint8_t *)buffer;
    struct thread *curr = thread_current();

    for (unsigned i = 0; i < size; i++) {
        // 유저 주소 범위 체크
        if (!is_user_vaddr(ptr + i))
            exit(-1);

        // 페이지 매핑 존재 여부 체크
        if (pml4_get_page(curr->pml4, ptr + i) == NULL)
            exit(-1);
    }
}