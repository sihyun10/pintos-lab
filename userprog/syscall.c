#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/malloc.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/init.h" // power_off
#include "console.h" // putbuf
#include "filesys/filesys.h" // filesys_create
#include "filesys/file.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame*);
bool is_valid_user_ptr(void*);
bool is_valid_buffer(void* buffer, size_t size);
/* system calls */
void halt(void);
void exit(int status);
bool create(const char* file, unsigned initial_size);
bool remove(const char* file);
int open(const char* file);
void close(int fd);
int filesize(int fd);
int read(int fd, void* buffer, unsigned size);
int write(int fd, const void* buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);

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
syscall_init(void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
		((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
		FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler(struct intr_frame* f UNUSED) {
	// TODO: Your implementation goes here.
	/* %rdi, %rsi, %rdx, %r10, %r8, and %r9 */
	int syscall_number = (int)f->R.rax;

	switch (syscall_number)
	{
		/* Projects 2 and later. */
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;

	default:
		exit(-1);
		break;
	}
}

void check_address(void* addr) {
	if (addr == NULL || !is_user_vaddr(addr) || pml4_get_page(thread_current()->pml4, addr) == NULL)
	{
		exit(-1);
	}
}

/* ---------- system calls ---------- */
void halt(void) {
	power_off();
}

/* Terminates the current user program, returning status to the kernel. */
void exit(int status) {
	struct thread* curr = thread_current();
	curr->exit_status = status; // 부모 프로세스가 알 수 있도록

	/* fdt 정리 */
	for (int i = 0; i < 64;i++) {
		curr->fd_table[i] = NULL;
	}
	free(curr->fd_table);

	// thread 이름 출력
	printf("%s: exit(%d)\n", curr->name, status);
	thread_exit();
}

bool create(const char* file, unsigned initial_size) {
	check_address(file);
	return filesys_create(file, initial_size);
}

bool remove(const char* file) {
	check_address(file);
	return filesys_remove(file);
}

int open(const char* file) {
	/* open 성공시, fd를 반환하고 실패시, -1을 반환한다. */
	check_address(file);

	struct file* f = filesys_open(file);
	if (f == NULL) {
		return -1;
	}

	struct file** fdt = thread_current()->fd_table;
	for (int fd = 2;fd < 64;fd++) {
		if (fdt[fd] == NULL || fdt[fd] == 0) {
			fdt[fd] = f;
			return fd;
		}
	}
	return -1; // fdt 전부 할당됨
}

void close(int fd) {
	/* fd를 0으로 바꿔준다. */
	if (fd >= 2 && fd < 64) {
		thread_current()->fd_table[fd] = 0;
	}
}

int filesize(int fd) {
	return file_length(thread_current()->fd_table[fd]);
}

int read(int fd, void* buffer, unsigned size) {
	check_address(buffer);

	// 표준 입력의 경우, 키보드의 입력을 받음
	if (fd == 0) {
		buffer = input_getc();
		return size;
	}
	else {
		// 옳은 fd인지 확인
		if (fd < 2 || fd >= 64) {
			return -1;
		}

		// 접근한 file이 비어있는지 확인
		struct file* read_file = thread_current()->fd_table[fd];
		if (read_file == NULL) {
			return -1;
		}

		return file_read(read_file, buffer, size);
	}
}

int write(int fd, const void* buffer, unsigned size) {
	check_address(buffer);

	if (fd == 1) {
		putbuf(buffer, size);
		return size;
	}
	else {
		// 옳은 fd인지 확인
		if (fd < 2 || fd >= 64) {
			return -1;
		}

		// 접근한 file이 비어있는지 확인
		struct file* write_file = thread_current()->fd_table[fd];
		if (write_file == NULL) {
			return -1;
		}

		return file_write(write_file, buffer, size);
	}
}

void seek(int fd, unsigned position) {
	file_seek(thread_current()->fd_table[fd], position);
}

unsigned tell(int fd) {
	return file_tell(thread_current()->fd_table[fd]);
}