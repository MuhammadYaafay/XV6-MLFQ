#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  if(argc < 2){
    printf("Usage: time command\n");
    exit(0);
  }
  int start = uptime();
  if(fork() == 0){
    exec(argv[1], argv+1);
    exit(0);
  }
  wait(0);
  int end = uptime();
  printf("Time elapsed: %d ticks\n", end - start);
  exit(0);
}
