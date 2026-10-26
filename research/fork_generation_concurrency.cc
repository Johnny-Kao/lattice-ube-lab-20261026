#include <openssl/rand.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <thread>
#include <vector>

extern "C" uint64_t CRYPTO_get_fork_ube_generation(void);

int main(int argc, char **argv) {
  const int rounds = argc > 1 ? std::atoi(argv[1]) : 100;
  if (rounds <= 0) return 2;
  unsigned char initial[32];
  if (RAND_bytes(initial, sizeof(initial)) != 1) return 3;
  const uint64_t parent_generation = CRYPTO_get_fork_ube_generation();
  if (parent_generation == 0) {
    std::fprintf(stderr, "FAIL fork UBE generation is unavailable on Linux\n");
    return 4;
  }
  std::atomic<bool> quit{false};
  std::atomic<bool> failure{false};
  std::vector<std::thread> workers;
  for (int t = 0; t < 4; ++t) {
    workers.emplace_back([&] {
      unsigned char buf[32];
      while (!quit.load(std::memory_order_relaxed)) {
        if (RAND_bytes(buf, sizeof(buf)) != 1 ||
            CRYPTO_get_fork_ube_generation() != parent_generation) {
          failure.store(true, std::memory_order_relaxed);
          return;
        }
      }
    });
  }
  for (int round = 0; round < rounds && !failure.load(); ++round) {
    const pid_t pid = fork();
    if (pid < 0) { failure.store(true); break; }
    if (pid == 0) {
      // This process is single-threaded after fork. Child must detect one
      // generation transition, preserve the result, and generate randomness.
      const uint64_t before_rand = CRYPTO_get_fork_ube_generation();
      unsigned char buf[32];
      const int ok = (before_rand == parent_generation + 1) &&
          RAND_bytes(buf, sizeof(buf)) == 1 &&
          CRYPTO_get_fork_ube_generation() == before_rand;
      _exit(ok ? 0 : 7);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
        CRYPTO_get_fork_ube_generation() != parent_generation) {
      failure.store(true);
      std::fprintf(stderr, "FAIL round=%d status=%d\n", round, status);
      break;
    }
  }
  quit.store(true, std::memory_order_relaxed);
  for (auto &t : workers) t.join();
  if (failure.load()) return 1;
  std::printf("PASS: %d fork generation transitions while 4 parent threads call RAND_bytes\n", rounds);
  return 0;
}
