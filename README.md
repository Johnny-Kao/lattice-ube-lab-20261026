# AWS-LC RAND / UBE research kit

Purpose: test one narrow performance hypothesis in public AWS-LC source without opening an upstream PR.

## Hypothesis

`CRYPTO_get_fork_ube_generation()` currently takes a process-global read lock on the normal no-fork path. `RAND_bytes()` reaches UBE checks on a hot path. Current BoringSSL uses an atomic common-case fork flag and only locks on the rare post-fork update path.

The candidate patch in `research/atomic-fork-fastpath.patch` tests whether the same shape materially improves AWS-LC `RAND_bytes()` under concurrency while preserving fork behavior.

## Repository model

This kit is designed for a **new independent GitHub repository**. It must not be created with GitHub's Fork function.

On the first GitHub Actions run, the workflow:

1. clones the latest public `aws/aws-lc` source;
2. copies that source snapshot into the independent research repo, excluding upstream Git metadata/workflows;
3. records the exact upstream SHA;
4. builds an unchanged baseline and a temporary patched copy;
5. runs correctness tests and repeated `RAND_bytes()` benchmarks;
6. runs the AWS-LC test suite on the patched build;
7. uploads CSV/log/summary results.

The research patch is **not applied to the repository's baseline source**. It remains a patch file so baseline and candidate can be built from the same snapshot in one CI run.

## Decision gate

Primary performance cell: 32-byte `RAND_bytes()` at the highest non-oversubscribed thread count.

- GO: >=5% stable median improvement on a runner with >=4 logical CPUs; no >2% 1-thread regression; correctness and AWS-LC tests pass.
- BORDERLINE: 2-5% with a very small/local final patch.
- DROP/DEFER: <2%, unstable result, correctness uncertainty, or materially larger maintenance burden.

Do not open an upstream issue or PR automatically.
