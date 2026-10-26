#define _GNU_SOURCE
#include <openssl/rand.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int write_all(int fd, const uint8_t *buf, size_t len) {
  while (len > 0) {
    ssize_t n = write(fd, buf, len);
    if (n < 0) {
      if (errno == EINTR) continue;
      return 0;
    }
    buf += (size_t)n;
    len -= (size_t)n;
  }
  return 1;
}

static int read_all(int fd, uint8_t *buf, size_t len) {
  while (len > 0) {
    ssize_t n = read(fd, buf, len);
    if (n < 0) {
      if (errno == EINTR) continue;
      return 0;
    }
    if (n == 0) return 0;
    buf += (size_t)n;
    len -= (size_t)n;
  }
  return 1;
}

int main(int argc, char **argv) {
  int rounds = 100;
  if (argc == 2) rounds = atoi(argv[1]);
  if (rounds < 1) return 2;

  uint8_t warmup[32];
  if (RAND_bytes(warmup, sizeof(warmup)) != 1) return 2;

  for (int round = 0; round < rounds; ++round) {
    int fds[2];
    if (pipe(fds) != 0) {
      perror("pipe");
      return 2;
    }

    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      return 2;
    }
    if (pid == 0) {
      close(fds[0]);
      uint8_t child[32];
      if (RAND_bytes(child, sizeof(child)) != 1 ||
          !write_all(fds[1], child, sizeof(child))) {
        _exit(3);
      }
      close(fds[1]);
      _exit(0);
    }

    close(fds[1]);
    uint8_t parent[32];
    uint8_t child[32];
    if (RAND_bytes(parent, sizeof(parent)) != 1 ||
        !read_all(fds[0], child, sizeof(child))) {
      fprintf(stderr, "RAND/read failed at round %d\n", round);
      return 2;
    }
    close(fds[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
      fprintf(stderr, "child failed at round %d\n", round);
      return 2;
    }

    if (memcmp(parent, child, sizeof(parent)) == 0) {
      fprintf(stderr,
              "FAIL: parent and child produced identical first output after "
              "fork at round %d\n",
              round);
      return 1;
    }
  }

  printf("PASS: %d fork rounds produced distinct parent/child outputs\n",
         rounds);
  return 0;
}
