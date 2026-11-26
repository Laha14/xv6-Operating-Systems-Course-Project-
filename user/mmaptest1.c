#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define PGSIZE 4096
#define MAP_FAILED ((char *)0xffffffffffffffffUL)
#define F "mmap.dur"

static char buf[PGSIZE * 2];

static void
check(int ok, char *msg)
{
  if(!ok){
    printf("FAILED: %s\n", msg);
    exit(1);
  }
  printf("ok: %s\n", msg);
}

// create the test file: 2 pages of 'A'
static void
mk(void)
{
  int fd = open(F, O_WRONLY | O_CREATE | O_TRUNC);
  memset(buf, 'A', sizeof(buf));
  write(fd, buf, sizeof(buf));
  close(fd);
}

// read the file back into buf
static void
rd(void)
{
  int fd = open(F, O_RDONLY);
  read(fd, buf, sizeof(buf));
  close(fd);
}

// open the file, map 2 pages, close the fd (mapping must survive that)
static char *
map(int omode, int prot, int flags)
{
  int fd = open(F, omode);
  char *p = mmap(0, PGSIZE * 2, prot, flags, fd, 0);
  close(fd);
  return p;
}

int
main(void)
{
  char *p;
  int st;

  mk();
  p = map(O_RDONLY, PROT_READ, MAP_PRIVATE);
  check(p != MAP_FAILED && p[0] == 'A' && p[PGSIZE + 5] == 'A', "read mapping");
  munmap(p, PGSIZE * 2);

  mk();
  p = map(O_RDWR, PROT_READ | PROT_WRITE, MAP_PRIVATE);
  p[0] = 'B';
  munmap(p, PGSIZE * 2);
  rd();
  check(buf[0] == 'A', "private write not saved to file");

  mk();
  p = map(O_RDWR, PROT_READ | PROT_WRITE, MAP_SHARED);
  p[0] = 'C';
  p[PGSIZE] = 'D';
  munmap(p, PGSIZE * 2);
  rd();
  check(buf[0] == 'C' && buf[PGSIZE] == 'D', "shared write saved to file");

  mk();
  p = map(O_RDWR, PROT_READ | PROT_WRITE, MAP_SHARED);
  p[0] = 'E';
  check(munmap(p, PGSIZE) == 0 && p[PGSIZE] == 'A' &&
        munmap(p + PGSIZE, PGSIZE) == 0, "partial munmap (start, then rest)");
  rd();
  check(buf[0] == 'E', "partial munmap wrote back");

  mk();
  p = map(O_RDWR, PROT_READ | PROT_WRITE, MAP_SHARED);
  if(fork() == 0){
    p[0] = 'F';          // child exits without munmap
    exit(0);
  }
  wait(&st);
  munmap(p, PGSIZE * 2);
  rd();
  check(buf[0] == 'F', "fork + write-back on exit");

  unlink(F);
  printf("all tests OK\n");
  exit(0);
}