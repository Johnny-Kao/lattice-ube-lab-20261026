#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
  int threads;
  uint64_t ops_per_thread;
  int patched;
} config_t;

static pthread_rwlock_t fork_lock = PTHREAD_RWLOCK_INITIALIZER;
static pthread_rwlock_t ube_lock = PTHREAD_RWLOCK_INITIALIZER;
static uint64_t fork_generation = 1;
static uint64_t ube_generation = 1;
static _Atomic uint32_t fork_flag = 1;
static _Atomic uint64_t sink = 0;

static pthread_barrier_t barrier;
static config_t cfg;

static inline uint64_t baseline_one(void) {
  uint64_t x = 0;
  for (int check = 0; check < 2; check++) {
    pthread_rwlock_rdlock(&fork_lock);
    x ^= fork_generation;
    pthread_rwlock_unlock(&fork_lock);

    pthread_rwlock_rdlock(&ube_lock);
    x ^= ube_generation;
    pthread_rwlock_unlock(&ube_lock);
  }
  return x;
}

static inline uint64_t patched_one(void) {
  uint64_t x = 0;
  for (int check = 0; check < 2; check++) {
    if (atomic_load_explicit(&fork_flag, memory_order_acquire) != 0) {
      x ^= fork_generation;
    }

    pthread_rwlock_rdlock(&ube_lock);
    x ^= ube_generation;
    pthread_rwlock_unlock(&ube_lock);
  }
  return x;
}

static void *worker(void *unused) {
  (void)unused;
  pthread_barrier_wait(&barrier);
  uint64_t local = 0;
  for (uint64_t i = 0; i < cfg.ops_per_thread; i++) {
    if (cfg.patched) {
      local ^= patched_one();
    } else {
      local ^= baseline_one();
    }
  }
  atomic_fetch_xor_explicit(&sink, local, memory_order_relaxed);
  return NULL;
}

static double now_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: %s <threads> <ops_per_thread> <baseline|patched>\n", argv[0]);
    return 2;
  }
  cfg.threads = atoi(argv[1]);
  cfg.ops_per_thread = strtoull(argv[2], NULL, 10);
  cfg.patched = argv[3][0] == 'p';
  if (cfg.threads < 1 || cfg.ops_per_thread < 1) return 2;

  pthread_t *t = calloc((size_t)cfg.threads, sizeof(*t));
  if (!t) return 2;
  pthread_barrier_init(&barrier, NULL, (unsigned)cfg.threads + 1);

  for (int i = 0; i < cfg.threads; i++) {
    if (pthread_create(&t[i], NULL, worker, NULL) != 0) return 2;
  }
  pthread_barrier_wait(&barrier);
  double start = now_sec();
  for (int i = 0; i < cfg.threads; i++) pthread_join(t[i], NULL);
  double end = now_sec();

  uint64_t total = (uint64_t)cfg.threads * cfg.ops_per_thread;
  double sec = end - start;
  double ns_per_op = sec * 1e9 / (double)total;
  double mops = (double)total / sec / 1e6;
  printf("mode=%s threads=%d total_ops=%llu seconds=%.6f ns_per_op=%.2f mops=%.3f sink=%llu\n",
         cfg.patched ? "patched" : "baseline", cfg.threads,
         (unsigned long long)total, sec, ns_per_op, mops,
         (unsigned long long)atomic_load_explicit(&sink, memory_order_relaxed));
  free(t);
  return 0;
}
