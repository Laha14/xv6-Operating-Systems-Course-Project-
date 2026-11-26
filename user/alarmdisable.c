#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

volatile int count = 0;

void
handler(void)
{
  count++;
  printf("Alarm fired!\n");
  sigreturn();
}

int
main(void)
{
  sigalarm(5, handler);

  while(count < 1);

  sigalarm(0, handler);

  int old_count = count;

  for(volatile int i = 0; i < 100000000; i++);

  if(count == old_count)
    printf("PASS: Alarm disabled\n");
  else
    printf("FAIL: Alarm fired after disabling\n");

  exit(0);
}