// Differential in-process kernel-like wipe. The source under test exposes
// only the address of its existing flag through a TEST-ONLY accessor.
// This does not alter the production fast/slow logic.
#include <openssl/rand.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <execinfo.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <thread>
#include <vector>

extern "C" uint64_t CRYPTO_get_fork_ube_generation(void);
extern "C" uintptr_t AWSLC_test_fork_flag_address(void);
extern "C" size_t AWSLC_test_fork_flag_width(void);

static void abort_trace(int sig) {
  const char msg[] = "TRACE: caught SIGABRT in kernel wipe differential\n";
  (void)write(STDERR_FILENO, msg, sizeof(msg)-1);
  void *frames[32];
  int count = backtrace(frames, 32);
  backtrace_symbols_fd(frames, count, STDERR_FILENO);
  _exit(128 + sig);
}

int main(int argc, char** argv) {
  signal(SIGABRT, abort_trace);
  const int rounds = argc > 1 ? atoi(argv[1]) : 200;
  const int mode = argc > 2 ? atoi(argv[2]) : 0;  // 0: generation; 1: unsafe RAND stress; 2: cooperative RAND
  const bool rand_workload = mode != 0;
  const bool cooperative = mode == 2;
  const size_t expected_width = argc > 3 ? (size_t)atoi(argv[3]) : 0;
  fprintf(stderr, "BEGIN rounds=%d mode=%d\n", rounds, mode);
  if (rounds < 1) return 2;
  uint64_t prior = CRYPTO_get_fork_ube_generation();
  uintptr_t p = AWSLC_test_fork_flag_address();
  const size_t width = AWSLC_test_fork_flag_width();
  if (expected_width && width != expected_width) {
    fprintf(stderr, "FAIL incorrect compiled variant flag_width=%zu expected=%zu\n", width, expected_width);
    return 5;
  }
  fprintf(stderr, "FLAG_WIDTH=%zu EXPECTED=%zu\n", width, expected_width);
  const long pagesize = sysconf(_SC_PAGESIZE);
  if (prior == 0 || p == 0 || pagesize < 1 || (p % pagesize) != 0) {
    fprintf(stderr, "FAIL initial generation=%llu addr=%p page=%ld\n",
            (unsigned long long)prior, (void*)p, pagesize);
    return 3;
  }
  std::atomic<bool> stop{false}, violation{false};
  std::atomic<uint64_t> calls{0};
  std::atomic<int> ready{0};
  std::atomic<int> active{0};
  std::atomic<bool> pause{false};
  std::atomic<uint64_t> per_thread[4]{};
  std::vector<std::thread> workers;
  for (int t = 0; t < 4; ++t) {
    workers.emplace_back([&, t] {
      uint64_t prev = prior;
      unsigned char bytes[32];
      ready.fetch_add(1, std::memory_order_release);
      while (!stop.load(std::memory_order_relaxed)) {
        if (cooperative && pause.load(std::memory_order_acquire)) {
          std::this_thread::yield();
          continue;
        }
        active.fetch_add(1, std::memory_order_acq_rel);
        if (cooperative && pause.load(std::memory_order_acquire)) {
          active.fetch_sub(1, std::memory_order_acq_rel);
          continue;
        }
        const uint64_t now = CRYPTO_get_fork_ube_generation();
        if (now == 0 || now < prev) violation.store(true);
        prev = now;
        if (rand_workload && RAND_bytes(bytes, sizeof(bytes)) != 1) violation.store(true);
        calls.fetch_add(1, std::memory_order_relaxed);
        per_thread[t].fetch_add(1, std::memory_order_relaxed);
        active.fetch_sub(1, std::memory_order_acq_rel);
      }
    });
  }
  int passed = 0;
  for (int i=0; i<rounds; ++i) {
    // Require real concurrent worker progress before EVERY kernel page reset.
    // A very fast main thread could otherwise finish all wipes before workers run.
    const uint64_t min_calls = 1000 + (uint64_t)(i + 1) * 128;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    auto all_progressed = [&] {
      if (ready.load(std::memory_order_acquire) != 4) return false;
      for (auto &counter : per_thread) {
        if (counter.load(std::memory_order_relaxed) < min_calls / 4) return false;
      }
      return true;
    };
    while (!all_progressed() && !violation.load()) {
      if (std::chrono::steady_clock::now() > deadline) {
        fprintf(stderr, "FAIL workers not scheduled before wipe=%d count=%llu\n",
                i, (unsigned long long)calls.load());
        violation.store(true);
        break;
      }
      std::this_thread::yield();
    }
    if (violation.load()) break;
    if (cooperative) {
      pause.store(true, std::memory_order_release);
      auto pause_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
      while (active.load(std::memory_order_acquire) != 0) {
        if (std::chrono::steady_clock::now() > pause_deadline) {
          fprintf(stderr, "FAIL cooperative pause timeout wipe=%d active=%d\n", i, active.load());
          violation.store(true);
          break;
        }
        std::this_thread::yield();
      }
      if (violation.load()) break;
    }
    const uint64_t before = CRYPTO_get_fork_ube_generation();
    if (madvise(reinterpret_cast<void*>(p), (size_t)pagesize, MADV_DONTNEED) != 0) {
      perror("madvise(MADV_DONTNEED)"); violation.store(true); break;
    }
    const uint64_t after = CRYPTO_get_fork_ube_generation();
    if (after != before + 1) {
      fprintf(stderr, "FAIL wipe=%d before=%llu after=%llu wanted=%llu\n",
              i,(unsigned long long)before,(unsigned long long)after,
              (unsigned long long)(before+1));
      violation.store(true); break;
    }
    passed++;
    if (cooperative) pause.store(false, std::memory_order_release);
    if ((i % 25) == 0) fprintf(stderr, "PROGRESS wipe=%d calls=%llu\n", i, (unsigned long long)calls.load());
  }
  stop.store(true);
  pause.store(false, std::memory_order_release);
  for (auto &t: workers) t.join();
  fprintf(stderr, "RESULT passed=%d/%d calls=%llu violation=%d generation=%llu\n",
          passed,rounds,(unsigned long long)calls.load(),violation.load(),
          (unsigned long long)CRYPTO_get_fork_ube_generation());
  return !violation.load() && passed==rounds ? 0:1;
}
