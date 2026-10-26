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
#include <sys/mman.h>
#include <unistd.h>
#include <thread>
#include <vector>

extern "C" uint64_t CRYPTO_get_fork_ube_generation(void);
extern "C" uintptr_t AWSLC_test_fork_flag_address(void);

int main(int argc, char** argv) {
  const int rounds = argc > 1 ? atoi(argv[1]) : 200;
  if (rounds < 1) return 2;
  uint64_t prior = CRYPTO_get_fork_ube_generation();
  uintptr_t p = AWSLC_test_fork_flag_address();
  const long pagesize = sysconf(_SC_PAGESIZE);
  if (prior == 0 || p == 0 || pagesize < 1 || (p % pagesize) != 0) {
    fprintf(stderr, "FAIL initial generation=%llu addr=%p page=%ld\n",
            (unsigned long long)prior, (void*)p, pagesize);
    return 3;
  }
  std::atomic<bool> stop{false}, violation{false};
  std::atomic<uint64_t> calls{0};
  std::vector<std::thread> workers;
  for (int t = 0; t < 4; ++t) {
    workers.emplace_back([&] {
      uint64_t prev = prior;
      unsigned char bytes[32];
      while (!stop.load(std::memory_order_relaxed)) {
        const uint64_t now = CRYPTO_get_fork_ube_generation();
        if (now == 0 || now < prev) violation.store(true);
        prev = now;
        if (RAND_bytes(bytes, sizeof(bytes)) != 1) violation.store(true);
        calls.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  int passed = 0;
  for (int i=0; i<rounds; ++i) {
    if (violation.load()) break;
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
  }
  stop.store(true);
  for (auto &t: workers) t.join();
  fprintf(stderr, "RESULT passed=%d/%d calls=%llu violation=%d generation=%llu\n",
          passed,rounds,(unsigned long long)calls.load(),violation.load(),
          (unsigned long long)CRYPTO_get_fork_ube_generation());
  return !violation.load() && passed==rounds ? 0:1;
}
