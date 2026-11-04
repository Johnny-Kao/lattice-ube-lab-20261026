# AWS-LC #3700 — staging exact-commit verification (2026-11-05)

Status: **PASS for scoped staging Linux x86_64 and AArch64 functional gate**, pending owner decision before upstream changes.

## Staging snapshot

- Repo: https://github.com/Johnny-Kao/aws-lc-staging
- Staging branch: `staging/pr-3700-reviewer-validation`
- Staging commit (pinned): `d50fbf075a8789a65ae30531dc4f8b36a4a88043`
- Parent 2 commits back: `632e337448a5a9fd9b8e85df92b340c3797e8aaa`
- Exactly two changed files vs parent: `crypto/ube/fork_ube_detect.c` and `crypto/ube/fork_ube_detect_test.cc`.
- Original PR-tracking branch `perf/fork-ube-atomic-fast-path` remains at `632e337448a5a9fd9b8e85df92b340c3797e8aaa` (verified).
- BSD fast-path extension explicitly **PARKED**. Existing FreeBSD fallback was separately verified in research Run 41872671247 and not changed by this staging diff.

## GitHub Actions evidence

- Exact staging commit test: https://github.com/Johnny-Kao/lattice-ube-lab-20261026/actions/runs/41875850458
- x86_64 Linux / ubuntu-latest: **SUCCESS**
- AArch64 Linux / ubuntu-24.04-arm: **SUCCESS**
- Both matrix jobs: exact SHA and 2-file diff checks, CMake/Ninja build, AWS-LC native `crypto_test` filter `ForkDetect.SimultaneousFirstReadAfterFork:ForkDetect.Test`, official `run_minimal_tests`, standalone 60 fresh fork children with 8 synchronized readers each, and evidence artifacts all passed.
- Artifact `staging-3700-x86_64`: 13347680820; `staging-3700-aarch64`: 13346702930.
- Research CI workflow: `.github/workflows/staging-3700-exact.yml`.

## Remaining decisions and limitations

- This validates the exact staging source snapshot on two Linux architectures. It is not the full upstream required CI matrix nor every possible fork/clone/VM-restoration mode.
- Research forced in-process `MADV_DONTNEED` concurrent reset still produces a real TSan plain-`fgn` race. That is not the normal child-only `MADV_WIPEONFORK` semantics. Retain the diagnostic and do not claim TSan universally clean.
- Prior CPU/RAM comparison of the two research variants appears in `research/REVIEWER_3700_VALIDATION_2026-11-05.md`. No new staging-specific CPU/RAM measurement in this run; staged source reflects the same candidate transform.
- No upstream code, PR comment, or review-thread resolution was posted. Wait for owner decision on whether to keep `uint64_t`, include the official test, and update upstream.
