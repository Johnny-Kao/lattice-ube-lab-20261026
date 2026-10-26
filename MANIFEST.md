# Manifest

- `PROMPT_FOR_NEXT_AI.md` — exact handoff instructions for a GitHub-MCP-capable AI.
- `.github/workflows/rand-ube-research.yml` — bootstraps an independent AWS-LC source snapshot and runs baseline vs patched GitHub Actions research.
- `research/atomic-fork-fastpath.patch` — research-only Linux+C11 atomic fast-path candidate; baseline source remains unchanged.
- `research/bench_rand_bytes.cc` — end-to-end concurrent `RAND_bytes()` benchmark harness.
- `research/fork_rand_correctness.c` — parent/child post-fork output-divergence smoke test.
- `research/run_rand_bench.py` — repeated alternating benchmark runner, CSV output, medians and GO/BORDERLINE/DROP gate.
- `research/synthetic_lock_bench.c` — standalone synchronization proxy.
- `research/SYNTHETIC_RESULTS.txt` — local proxy results; directional only, not an AWS-LC end-to-end result.
- `research/SOURCE_NOTES.md` — public-source hypothesis and references.
- `research/RESULTS_TEMPLATE.md` — compact final research report template.
