#!/usr/bin/env python3
"""Paired resource accounting for reviewer vs PR-head. Linux only.
Reports per-process user/system CPU seconds, peak RSS KiB and wall times.
Peak RSS is process high-water mark, not incremental memory allocation.
"""
import argparse
import csv
import os
from pathlib import Path
import re
import statistics
import subprocess
import tempfile

def parse_time(path):
    s = Path(path).read_text()
    def val(p):
        m = re.search(p, s, re.M)
        if not m:
            raise ValueError(f"resource metric missing: {p}")
        return float(m.group(1))
    return (val(r"User time \(seconds\):\s*([0-9.]+)"),
            val(r"System time \(seconds\):\s*([0-9.]+)"),
            val(r"Maximum resident set size \(kbytes\):\s*([0-9.]+)"))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--head", required=True)
    ap.add_argument("--review", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--repeats", type=int, default=7)
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    cpuinfo = Path("/proc/cpuinfo").read_text()
    model = re.search(r"^model name\s*:\s*(.+)$", cpuinfo, re.M)
    metadata = {"logical_cpus": os.cpu_count(), "cpu_model": model.group(1) if model else "unknown"}
    (out / "environment.txt").write_text(str(metadata) + "\n" + subprocess.check_output(["uname", "-a"], text=True))
    rows = []
    for threads, size, iterations in [(1, 32, 200000), (4, 32, 200000), (8, 32, 200000),
                                       (4, 4096, 100000), (1, 0, 3000000), (4, 0, 3000000), (8, 0, 3000000)]:
        for rep in range(args.repeats):
            order = ["head", "review"] if rep % 2 == 0 else ["review", "head"]
            for variant in order:
                binary = getattr(args, variant)
                with tempfile.NamedTemporaryFile(prefix="resource-", delete=False) as tmp:
                    resource_file = tmp.name
                try:
                    output = subprocess.check_output(
                        ["/usr/bin/time", "-v", "-o", resource_file,
                         binary, str(threads), str(size), str(iterations)],
                        text=True, timeout=90).strip()
                    user, system, rss = parse_time(resource_file)
                finally:
                    os.unlink(resource_file)
                kv = dict(re.findall(r"(\w+)=([^ ]+)", output))
                row = dict(variant=variant, repeat=rep, threads=threads, bytes=size,
                           iterations=iterations, user_cpu_s=user, system_cpu_s=system,
                           cpu_total_s=user+system, peak_rss_kib=rss,
                           wall_s=float(kv["seconds"]), ns_per_op=float(kv["ns_per_op"]),
                           ops_per_s=int(kv["total_ops"])/float(kv["seconds"]),
                           mib_per_s=float(kv["mib_per_s"]))
                rows.append(row)
                print(row, flush=True)
    with (out / "resource_raw.csv").open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader(); w.writerows(rows)
    with (out / "SUMMARY.md").open("w") as f:
        f.write("# Reviewer vs PR head: resource measurement\n\n")
        f.write("Linux process wall and CPU seconds, /usr/bin/time peak RSS (KiB). "
                "Same runner, sequential alternating A/B order; medians over "
                f"{args.repeats} repetitions. Peak RSS is not allocations.\n\n")
        f.write(f"CPU: {metadata['cpu_model']} | logical CPUs: {metadata['logical_cpus']}\n\n")
        f.write("| Threads | Bytes | Variant | ns/op | ops/s | user+system CPU s | peak RSS KiB |\n"
                "|---:|---:|---|---:|---:|---:|---:|\n")
        for threads, size, _ in [(1,32,0),(4,32,0),(8,32,0),(4,4096,0),(1,0,0),(4,0,0),(8,0,0)]:
            for variant in ("head", "review"):
                rr = [r for r in rows if r["threads"] == threads and r["bytes"] == size and r["variant"] == variant]
                med = lambda name: statistics.median(r[name] for r in rr)
                f.write(f"| {threads} | {size} | {variant} | {med('ns_per_op'):.2f} | "
                        f"{med('ops_per_s'):.0f} | {med('cpu_total_s'):.3f} | "
                        f"{med('peak_rss_kib'):.0f} |\n")
if __name__ == "__main__":
    main()
