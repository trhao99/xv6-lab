#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char **argv) {
  int p[2];
  pipe(p);
  char buf[2];
  if (fork() == 0) {  // child
    if (read(p[0], buf, 1) != 1) {
      fprintf(2, "child read error\n");
      exit(1);
    }
    fprintf(1, "%d: received ping\n", getpid());
    if (write(p[1], "a", 1) != 1) {
      fprintf(2, "child write error\n");
      exit(1);
    }
    close(p[0]);
    close(p[1]);
    exit(0);
  } else {  // parent
    if (write(p[1], "a", 1) != 1) {
      fprintf(2, "parent write error\n");
      exit(1);
    }
    wait(0);
    if (read(p[0], buf, 1) != 1) {
      fprintf(2, "parent read error\n");
      exit(1);
    }
    fprintf(1, "%d: received pong\n", getpid());
    close(p[1]);
    close(p[0]);
    exit(0);
  }
  exit(0);
}
