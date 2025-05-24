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

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

static void syscall_halt(void);
static void syscall_exit(int status);

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

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
  uint64_t syscall_num = f->R.rax;

  // TODO: Your implementation goes here.
  switch (syscall_num)
  {
  case SYS_HALT:
    syscall_halt();
    break;
  case SYS_EXIT:
  {
    int status = (int)f->R.rdi;
    syscall_exit(status);
    break;
  }
  default:
    thread_exit();
  }
}

static void syscall_halt(void)
{
  power_off();
}

static void syscall_exit(int status)
{
  struct thread *curr = thread_current();

  curr->exit_status = status;
  curr->has_exited = true;

  printf("%s: exit(%d)\n", curr->name, status);

  sema_up(&curr->wait_sema);

  thread_exit();
}