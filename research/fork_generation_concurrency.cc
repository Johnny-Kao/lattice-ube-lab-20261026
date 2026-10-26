#include <openssl/rand.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <thread>
#include <vector>

extern "C" uint64_t CRYPTO_get_fork_ube_generation(void);

static bool wait_child(pid_t pid, int round) {
  int status = 0;
  for (int ticks = 0; ticks < 200; ++ticks) {
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid) {
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
      std::fprintf(stderr, "FAIL round=%d child_status=%d\n", round, status);
      return false;
    }
    if (result < 0 && errno != EINTR) {
      std::perror("waitpid"); return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::fprintf(stderr, "TIMEOUT round=%d pid=%ld; killing child\n", round, (long)pid);
  kill(pid, SIGKILL);
  waitpid(pid, &status, 0);
  return false;
}

int main(int argc, char **argv) {
  const int rounds = argc > 1 ? std::atoi(argv[1]) : 20;
  if (rounds <= 0) return 2;
  unsigned char initial[32];
  if (RAND_bytes(initial, sizeof(initial)) != 1) return 3;
  const uint64_t parent_generation = CRYPTO_get_fork_ube_generation();
  if (!parent_generation) return 4;
  std::atomic<bool> quit{false}, failed{false};
  std::atomic<int> busy{0};
  std::vector<std::thread> workers;
  // The worker threads exist during fork, but synchronize at a safe boundary.
  // This avoids child-side fork() of a process whose libc/crypto lock is held.
  std::atomic<bool> pause{false};
  for (int t = 0; t < 4; ++t) {
    workers.emplace_back([&] {
      unsigned char buf[32];
      while (!quit.load(std::memory_order_acquire)) {
        if (pause.load(std::memory_order_acquire)) {
          std::this_thread::yield(); continue;
        }
        busy.fetch_add(1, std::memory_order_acq_rel);
        if (!pause.load(std::memory_order_acquire)) {
          if (RAND_bytes(buf, sizeof(buf)) != 1 ||
              CRYPTO_get_fork_ube_generation() != parent_generation)
            failed.store(true, std::memory_order_release);
        }
        busy.fetch_sub(1, std::memory_order_acq_rel);
      }
    });
  }
  int completed = 0;
  for (int round=0;round<rounds && !failed.load();++round) {
    pause.store(true, std::memory_order_release);
    while (busy.load(std::memory_order_acquire) != 0) std::this_thread::yield();
    // Quiescent fork tests the generation transition in a multithreaded
    // program. Active-while-forking remains a separate, unsupported case.
    const pid_t pid=fork();
    if (pid < 0) {std::perror("fork"); failed.store(true);break;}
    if (pid == 0) {
      const uint64_t child_generation=CRYPTO_get_fork_ube_generation();
      unsigned char buf[32];
      const bool ok=child_generation == parent_generation+1 &&
        RAND_bytes(buf,sizeof(buf))==1 &&
        CRYPTO_get_fork_ube_generation()==child_generation;
      _exit(ok?0:7);
    }
    const bool ok=wait_child(pid,round);
    if (CRYPTO_get_fork_ube_generation()!=parent_generation) failed.store(true);
    pause.store(false,std::memory_order_release);
    if (!ok) {failed.store(true);break;}
    completed++;
  }
  quit.store(true,std::memory_order_release);
  pause.store(false,std::memory_order_release);
  for(auto &t:workers)t.join();
  if(failed.load())return 1;
  std::printf("PASS: %d quiescent forks with 4 live parent threads; generation stable\n",completed);
  return 0;
}
