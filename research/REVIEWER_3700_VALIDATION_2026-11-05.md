# AWS-LC PR #3700 — reviewer validation record (2026-11-05)

## Scope and provenance

- Upstream review: https://github.com/aws/aws-lc/pull/3700
- Pinned original PR SHA: `632e337448a5a9fd9b8e85df92b340c3797e8aaa`
- Research branch: `research/aws-lc-3700-review-validation`
- Production patch experiment: `research/reviewer_fgn_variant.py` applied to pinned PR head.
- Official GTest patch experiment: `research/integrate_reviewer_fork_test.py`.
- **No upstream PR changes, messages, or review-thread resolutions have been made.**

## Reviewer issue-by-issue

1. Fork/WIPEONFORK comment and wrong VM-cloning claim: corrected in proposed source transform. The original kernel action clears a child's memory view, not the parent's running page.
2. Extend to BSD MAP_INHERIT_ZERO: reviewer calls this **optional**. No BSD fast-path extension implemented. Existing BSD fallback compile and functional fork check pass on FreeBSD 14.5.
3. Simplify `fgn` atomic ordering: considered. Proposed ordinary `uint64_t` is stricter simplification (no 64-bit atomics); the 32-bit flag still serves as publication with acquire/release. Keep relaxed atomic `fgn` variant as a viable contingency if the reviewer rejects plain `fgn`.
4. Synchronized first read after fork: 60 fresh children × 8 synchronized readers in stand-alone test; 40 fresh children × 8 readers in official `crypto_test`; both PASS. Additionally 30 quiescent forks with 4 live parent threads PASS.
5. Ordinary `uint64_t fgn` with 32-bit flag: implemented as a research-only variant; official minimal tests and Linux first-read functional checks PASS.

## Evidence

- Full research verification, including FreeBSD 14.5: https://github.com/Johnny-Kao/lattice-ube-lab-20261026/actions/runs/41872671247 (SUCCESS)
- Official `crypto_test` integration: https://github.com/Johnny-Kao/lattice-ube-lab-20261026/actions/runs/41874340420 (SUCCESS; one GTest PASS, 405ms)
- Existing TSan forced-wipe baseline and review: https://github.com/Johnny-Kao/lattice-ube-lab-20261026/actions/runs/41869352053 (diagnostic run). The subsequent full pass explicitly preserves the known out-of-scope TSan diagnostic.
- Reviewer change reuses `crypto/ube/fork_ube_detect.c`. Official test candidate adds `crypto/ube/fork_ube_detect_test.cc`.

### Concurrency caveat

Under the synthetic **in-process MADV_DONTNEED reset while other threads run**, the ordinary `fgn` implementation triggers a real TSan race on `fgn` (read and write). The original PR's atomic `fgn` does not trigger this race. This synthetic reset does **not** reproduce ordinary Linux `MADV_WIPEONFORK`, which zeros the child address space upon fork and leaves the parent's page intact. This result is documented, not hidden, and excluded **only** from normal fork production acceptance. It must not be represented as `TSan universally clean`.

Native differential tests: head and review each passed 500 generation-reset rounds and 150 coordinated RAND rounds. Review TSan cooperative (70 rounds) PASS; head TSan generation (200 rounds) and cooperative (70) PASS. The review TSan uncoordinated generation reset reports the acknowledged race.

## CPU, RAM, throughput — paired A/B

Source: artifact `results/resource/{resource_raw.csv,SUMMARY.md}` in Run 41872671247.
Runner Linux x86_64, AMD EPYC 9V74, 4 logical CPUs. 7 alternating repeated trials per scenario; values below are medians and apply ONLY to the original PR head versus reviewer plain-`fgn` variant, **not** upstream main. `/usr/bin/time` peak RSS is high-water mark, not additional heap allocation.

| Scenario | Original PR | Reviewer variant | Interpretation |
|---|---:|---:|---|
| 4 threads, RAND(32), ns/op | 110.59 | 109.23 | reviewer ~1.2% faster |
| 4 threads, RAND(32), ops/s | 9,042,726 | 9,154,627 | reviewer ~1.2% higher |
| 4 threads, RAND(32), user+sys CPU sec | 0.410 | 0.410 | no measured difference at this resolution |
| 4 threads, RAND(32), peak RSS KiB | 5,080 | 5,056 | 24 KiB less; likely measurement noise |
| 4 threads, RAND(4096), ns/op | 363.81 | 364.05 | effectively unchanged |
| 4 threads, generation only, ns/op | 1.36 | 1.40 | reviewer ~2.9% slower; microbenchmark noise |
| 4 threads, generation only, peak RSS KiB | 4,988 | 5,108 | 120 KiB higher; likely noise |
| 1 thread, RAND(32), ns/op | 313.07 | 314.35 | reviewer ~0.4% slower; likely noise |

Performance evidence **does not establish a robust speed or RAM benefit** for plain `fgn`; simplicity is the main motivation. Do not assert significant performance wins or fixed memory savings.

## Remaining boundaries / final reviewer sign-off

- The full suite of cross-platform upstream CI, especially AArch64, platform lock-free gates, and sanitizers under AWS-LC's own workflow matrix has **not** been run on the proposed integrated commit.
- The test was integrated and validated in a Research copy, not submitted to upstream.
- The optional BSD **fast-path extension** is not implemented; only existing BSD fallback was verified.
- Ensure exact source/test diff and comments are reviewed together before any upstream push. Seek reviewer/maintainer feedback on whether the synthetic in-process reset guarantee is intended; do not claim the more permissive contract as proven for all VM checkpoint/restore systems.

## Working decision

The normal fork acceptance matrix supports proceeding with reviewer-style plain `uint64_t` **as a candidate**, including synchronized GTest. No evidence yet warrants merging or claiming all possible race scenarios covered. Keep all upstream writes/comments on hold until owner explicitly authorizes them.
