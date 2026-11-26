// user/mmaptest.c -- tests for Task 3 (mmap / munmap) on xv6-riscv.
//
// NOTE: a few tests deliberately touch unmapped memory in a child process.
// The kernel prints "usertrap(): unexpected scause ..." for those; that
// output is EXPECTED and the test passes when the child is killed.

#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "user/user.h"

#ifndef PGSIZE
#define PGSIZE 4096
#endif

#define MAP_FAILED ((char *)0xffffffffffffffffUL)
#define FNAME  "mmap.dur"
#define FSIZE  (PGSIZE * 2 + 512)               // 2.125 pages: last page partial
#define RUP(x) ((((x) + PGSIZE - 1) / PGSIZE) * PGSIZE)
#define PAT(i) ((char)('A' + ((i) % 26)))       // known file pattern

static char buf[PGSIZE * 4];

static void
fail(char *msg)
{
  printf("mmaptest: FAILED: %s\n", msg);
  exit(1);
}

// (re)create FNAME with the known pattern.
static void
makefile(void)
{
  int fd, i;

  for(i = 0; i < FSIZE; i++)
    buf[i] = PAT(i);
  fd = open(FNAME, O_WRONLY | O_CREATE | O_TRUNC);
  if(fd < 0)
    fail("cannot create test file");
  if(write(fd, buf, FSIZE) != FSIZE)
    fail("cannot write test file");
  close(fd);
}

// read the whole file into buf, return its size.
static int
readfile(void)
{
  int fd, n;

  fd = open(FNAME, O_RDONLY);
  if(fd < 0)
    fail("cannot open file for verification");
  n = read(fd, buf, sizeof(buf));
  close(fd);
  return n;
}

// fork a child that loads from address a; the child must be killed.
static void
expect_fault(volatile char *a, char *what)
{
  int pid, st;

  pid = fork();
  if(pid < 0)
    fail("fork");
  if(pid == 0){
    char c = *a;          // should trap and kill us
    (void)c;
    exit(0);              // reaching this line means no fault happened
  }
  wait(&st);
  if(st == 0)
    fail(what);
}

// ---------------------------------------------------------------------
static void
test_read_private(void)
{
  int fd, i;
  char *p;

  makefile();
  fd = open(FNAME, O_RDONLY);
  p = mmap(0, FSIZE, PROT_READ, MAP_PRIVATE, fd, 0);
  if(p == MAP_FAILED)
    fail("mmap PROT_READ|MAP_PRIVATE");
  close(fd);                                  // mapping must keep file alive

  for(i = 0; i < FSIZE; i++)
    if(p[i] != PAT(i))
      fail("mapped contents differ from file");
  for(i = FSIZE; i < RUP(FSIZE); i++)
    if(p[i] != 0)
      fail("bytes past EOF in last page must read as zero");

  if(munmap(p, FSIZE) != 0)
    fail("munmap whole region");
  printf("  read/private ............ ok\n");
}

static void
test_permissions_and_args(void)
{
  int fd;
  char *p;

  makefile();

  // writable MAP_SHARED of a read-only fd must fail
  fd = open(FNAME, O_RDONLY);
  if(mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) != MAP_FAILED)
    fail("PROT_WRITE|MAP_SHARED on O_RDONLY fd should fail");

  // but a private writable mapping of a read-only fd is fine
  p = mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if(p == MAP_FAILED)
    fail("PROT_WRITE|MAP_PRIVATE on O_RDONLY fd should succeed");
  p[0] = 'Z';
  if(munmap(p, PGSIZE) != 0)
    fail("munmap private");
  close(fd);

  // PROT_READ on a write-only fd must fail
  fd = open(FNAME, O_WRONLY);
  if(mmap(0, PGSIZE, PROT_READ, MAP_PRIVATE, fd, 0) != MAP_FAILED)
    fail("PROT_READ on O_WRONLY fd should fail");
  close(fd);

  // bad length / bad munmap
  fd = open(FNAME, O_RDWR);
  if(mmap(0, 0, PROT_READ, MAP_SHARED, fd, 0) != MAP_FAILED)
    fail("len == 0 should fail");
  close(fd);
  if(munmap((void *)(PGSIZE * 100), PGSIZE) != -1)
    fail("munmap of never-mapped range should return -1");

  printf("  permissions / bad args .. ok\n");
}

static void
test_private_write(void)
{
  int fd;
  char *p;

  makefile();
  fd = open(FNAME, O_RDWR);
  p = mmap(0, FSIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if(p == MAP_FAILED)
    fail("mmap RW/private");
  p[0] = 'Z';
  p[PGSIZE] = 'Z';
  if(p[0] != 'Z' || p[PGSIZE] != 'Z')
    fail("private write not visible to writer");
  munmap(p, FSIZE);
  close(fd);

  readfile();
  if(buf[0] != PAT(0) || buf[PGSIZE] != PAT(PGSIZE))
    fail("MAP_PRIVATE write leaked into the file");
  printf("  private write ........... ok\n");
}

static void
test_shared_write(void)
{
  int fd, n;
  char *p;

  makefile();
  fd = open(FNAME, O_RDWR);
  p = mmap(0, FSIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(p == MAP_FAILED)
    fail("mmap RW/shared");
  p[0] = 'S';
  p[PGSIZE + 1] = 'T';
  p[FSIZE - 1] = 'U';
  p[FSIZE + 10] = 'X';          // past EOF but inside last page: must be dropped
  if(munmap(p, FSIZE) != 0)
    fail("munmap shared");
  close(fd);

  n = readfile();
  if(n != FSIZE)
    fail("munmap write-back changed the file size");
  if(buf[0] != 'S' || buf[PGSIZE + 1] != 'T' || buf[FSIZE - 1] != 'U')
    fail("MAP_SHARED write was not written back");
  if(buf[1] != PAT(1))
    fail("untouched byte corrupted by write-back");
  printf("  shared write-back ....... ok\n");
}

static void
test_beyond_file(void)
{
  int fd;
  char *p;

  makefile();
  fd = open(FNAME, O_RDONLY);
  p = mmap(0, PGSIZE * 8, PROT_READ, MAP_PRIVATE, fd, 0);   // lazy: 8 pages
  if(p == MAP_FAILED)
    fail("mmap longer than file");
  if(p[3] != PAT(3))
    fail("first page content");
  if(p[PGSIZE * 5] != 0)
    fail("page wholly past EOF must read as zero");
  munmap(p, PGSIZE * 8);
  close(fd);
  printf("  region longer than file . ok\n");
}

static void
test_partial_unmap(void)
{
  int fd;
  char *p;

  makefile();
  fd = open(FNAME, O_RDWR);
  p = mmap(0, PGSIZE * 3, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(p == MAP_FAILED)
    fail("mmap 3 pages");
  close(fd);

  p[0] = '1';
  p[PGSIZE] = '2';
  p[PGSIZE * 2] = '3';

  if(munmap(p, PGSIZE) != 0)                    // unmap the START
    fail("munmap start");
  if(p[PGSIZE] != '2' || p[PGSIZE * 2] != '3')
    fail("remaining pages damaged by start unmap");

  if(munmap(p + PGSIZE * 2, PGSIZE) != 0)       // unmap the END
    fail("munmap end");
  if(p[PGSIZE] != '2')
    fail("middle page damaged by end unmap");

  expect_fault(p, "page 0 still accessible after munmap");
  expect_fault(p + PGSIZE * 2, "page 2 still accessible after munmap");

  if(munmap(p + PGSIZE, PGSIZE) != 0)           // unmap what is left
    fail("munmap remaining page");

  readfile();
  if(buf[0] != '1' || buf[PGSIZE] != '2' || buf[PGSIZE * 2] != '3')
    fail("partial munmap did not write back");
  printf("  partial munmap .......... ok\n");
}

static void
test_fork_shared(void)
{
  int fd, pid, st;
  char *p;

  makefile();
  fd = open(FNAME, O_RDWR);
  p = mmap(0, PGSIZE * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(p == MAP_FAILED)
    fail("mmap for fork_shared");
  close(fd);

  if(p[0] != PAT(0))            // page 0 becomes resident BEFORE fork
    fail("initial read");        // page 1 stays unfaulted

  pid = fork();
  if(pid < 0)
    fail("fork");
  if(pid == 0){
    p[0] = 'C';                 // resident page: physically shared
    p[PGSIZE] = 'D';            // unfaulted page: loaded in the child alone
    exit(0);                    // exit must write both back
  }
  wait(&st);
  if(st != 0)
    fail("child failed");

  if(p[0] != 'C')
    fail("child write to resident shared page not visible to parent");
  if(p[PGSIZE] != 'D')
    fail("child's exit write-back not seen when parent faults page in");

  munmap(p, PGSIZE * 2);
  readfile();
  if(buf[0] != 'C' || buf[PGSIZE] != 'D')
    fail("file does not contain child's shared writes");
  printf("  fork + MAP_SHARED ....... ok\n");
}

static void
test_fork_private(void)
{
  int fd, pid, st;
  char *p;

  makefile();
  fd = open(FNAME, O_RDWR);
  p = mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if(p == MAP_FAILED)
    fail("mmap for fork_private");
  close(fd);
  p[0] = 'P';

  pid = fork();
  if(pid < 0)
    fail("fork");
  if(pid == 0){
    if(p[0] != 'P')
      exit(2);                  // child must inherit contents
    p[0] = 'Q';
    if(p[0] != 'Q')
      exit(3);
    exit(0);
  }
  wait(&st);
  if(st != 0)
    fail("child private check failed");
  if(p[0] != 'P')
    fail("child's private write visible in parent");

  munmap(p, PGSIZE);
  readfile();
  if(buf[0] != PAT(0))
    fail("private writes reached the file");
  printf("  fork + MAP_PRIVATE ...... ok\n");
}

static void
test_exit_writeback(void)
{
  int fd, pid, st;
  char *p;

  makefile();
  pid = fork();
  if(pid < 0)
    fail("fork");
  if(pid == 0){
    fd = open(FNAME, O_RDWR);
    p = mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(p == MAP_FAILED)
      exit(1);
    close(fd);
    p[10] = 'E';
    exit(0);                    // no munmap: exit must write back
  }
  wait(&st);
  if(st != 0)
    fail("child failed");
  readfile();
  if(buf[10] != 'E')
    fail("exit did not write back MAP_SHARED changes");
  printf("  write-back on exit ...... ok\n");
}

static void
test_multiple_mappings(void)
{
  int fd;
  char *p1, *p2;

  makefile();
  fd = open(FNAME, O_RDONLY);
  p1 = mmap(0, PGSIZE * 2, PROT_READ, MAP_PRIVATE, fd, 0);
  p2 = mmap(0, PGSIZE, PROT_READ, MAP_PRIVATE, fd, 0);
  if(p1 == MAP_FAILED || p2 == MAP_FAILED)
    fail("two mmaps");
  close(fd);
  if(!(p2 + PGSIZE <= p1 || p1 + PGSIZE * 2 <= p2))
    fail("mappings overlap");
  if(p1[PGSIZE + 5] != PAT(PGSIZE + 5) || p2[7] != PAT(7))
    fail("contents of multiple mappings");
  if(munmap(p1, PGSIZE * 2) != 0 || munmap(p2, PGSIZE) != 0)
    fail("munmap of multiple mappings");
  printf("  multiple mappings ....... ok\n");
}

static void
test_many_mappings(void)
{
  int fd, i, n;
  char *ps[64];
  char *p;

  makefile();
  fd = open(FNAME, O_RDONLY);
  for(n = 0; n < 64; n++){
    ps[n] = mmap(0, PGSIZE, PROT_READ, MAP_PRIVATE, fd, 0);
    if(ps[n] == MAP_FAILED)
      break;
  }
  if(n < 16)
    fail("could not create at least 16 mappings");
  for(i = 0; i < n; i++)
    if(munmap(ps[i], PGSIZE) != 0)
      fail("munmap of one of many mappings");
  p = mmap(0, PGSIZE, PROT_READ, MAP_PRIVATE, fd, 0);   // slots reusable
  if(p == MAP_FAILED)
    fail("VMA slots not released by munmap");
  munmap(p, PGSIZE);
  close(fd);
  printf("  many mappings (%d) ....... ok\n", n);
}

static void
test_read_into_mapping(void)
{
  int fd, src, i;
  char *p;
  char tmp[100];

  makefile();
  for(i = 0; i < 100; i++)
    tmp[i] = 'Z';
  src = open("mmap.src", O_WRONLY | O_CREATE | O_TRUNC);   // a DIFFERENT file
  write(src, tmp, 100);
  close(src);

  fd = open(FNAME, O_RDWR);
  p = mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(p == MAP_FAILED)
    fail("mmap for read_into_mapping");
  close(fd);

  src = open("mmap.src", O_RDONLY);
  // p+200 is still unfaulted: copyout() must fault the page in.
  if(read(src, p + 200, 100) != 100)
    fail("read() into a lazy mmap page failed (vmfault patch missing?)");
  close(src);
  if(p[200] != 'Z' || p[299] != 'Z')
    fail("read() into mapping: wrong data");
  munmap(p, PGSIZE);

  readfile();
  if(buf[200] != 'Z')
    printf("  WARN: read() into MAP_SHARED page not written back "
           "(copyout PTE_D follow-up not applied)\n");
  else
    printf("  read() into mapping ..... ok\n");
  unlink("mmap.src");
}

int
main(int argc, char *argv[])
{
  printf("mmaptest starting\n");
  test_read_private();
  test_permissions_and_args();
  test_private_write();
  test_shared_write();
  test_beyond_file();
  test_partial_unmap();
  test_fork_shared();
  test_fork_private();
  test_exit_writeback();
  test_multiple_mappings();
  test_many_mappings();
  test_read_into_mapping();
  unlink(FNAME);
  printf("mmaptest: all tests OK\n");
  exit(0);
}