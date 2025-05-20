#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/init.h" // power_off
#include "console.h" // putbuf

void syscall_entry(void);
void syscall_handler(struct intr_frame*);
bool is_valid_user_ptr(void*);
bool is_valid_buffer(void* buffer, size_t size);
/* system calls */
void halt(void);
void exit(int status);
int read(int fd, void* buffer, unsigned size);
int write(int fd, const void* buffer, unsigned size);

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
		exit((int)f->R.rdi);
		break;
	case SYS_READ:
		if (is_valid_user_ptr((void*)f->R.rsi))
			f->R.rax = read((int)f->R.rdi, (void*)f->R.rsi, (unsigned)f->R.rdx);
		break;
	case SYS_WRITE:
		if (is_valid_user_ptr((void*)f->R.rsi))
			f->R.rax = write((int)f->R.rdi, (void*)f->R.rsi, (unsigned)f->R.rdx);
		break;

	default:
		break;
	}
}

bool is_valid_user_ptr(void* addr) {
	if (!is_user_vaddr(addr) || pml4_get_page(thread_current()->pml4, addr) == NULL)
		exit(-1);
	else
		return true;
}

bool is_valid_buffer(void* buffer, size_t size) {
	for (size_t i = 0;i < size;i++) {
		if (!is_valid_user_ptr((void*)((size_t)buffer + i)))
			return false;
	}
	return true;
}

void halt(void) {
	power_off();
}

void exit(int status) {
	struct thread* curr = thread_current();

	// thread 이름 출력
	printf("%s: exit(%d)\n", curr->name, status);
	curr->status = status;
	thread_exit();
}

int read(int fd, void* buffer, unsigned size) {

}

int write(int fd, const void* buffer, unsigned size) {
	if (fd == 1) {
		putbuf(buffer, size);
	}
}
// void read()