
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main(void)
{
  char buf[100];
  int fd, n;

  printf("Starting symlink tests...\n");

  // Test 1: Symlink to a regular file
  fd = open("original.txt", O_CREATE | O_WRONLY);
  if(fd < 0) {
    printf("Failed to create original.txt\n");
    exit(1);
  }
  write(fd, "Hello from original file!", 25);
  close(fd);

  if(symlink("original.txt", "link1") < 0) {
    printf("Failed to create symlink\n");
    exit(1);
  }

  fd = open("link1", O_RDONLY);
  if(fd < 0) {
    printf("FAIL: Could not open symlink\n");
  } else {
    n = read(fd, buf, sizeof(buf) - 1);
    buf[n] = '\0';
    printf("Test 1: %s\n", strcmp(buf, "Hello from original file!") == 0
           ? "PASS" : "FAIL");
    close(fd);
  }

  // Test 2: Symlink to another symlink
  if(symlink("link1", "link2") < 0) {
    printf("Failed to create chained symlink\n");
    exit(1);
  }

  fd = open("link2", O_RDONLY);
  if(fd < 0) {
    printf("Test 2: FAIL\n");
  } else {
    n = read(fd, buf, sizeof(buf) - 1);
    buf[n] = '\0';
    printf("Test 2: %s\n", strcmp(buf, "Hello from original file!") == 0
           ? "PASS" : "FAIL");
    close(fd);
  }

  // Test 3: Symlink to nonexistent target
  if(symlink("missing.txt", "broken") < 0) {
    printf("Failed to create broken symlink\n");
    exit(1);
  }

  fd = open("broken", O_RDONLY);
  printf("Test 3 (broken link): %s\n", fd < 0 ? "PASS" : "FAIL");
  if(fd >= 0)
    close(fd);

  // Test 4: Circular symlinks
  if(symlink("cycleB", "cycleA") < 0 ||
     symlink("cycleA", "cycleB") < 0) {
    printf("Failed to create circular symlinks\n");
    exit(1);
  }

  fd = open("cycleA", O_RDONLY);
  printf("Test 4 (circular links): %s\n", fd < 0 ? "PASS" : "FAIL");
  if(fd >= 0)
    close(fd);

  printf("Symlink tests completed.\n");
  exit(0);
}
