#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/init.h"
#include "filesys/filesys.h"
#include "threads/vaddr.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void halt(void);
void exit(int status);
bool create(const char *file, unsigned initial_size);
int write(int fd, const void *buffer, unsigned size);

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

void syscall_init(void)
{
  write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
                          ((uint64_t)SEL_KCSEG) << 32);
  write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

  /* The interrupt service rountine should not serve any interrupts
   * until the syscall_entry swaps the userland stack to the kernel
   * mode stack. Therefore, we masked the FLAG_FL. */
  write_msr(MSR_SYSCALL_MASK,
            FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

void syscall_handler(struct intr_frame *f UNUSED)
{
  int syscall_number = (int)f->R.rax;

  switch (syscall_number)
  {
  case SYS_HALT:
    halt();
    break;
  case SYS_EXIT:
    exit((int)f->R.rdi);
    break;
  case SYS_CREATE:
    f->R.rax = create(f->R.rdi, f->R.rsi);
    break;
  case SYS_WRITE:
    f->R.rax = write((int)f->R.rdi, (void *)f->R.rsi, (unsigned)f->R.rdx);
    break;
  default:
    break;
  }
}

void halt(void)
{
  power_off();
}

void exit(int status)
{
  struct thread *curr = thread_current();
  curr->exit_status = status;
  printf("%s: exit(%d)\n", curr->name, curr->exit_status);
  thread_exit();
}

static void check_address(const void *addr)
{
  if (is_kernel_vaddr(addr) || addr == NULL)
    exit(-1);

  if (pml4_get_page(thread_current()->pml4, addr) == NULL)
    exit(-1);
}

bool create(const char *file, unsigned initial_size)
{
  check_address(file);
  return filesys_create(file, initial_size);
}

int write(int fd, const void *buffer, unsigned size)
{
  if (fd == 1)
  {
    putbuf(buffer, size);
  }
}
