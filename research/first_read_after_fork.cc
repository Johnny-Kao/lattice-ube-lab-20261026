// Research regression: every simultaneous first reader in a fresh forked
// child must observe exactly parent_generation + 1. Not a TSAN test: TSAN's
// multi-threaded fork model is unsupported by the upstream existing test.
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

extern "C" uint64_t CRYPTO_get_fork_ube_generation(void);

static constexpr int kReaders = 8;
static int child_test(uint64_t expected) {
  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  std::atomic<int> wrong{0};
  std::vector<std::thread> threads;
  threads.reserve(kReaders);
  for (int i = 0; i < kReaders; ++i) {
    threads.emplace_back([&] {
      ready.fetch_add(1, std::memory_order_release);
      while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
      auto first = CRYPTO_get_fork_ube_generation();
      auto second = CRYPTO_get_fork_ube_generation();
      if (first != expected || second != expected) {
        std::fprintf(stderr, "reader got %llu/%llu, expected %llu\n",
            (unsigned long long)first, (unsigned long long)second,
            (unsigned long long)expected);
        wrong.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (ready.load(std::memory_order_acquire) != kReaders) {
    if (std::chrono::steady_clock::now() > deadline) _exit(30);
    std::this_thread::yield();
  }
  go.store(true, std::memory_order_release);
  for (auto &t : threads) t.join();
  return wrong.load() ? 31 : 0;
}

int main(int argc, char **argv) {
  int rounds = argc > 1 ? std::atoi(argv[1]) : 40;
  if (rounds < 1 || rounds > 2000) return 2;
  const auto parent = CRYPTO_get_fork_ube_generation();
  if (parent == 0 || parent == UINT64_MAX) {
    std::fprintf(stderr, "fork generation unavailable or wrapped\n");
    return 3;
  }
  for (int r = 0; r < rounds; ++r) {
    pid_t pid = fork();
    if (pid == -1) { std::perror("fork"); return 4; }
    if (pid == 0) _exit(child_test(parent + 1));
    int status = 0;
    bool exited = false;
    for (int tick = 0; tick < 500; ++tick) {
      auto rc = waitpid(pid, &status, WNOHANG);
      if (rc == pid) { exited = true; break; }
      if (rc < 0 && errno != EINTR) { std::perror("waitpid"); return 5; }
      usleep(10000);
    }
    if (!exited) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      std::fprintf(stderr, "round=%d child timeout\n", r);
      return 6;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      std::fprintf(stderr, "round=%d child status=%d\n", r, status);
      return 7;
    }
    if (CRYPTO_get_fork_ube_generation() != parent) {
      std::fprintf(stderr, "parent generation changed at round=%d\n", r);
      return 8;
    }
  }
  std::printf("PASS: %d fresh child forks, %d synchronized first readers each\n", rounds, kReaders);
  return 0;
}
