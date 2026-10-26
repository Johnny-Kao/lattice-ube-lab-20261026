# Handoff prompt — execute via GitHub MCP

You are continuing a public-source AWS-LC performance research task. Use the GitHub MCP for repository operations and inspection.

## Objective

Validate whether removing the normal-path read lock from AWS-LC fork UBE detection materially improves concurrent `RAND_bytes()` performance without changing fork/UBE correctness.

The attached ZIP already contains:

- `.github/workflows/rand-ube-research.yml`
- `research/atomic-fork-fastpath.patch`
- `research/bench_rand_bytes.cc`
- `research/fork_rand_correctness.c`
- `research/run_rand_bench.py`
- source/research notes and a provisional synthetic proxy result

Do not redesign the experiment unless current upstream source has changed enough to require a minimal compatibility update.

## 1. Collision check FIRST

Using GitHub MCP, inspect `aws/aws-lc` current `main` plus open/closed issues and pull requests. Search specifically for:

- `CRYPTO_get_fork_ube_generation`
- `fork_ube_detect.c`
- fork UBE atomic / lock-free / rwlock
- RAND / UBE contention
- recent changes touching `crypto/ube/fork_ube_detect.c`

Also inspect recent commits for that file. If an equivalent optimization is already open, merged, or clearly being worked on, STOP and report the collision. Do not create duplicate research work.

Record the current upstream HEAD SHA and confirm the source still has the normal-path `CRYPTO_STATIC_MUTEX_lock_read` in `CRYPTO_get_fork_ube_generation()`.

## 2. Create an INDEPENDENT research repo — NOT a fork

Use GitHub MCP to create a new **public** repository in my account with a neutral randomized research name such as `lattice-ube-lab-<random>`.

Hard requirements:

- Do NOT use GitHub's Fork operation.
- Do NOT create a GitHub fork relationship to `aws/aws-lc`.
- The repo must report `fork: false` / have no parent relationship.
- Upload the contents of this ZIP to the repository root, preserving `.github/workflows/rand-ube-research.yml`.
- Do not open any issue/PR against `aws/aws-lc`.

The supplied workflow intentionally bootstraps the public AWS-LC source snapshot into this independent repository on its first run. This avoids trying to copy thousands of upstream files one-by-one through MCP while still producing a true independent source-copy repo rather than a GitHub fork.

After the workflow bootstraps the source, verify that the research repo contains AWS-LC source and `research/UPSTREAM_COMMIT.txt` records the exact snapshot SHA.

## 3. Run GitHub Actions

The initial push to `main` should trigger `AWS-LC RAND UBE research`. If the MCP supports workflow dispatch and the push did not trigger it, dispatch the workflow manually.

The workflow should:

1. bootstrap the independent AWS-LC source snapshot if absent;
2. build unchanged baseline source;
3. apply `research/atomic-fork-fastpath.patch` only to a temporary patched copy;
4. build patched source/tests;
5. run 100 parent/child fork correctness rounds on baseline and patched builds;
6. benchmark `RAND_bytes()` for 16/32/256/4096-byte requests across available thread counts, 7 repetitions, alternating baseline/patched order;
7. run the AWS-LC test suite on the patched build;
8. upload `rand-ube-results` with `ENVIRONMENT.txt`, CSV, summary, fork logs, and ctest log.

If the patch no longer applies because upstream changed, inspect current source with GitHub MCP and update the patch minimally. Preserve the research hypothesis. Do not broaden into the higher-level UBE state machine unless the current source makes the localized experiment impossible.

## 4. Evaluate rigorously

Use actual GitHub Actions results, not the local synthetic proxy, for the decision.

Primary cell:

- 32-byte `RAND_bytes()`
- highest non-oversubscribed thread count
- runner must expose >=4 logical CPUs for a real GO decision

Decision rules:

- GO: >=5% stable median end-to-end improvement, no >2% 1-thread regression, fork correctness passes, AWS-LC tests pass.
- BORDERLINE: 2-5% and patch remains very small/local.
- DROP/DEFER: <2%, unstable/noisy, any correctness ambiguity, or patch complexity expands materially.
- If runner has <4 logical CPUs: label result INCONCLUSIVE even if oversubscribed-thread numbers look large.

Check individual repeats for variance/outliers; do not report only the best run.

## 5. Return a compact research report

Report:

- independent research repo URL
- confirmation `fork: false`
- upstream snapshot SHA
- Actions run URL/status
- runner CPU / `nproc`
- baseline vs patched medians for 32-byte RAND at 1 thread and max native threads
- percentage improvement
- fork correctness result
- AWS-LC test result
- any patch/build adaptation you had to make
- final `GO / BORDERLINE / DROP / INCONCLUSIVE`
- one-sentence reason

Do not submit or draft an upstream PR yet. The current task is validation only.
