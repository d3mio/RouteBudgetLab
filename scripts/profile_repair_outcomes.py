#!/usr/bin/env python3
"""
RouteBudget Lab - Repair Outcome Separation Profiler (Gate 1 Audit & WP1/FR-02)

Instruments 5 distinct, mutually exclusive repair outcomes across the 4 training partition maps:
1. COMPLETED_ACCEPTED: All neighborhood agents routed; sum-of-costs strictly improved.
2. COMPLETED_NOT_IMPROVING: Neighborhood routed or cost-pruned; cost >= old cost (LNS rejection).
3. FAILED_NO_PATH: Space-Time A* search failed to find a collision-free path.
4. INTERRUPTED_SLICE: Active search cutoff reached mid-repair (with prefix metrics).
5. STOPPED_DEADLINE: Global search deadline reached.

Runs under:
- UNCONSTRAINED (natural repair dynamics, no artificial slice cutoff)
- ARTIFICIAL_SLICE_10MS (10 ms time slice)
- ARTIFICIAL_SLICE_5MS (5 ms time slice)
"""

import os, sys, json, subprocess

TRAINING_MAPS = [
    ("warehouse-10-20-10-2-1", "warehouse-10-20-10-2-1.map", "warehouse-10-20-10-2-1-random-1.scen"),
    ("random-32-32-20", "random-32-32-20.map", "random-32-32-20-random-1.scen"),
    ("maze-32-32-2", "maze-32-32-2.map", "maze-32-32-2-random-1.scen"),
    ("empty-32-32", "empty-32-32.map", "empty-32-32-random-1.scen")
]

PROFILER = "build/rbl-release/rbl_profile_outcomes"
OUT_DIR = "results/profiling"

def run_profiling():
    os.makedirs(OUT_DIR, exist_ok=True)
    all_runs = []

    configs = [
        ("UNCONSTRAINED", 0.0),
        ("ARTIFICIAL_SLICE_10MS", 10.0),
        ("ARTIFICIAL_SLICE_5MS", 5.0)
    ]

    for map_key, map_file, scen_file in TRAINING_MAPS:
        map_path = os.path.join("data/training/maps", map_file)
        scen_path = os.path.join("data/training/scenarios", scen_file)

        for k in [50, 100]:
            paths_file = os.path.join(OUT_DIR, f"profile_{map_key}_{k}a_paths.txt")
            if not os.path.exists(paths_file):
                print(f"Warning: Paths file {paths_file} missing, skipping...")
                continue

            for cfg_name, slice_ms in configs:
                out_json = os.path.join(OUT_DIR, f"outcome_{map_key}_{k}a_{cfg_name}.json")
                cmd = [
                    PROFILER,
                    "--map", map_path,
                    "--scen", scen_path,
                    "--agents", str(k),
                    "--paths", paths_file,
                    "--iterations", "50",
                    "--n-size", "8",
                    "--slice-ms", str(slice_ms),
                    "--out-json", out_json
                ]
                print(f"Running: {map_key} | {k} agents | {cfg_name} (slice={slice_ms}ms)...")
                res = subprocess.run(cmd, capture_output=True, text=True)
                if res.returncode != 0:
                    print(f"ERROR: {res.stderr}")
                    continue

                if os.path.exists(out_json):
                    with open(out_json, "r") as f:
                        data = json.load(f)
                    all_runs.append(data)

    summary_file = os.path.join(OUT_DIR, "repair_outcomes_summary.json")
    with open(summary_file, "w") as f:
        json.dump({
            "timestamp": "Gate 1 Audit Oct 21 2026",
            "benchmark_scope": "Training partition partition profiling (4 maps, 50 & 100 agents)",
            "runs": all_runs
        }, f, indent=2)

    print(f"\nConsolidated outcome summary written to {summary_file}")

if __name__ == "__main__":
    run_profiling()
