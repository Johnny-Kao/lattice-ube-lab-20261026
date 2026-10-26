# Source notes — RAND / fork UBE candidate

Scope: public-source performance research only. This kit does **not** assert an upstream bug and must not auto-open an issue or PR.

## Current AWS-LC observations (checked 2026-10-26)

- `crypto/ube/fork_ube_detect.c`
  - `CRYPTO_get_fork_ube_generation()` takes the process-global `fork_detect_ube_lock` in read mode on the normal no-fork path.
  - On a cleared WIPEONFORK / inherit-zero flag it upgrades to a write-lock path and increments `fgn`.
  - Source: https://github.com/aws/aws-lc/blob/main/crypto/ube/fork_ube_detect.c

- `crypto/ube/ube.c`
  - `CRYPTO_get_ube_generation_number()` explicitly labels the path hot and notes that unnecessary read locks cost cycles even when not contended.
  - It calls the fork detector and then takes `ube_lock` in read mode in the normal case.
  - Source: https://github.com/aws/aws-lc/blob/main/crypto/ube/ube.c

- `crypto/fipsmodule/rand/rand.c`
  - RAND generation checks UBE uniqueness before generation and validates state again after generation.
  - Source: https://github.com/aws/aws-lc/blob/main/crypto/fipsmodule/rand/rand.c

## Upstream design precedent

Current BoringSSL uses an atomic fork flag on the common path and takes a lock only when the flag is observed cleared. Its source comment explicitly says this avoids cacheline contention in the PRNG.

Source: https://github.com/google/boringssl/blob/main/crypto/rand/fork_detect.cc

## Research hypothesis

On Linux builds with lock-free C11 atomics, replace only the normal fork-detector read-lock path with an atomic flag load. Preserve the write lock for the rare post-fork generation update. Leave the higher-level AWS-LC UBE state machine untouched.

The patch in this kit is **research-only**. It intentionally limits the fast path to Linux + lock-free C11 atomics so GitHub Actions can test the hypothesis without prematurely designing the final cross-platform upstream patch.
