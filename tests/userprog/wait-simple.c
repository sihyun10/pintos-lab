/* Wait for a subprocess to finish. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int pid;
  if ((pid = fork ("child-simple"))){

    msg ("wait(exec()) = %d", wait (pid));
  } else {

    exec ("child-simple");
  }
}

// void test_main(void) 
// {
//   int pid1 = fork("1");
//   if (pid1 == 0) {
//     msg("hi from child 1");
//     exit(0);
//   }
  
//   int pid2 = fork("2");
//   if (pid2 == 0) {
//     msg("hi from child 2");
//     exit(0);
//   }
  
//   // Only parent reaches here
//   msg ("wait(exec()) = %d", wait(pid1));
//   msg ("wait(exec()) = %d", wait(pid2));
// }

/* tests/userprog/fork-yield.c */

