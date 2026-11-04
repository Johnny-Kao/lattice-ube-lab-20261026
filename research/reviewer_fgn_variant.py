#!/usr/bin/env python3
"""Research-only transformation of the exact upstream PR head; fail closed on drift."""
from pathlib import Path
import sys

path = Path(sys.argv[1])
s = path.read_text()
changes = [
("""// The kernel can clear the flag's backing page after fork or VM cloning.
// On Linux, use lock-free atomics to avoid the reader lock in the common case.
// Other platforms retain the original mutex-protected volatile implementation.
#if defined(OPENSSL_LINUX) && defined(OPENSSL_C11_ATOMIC) && \\
    ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_LLONG_LOCK_FREE == 2""",
"""// WIPEONFORK clears this mapped flag in the forked child when it is
// single-threaded. Readers may race to rearm it after child threads start.
// The generation is published before a release-store to the flag and then
// remains fixed in that address-space copy; the flag requires lock-free
// atomic access, while the generation is protected by this publication
// protocol and the slow-path write lock. BSD retains the existing fallback.
#if defined(OPENSSL_LINUX) && defined(OPENSSL_C11_ATOMIC) && \\
    ATOMIC_INT_LOCK_FREE == 2"""),
("static _Atomic uint64_t fgn = 0;", "static uint64_t fgn = 0;"),
("atomic_store_explicit(&fgn, 1, memory_order_relaxed);", "fgn = 1;"),
("return atomic_load_explicit(&fgn, memory_order_acquire);", "return fgn;"),
("uint64_t current_fgn = atomic_load_explicit(&fgn, memory_order_relaxed);", "uint64_t current_fgn = fgn;"),
("atomic_store_explicit(&fgn, current_fgn, memory_order_release);", "fgn = current_fgn;"),
("""  // In the limit, the kernel may clear e.g. WIPEONFORK pages while a
  // multi-threaded process is running. (For example, because a VM was cloned.)
  // Therefore a lock is used below to synchronize the potentially multiple
  // threads that may concurrently observe the cleared flag.""",
"""  // The kernel clears WIPEONFORK in the forked child while it is
  // single-threaded. Multiple threads created later may observe the cleared
  // flag together, so the slow-path update is serialized by a write lock."""),
]
for old, new in changes:
    count = s.count(old)
    if count != 1:
        raise SystemExit(f"FAIL CLOSED: expected one occurrence, got {count}: {old[:80]!r}")
    s = s.replace(old, new)
path.write_text(s)
print("Reviewer variant transformation complete")
