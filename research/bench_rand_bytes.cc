#include <openssl/rand.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

static std::atomic<int> ready{0};
static std::atomic<bool> go{false};
static std::atomic<bool> failed{false};
static std::atomic<uint64_t> checksum{0};

static void worker(size_t bytes, uint64_t iterations) {
  std::vector<uint8_t> out(bytes);

  // Initialize this thread's AWS-LC thread-local DRBG before the timed region.
  for (int i = 0; i < 100; ++i) {
    if (RAND_bytes(out.data(), out.size()) != 1) {
      failed.store(true, std::memory_order_relaxed);
      return;
    }
  }

  ready.fetch_add(1, std::memory_order_release);
  while (!go.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  uint64_t local = 0;
  for (uint64_t i = 0; i < iterations; ++i) {
    if (RAND_bytes(out.data(), out.size()) != 1) {
      failed.store(true, std::memory_order_relaxed);
      return;
    }
    // Keep a tiny observable dependency outside AWS-LC. One byte is enough to
    // prevent the caller from treating the generated buffer as dead.
    local ^= out[(i + bytes - 1) % bytes];
  }
  checksum.fetch_xor(local, std::memory_order_relaxed);
}

int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "usage: " << argv[0]
              << " <threads> <bytes> <iterations-per-thread>\n";
    return 2;
  }
  int threads = std::atoi(argv[1]);
  size_t bytes = static_cast<size_t>(std::strtoull(argv[2], nullptr, 10));
  uint64_t iterations = std::strtoull(argv[3], nullptr, 10);
  if (threads < 1 || bytes < 1 || iterations < 1) return 2;

  ready.store(0, std::memory_order_relaxed);
  go.store(false, std::memory_order_relaxed);
  failed.store(false, std::memory_order_relaxed);

  std::vector<std::thread> pool;
  pool.reserve(static_cast<size_t>(threads));
  for (int i = 0; i < threads; ++i) {
    pool.emplace_back(worker, bytes, iterations);
  }
  while (ready.load(std::memory_order_acquire) != threads) {
    if (failed.load(std::memory_order_relaxed)) return 1;
    std::this_thread::yield();
  }

  auto start = std::chrono::steady_clock::now();
  go.store(true, std::memory_order_release);
  for (auto &t : pool) t.join();
  auto end = std::chrono::steady_clock::now();
  if (failed.load(std::memory_order_relaxed)) return 1;

  const uint64_t total_ops = static_cast<uint64_t>(threads) * iterations;
  const double seconds = std::chrono::duration<double>(end - start).count();
  const double ns_per_op = seconds * 1e9 / static_cast<double>(total_ops);
  const double mib_per_s =
      static_cast<double>(total_ops) * static_cast<double>(bytes) /
      seconds / (1024.0 * 1024.0);

  std::cout << "threads=" << threads
            << " bytes=" << bytes
            << " iterations=" << iterations
            << " total_ops=" << total_ops
            << " seconds=" << seconds
            << " ns_per_op=" << ns_per_op
            << " mib_per_s=" << mib_per_s
            << " checksum=" << checksum.load(std::memory_order_relaxed)
            << "\n";
  return 0;
}
