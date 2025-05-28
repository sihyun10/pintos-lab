/* This program attempts to execute code at a kernel virtual address. 
   This should terminate the process with a -1 exit code. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  msg ("Congratulations - you have successfully called kernel code: %d", 
        ((int (*)(void))0x8004000000)());
  fail ("should have exited with -1");
}

// void
// test_main (void) 
// {
//   int tid;
//   if((tid = fork("child"))){
//     msg("wait child exit: %d\n", wait(tid));
//   }
//   else{
//     msg ("Congratulations - you have successfully called kernel code: %d", 
//           ((int (*)(void))0x8004000000)());
//   }
  
// }
