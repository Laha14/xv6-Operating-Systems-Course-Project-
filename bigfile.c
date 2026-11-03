#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define BSIZE 512
#define NDIRECT 11
#define NINDIRECT 128
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT * NINDIRECT)

int
main(void)
{
  int fd, i, j;
  char buf[BSIZE];

  printf(1, "Creating maximum-size file (%d blocks)...\n", MAXFILE);

  fd = open("large.dat", O_CREATE | O_RDWR);
  if(fd < 0){
    printf(1, "open failed\n");
    exit();
  }

  for(i = 0; i < MAXFILE; i++){
    memset(buf, i % 256, sizeof(buf));

    if(write(fd, buf, sizeof(buf)) != sizeof(buf)){
      printf(1, "write failed at block %d\n", i);
      close(fd);
      exit();
    }
  }

  close(fd);
  printf(1, "Write completed. Verifying contents...\n");

  fd = open("large.dat", O_RDONLY);
  if(fd < 0){
    printf(1, "reopen failed\n");
    exit();
  }

  for(i = 0; i < MAXFILE; i++){
    if(read(fd, buf, sizeof(buf)) != sizeof(buf)){
      printf(1, "read failed at block %d\n", i);
      close(fd);
      exit();
    }

    for(j = 0; j < BSIZE; j++){
      if((uchar)buf[j] != (uchar)(i % 256)){
        printf(1, "verification failed at block %d\n", i);
        close(fd);
        exit();
      }
    }
  }

  close(fd);
  printf(1, "All %d blocks verified successfully.\n", MAXFILE);

  if(unlink("large.dat") < 0){
    printf(1, "unlink failed\n");
    exit();
  }

  printf(1, "File deleted successfully.\n");
  exit();
}