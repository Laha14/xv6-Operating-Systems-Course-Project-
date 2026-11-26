#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

volatile int count = 0;

void
handler(void)
{
  count++;
  printf("Alarm fired! count=%d\n", count);
  sigreturn();
}

int
main(void)
{
  sigalarm(10, handler);

  while(count < 3) {
    // Keep running
  }

  sigalarm(0, handler);
  printf("Alarm test finished!\n");
  exit(0);
}