# Results template

- Research repo:
- Repo reports `fork: false`:
- Upstream AWS-LC snapshot SHA:
- GitHub Actions run URL:
- Runner OS / CPU / `nproc`:
- Compiler / CMake:
- Patch applied cleanly: YES / NO
- Baseline build: PASS / FAIL
- Patched build: PASS / FAIL
- Patched full tests: PASS / FAIL
- Baseline fork correctness: PASS / FAIL
- Patched fork correctness: PASS / FAIL

## Primary benchmark

32-byte `RAND_bytes`, highest non-oversubscribed thread count:

- baseline median ns/op:
- patched median ns/op:
- improvement:
- 1-thread regression/improvement:

## Decision

- GO: >=5% stable end-to-end improvement at >=4 native threads, no >2% single-thread regression, all correctness/tests pass.
- BORDERLINE: 2–5% with a small/local patch.
- DROP/DEFER: <2%, unstable, correctness ambiguity, or patch complexity expands materially.

Do not open an upstream AWS-LC issue or PR from this research run.
