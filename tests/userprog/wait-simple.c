/* Wait for a subprocess to finish. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int pid;
  if ((pid = fork ("child-simple"))){
    //msg("child pid: %d", pid);
    
    //for(int i=0; i<2000000000; i++){}

    msg ("wait(exec()) = %d", wait (pid));
  } else {

    exec ("child-simple");
  }
}
