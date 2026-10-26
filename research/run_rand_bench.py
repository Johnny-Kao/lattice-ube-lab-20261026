#!/usr/bin/env python3
import argparse
import csv
import os
import re
import statistics
import subprocess
from pathlib import Path

KV = re.compile(r"(\w+)=([^ ]+)")


def run_one(binary, threads, size, iterations):
    out = subprocess.check_output(
        [binary, str(threads), str(size), str(iterations)], text=True
    ).strip()
    vals = dict(KV.findall(out))
    return {
        "threads": int(vals["threads"]),
        "bytes": int(vals["bytes"]),
        "iterations": int(vals["iterations"]),
        "total_ops": int(vals["total_ops"]),
        "seconds": float(vals["seconds"]),
        "ns_per_op": float(vals["ns_per_op"]),
        "mib_per_s": float(vals["mib_per_s"]),
    }


def iterations_for(size):
    # Constant call count across input sizes; avoids short 4096-byte trials.
    return 100000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--patched", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--repeats", type=int, default=11)
    ap.add_argument("--mode", choices=["rand", "fork"], default="rand")
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    cpus = os.cpu_count() or 1
    native_max = min(cpus, 8)
    thread_counts = sorted(set([1, 2, min(4, cpus), native_max, min(16, cpus * 2)]))
    thread_counts = [t for t in thread_counts if t >= 1]
    sizes = [0] if args.mode == "fork" else [16, 32, 256, 4096]

    rows = []
    for size in sizes:
        iters = iterations_for(size)
        for threads in thread_counts:
            for rep in range(args.repeats):
                # Alternate ordering to reduce systematic thermal/frequency bias.
                order = ["baseline", "patched"] if rep % 2 == 0 else ["patched", "baseline"]
                for variant in order:
                    binary = args.baseline if variant == "baseline" else args.patched
                    r = run_one(binary, threads, size, iters)
                    r.update({"variant": variant, "repeat": rep, "cpu_count": cpus})
                    rows.append(r)
                    print(variant, rep, r, flush=True)

    csv_path = out_dir / "rand_benchmark.csv"
    fields = [
        "variant", "repeat", "cpu_count", "threads", "bytes", "iterations",
        "total_ops", "seconds", "ns_per_op", "mib_per_s"
    ]
    with csv_path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)

    grouped = {}
    for r in rows:
        grouped.setdefault((r["threads"], r["bytes"], r["variant"]), []).append(r["ns_per_op"])

    summary = []
    for threads in thread_counts:
        for size in sizes:
            b = grouped[(threads, size, "baseline")]
            p = grouped[(threads, size, "patched")]
            bmed = statistics.median(b)
            pmed = statistics.median(p)
            improvement = (bmed - pmed) / bmed * 100.0
            summary.append((threads, size, bmed, pmed, improvement))

    md = out_dir / "SUMMARY.md"
    with md.open("w") as f:
        f.write(f"# AWS-LC {args.mode} baseline vs patched benchmark\n\n")
        f.write(f"Runner logical CPUs: **{cpus}**  \n")
        f.write(f"Repetitions per cell: **{args.repeats}**\n\n")
        f.write("| Threads | RAND bytes | Baseline median ns/op | Patched median ns/op | Improvement |\n")
        f.write("|---:|---:|---:|---:|---:|\n")
        for threads, size, bmed, pmed, imp in summary:
            f.write(f"| {threads} | {size} | {bmed:.2f} | {pmed:.2f} | {imp:+.2f}% |\n")

        primary_size = 0 if args.mode == "fork" else 32
        target = [x for x in summary if x[0] == native_max and x[1] == primary_size][0]
        imp = target[4]
        one_thread = [x for x in summary if x[0] == 1 and x[1] == primary_size][0][4]
        f.write("\n## Automated research gate\n\n")
        f.write(f"Primary cell: 32-byte RAND at {native_max} native threads: **{imp:+.2f}%**.  \n")
        f.write(f"1-thread 32-byte change: **{one_thread:+.2f}%**.\n\n")
        if cpus < 4:
            decision = "INCONCLUSIVE: runner exposes fewer than 4 logical CPUs; treat as directional only."
        elif imp >= 5.0 and one_thread >= -2.0:
            decision = "GO: performance gate passed. Continue correctness/concurrency validation before any upstream PR."
        elif imp >= 2.0 and one_thread >= -2.0:
            decision = "BORDERLINE: measurable but below preferred 5% gate. Review patch complexity and noise."
        else:
            decision = "DROP/DEFER: end-to-end gain is below the research threshold or regresses 1-thread performance."
        f.write(f"**{decision}**\n")

    print(md.read_text())


if __name__ == "__main__":
    main()
