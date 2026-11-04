#!/usr/bin/env python3
"""Inject the reviewer-required regression into a Research copy of AWS-LC tests.

No production code is changed. Fail if pinned source layout drifts.
"""
from pathlib import Path
import sys

p = Path(sys.argv[1])
s = p.read_text()
anchor = "#endif  // OPENSSL_LINUX && !OPENSSL_TSAN"
assert s.count(anchor) == 1, "pinned upstream test layout changed"
assert "#include <thread>" in s and "WaitpidEINTR" in s
assert "SimultaneousFirstReadAfterFork" not in s

s = s.replace("#include <functional>\n", "#include <atomic>\n#include <functional>\n")
test = r"""
#if defined(OPENSSL_THREADS)
// Every reader enters the fork-generation check simultaneously in a new
// child. No generation read or RAND call may re-arm the fork flag first.
TEST(ForkDetect, SimultaneousFirstReadAfterFork) {
  maybeDisableSomeForkUbeDetectMechanisms();
  const uint64_t parent_generation = CRYPTO_get_fork_ube_generation();
  if (parent_generation == 0) {
    GTEST_SKIP() << "Fork detection unavailable";
  }
  constexpr int kReaders = 8;
  constexpr int kForks = 40;
  for (int round = 0; round < kForks; ++round) {
    fflush(stderr);
    const pid_t child = fork();
    if (child == 0) {
      std::atomic<int> ready{0};
      std::atomic<bool> go{false}, failed{false};
      std::vector<std::thread> threads;
      for (int i = 0; i < kReaders; ++i) {
        threads.emplace_back([&] {
          ready.fetch_add(1, std::memory_order_release);
          while (!go.load(std::memory_order_acquire)) {
            std::this_thread::yield();
          }
          const uint64_t first = CRYPTO_get_fork_ube_generation();
          const uint64_t second = CRYPTO_get_fork_ube_generation();
          if (first != parent_generation + 1 ||
              second != parent_generation + 1) {
            failed.store(true, std::memory_order_release);
          }
        });
      }
      while (ready.load(std::memory_order_acquire) != kReaders) {
        std::this_thread::yield();
      }
      go.store(true, std::memory_order_release);
      for (auto &thread : threads) {
        thread.join();
      }
      _exit(failed.load(std::memory_order_acquire) ? 1 : 0);
    }
    ASSERT_GT(child, 0) << "fork failed: " << strerror(errno);
    int status = 0;
    bool exited = false;
    for (int tick = 0; tick < 500; ++tick) {
      const pid_t result = WaitpidEINTR(child, &status, WNOHANG);
      if (result == child) {
        exited = true;
        break;
      }
      ASSERT_GE(result, 0) << "waitpid failed";
      usleep(10000);
    }
    if (!exited) {
      kill(child, SIGKILL);
      WaitpidEINTR(child, &status, 0);
      FAIL() << "Child timed out in round " << round;
    }
    ASSERT_TRUE(WIFEXITED(status)) << "child terminated abnormally";
    ASSERT_EQ(0, WEXITSTATUS(status)) << "first read mismatch in round " << round;
    EXPECT_EQ(parent_generation, CRYPTO_get_fork_ube_generation());
  }
}
#endif  // OPENSSL_THREADS

"""
assert "#include <signal.h>" not in s
s = s.replace("#include <stdio.h>\n", "#include <signal.h>\n#include <stdio.h>\n")
s = s.replace(anchor, test + anchor)
p.write_text(s)
print("Inserted 40-fork / 8-reader official regression")
