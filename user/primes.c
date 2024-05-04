#include "kernel/types.h"
#include "user/user.h"
__attribute__((noreturn)) void primes(int p[2]) {
  int prime, num;
  int fd[2];
  close(p[1]);
  if (read(p[0], &prime, 4) != 4) {
    fprintf(2, "read error\n");
    exit(1);
  }
  fprintf(1, "prime %d\n", prime);
  pipe(fd);
  int running = read(p[0], &num, 4);
  if (running) {
    if (fork() == 0) {  // child
      primes(fd);
    } else {  // parent
      close(fd[0]);
      if (num % prime) write(fd[1], &num, 4);
      while (read(p[0], &num, 4) == 4) {
        if (num % prime) write(fd[1], &num, 4);
      }
      close(fd[1]);
      wait(0);
      exit(0);
    }
  }
  close(p[0]);
  exit(0);
}
int main(int argc, char* argv[]) {
  int p[2];
  pipe(p);
  if (fork() == 0) {  // child
    primes(p);
  } else {  // parent
    close(p[0]);
    for (int i = 2; i <= 35; i++) {
      write(p[1], &i, 4);
    }
    close(p[1]);
    wait(0);
    exit(0);
  }
  return 0;
}
