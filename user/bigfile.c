
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define BSIZE 1024
#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT * NINDIRECT)

int
main(void)
{
  int fd, block, pos;
  char data[BSIZE];

  printf("Testing maximum file size: %d blocks\n", (int)MAXFILE);

  fd = open("testfile", O_CREATE | O_RDWR);
  if(fd < 0){
    printf("Could not create file\n");
    exit(1);
  }

  // Write a different byte pattern to each block.
  for(block = 0; block < MAXFILE; block++){
    memset(data, block % 251, BSIZE);

    if(write(fd, data, BSIZE) != BSIZE){
      printf("Write error at block %d\n", block);
      close(fd);
      exit(1);
    }

    if((block + 1) % 4096 == 0 || block + 1 == MAXFILE)
      printf("Writing: %d/%d blocks complete\n", block + 1, (int)MAXFILE);
  }

  close(fd);
  printf("Writing completed. Checking file...\n");

  fd = open("testfile", O_RDONLY);
  if(fd < 0){
    printf("Could not open file for reading\n");
    exit(1);
  }

  // Check that every block contains the expected pattern.
  for(block = 0; block < MAXFILE; block++){
    if(read(fd, data, BSIZE) != BSIZE){
      printf("Read error at block %d\n", block);
      close(fd);
      exit(1);
    }

    for(pos = 0; pos < BSIZE; pos++){
      if((uchar)data[pos] != (uchar)(block % 251)){
        printf("Data mismatch in block %d\n", block);
        close(fd);
        exit(1);
      }
    }

    if((block + 1) % 4096 == 0 || block + 1 == MAXFILE)
      printf("Reading/checking: %d/%d blocks complete\n", block + 1, (int)MAXFILE);
  }

  close(fd);
  printf("All %d blocks verified successfully!\n", (int)MAXFILE);

  if(unlink("testfile") < 0){
    printf("Could not delete file\n");
    exit(1);
  }

  printf("Test file deleted successfully.\n");
  exit(0);
}
