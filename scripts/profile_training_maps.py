#!/usr/bin/env python3
"""
RouteBudget Lab - Training Partition Profiler for Gate 1 (O1, Gate 1, WP1).

Runs the baseline solver on all 4 training maps at 50 and 100 agents.
Extracts per-iteration repair durations, success/failure dynamics,
verifies plans with independent validator, and writes a consolidated
profiling report for the Gate 1 assessment.
"""

import os, sys, json, time, subprocess
import numpy as np

TRAINING_MAPS = [
    ("warehouse-10-20-10-2-1", "warehouse-10-20-10-2-1.map", "warehouse-10-20-10-2-1-random-1.scen"),
    ("random-32-32-20", "random-32-32-20.map", "random-32-32-20-random-1.scen"),
    ("maze-32-32-2", "maze-32-32-2.map", "maze-32-32-2-random-1.scen"),
    ("empty-32-32", "empty-32-32.map", "empty-32-32-random-1.scen")
]

AGENT_COUNTS = [50, 100]
TIME_LIMIT = 5.0 # seconds
SOLVER = "build/baseline-release-cxx14/balance"
VALIDATOR = "build/rbl-release/rbl_validate"
OUT_DIR = "results/profiling"

def profile():
    os.makedirs(OUT_DIR, exist_ok=True)
    results = []

    print("================================================================")
    print("Running Baseline Repair Profiling Across Training Partition Maps")
    print("================================================================")
    print(f"Solver: {SOLVER}")
    print(f"Time limit per run: {TIME_LIMIT}s\n")

    for map_key, map_file, scen_file in TRAINING_MAPS:
        map_path = os.path.join("data/training/maps", map_file)
        scen_path = os.path.join("data/training/scenarios", scen_file)

        for k in AGENT_COUNTS:
            run_tag = f"profile_{map_key}_{k}a"
            paths_file = os.path.join(OUT_DIR, f"{run_tag}_paths.txt")
            stats_prefix = os.path.join(OUT_DIR, f"{run_tag}_stats")
            out_prefix = os.path.join(OUT_DIR, f"{run_tag}_out")
            log_file = os.path.join(OUT_DIR, f"{run_tag}.log")

            cmd = [
                SOLVER,
                "-m", map_path,
                "-a", scen_path,
                "-k", str(k),
                "-t", str(TIME_LIMIT),
                "--seed", "0",
                "-o", out_prefix,
                "--outputPaths", paths_file,
                "--stats", stats_prefix,
                "--screen", "1",
                "--maxIterations", "1000000"
            ]

            t0 = time.perf_counter()
            proc = subprocess.run(cmd, capture_output=True, text=True)
            t1 = time.perf_counter()
            wall_time = t1 - t0

            with open(log_file, "w") as f:
                f.write(proc.stdout)
                if proc.stderr:
                    f.write("\n--- STDERR ---\n" + proc.stderr)

            # Independent validation
            val_status = "NO_PATHS"
            indep_cost = -1
            if os.path.exists(paths_file) and os.path.getsize(paths_file) > 0:
                val_res = subprocess.run([VALIDATOR, "--map", map_path, "--scen", scen_path, "--agents", str(k), "--paths", paths_file],
                                         capture_output=True, text=True)
                try:
                    v_json = json.loads(val_res.stdout)
                    val_status = v_json.get("status", "FAIL")
                    indep_cost = v_json.get("independent_sum_of_costs", -1)
                except Exception:
                    val_status = "VAL_PARSE_ERR"

            # Parse stats CSV to compute iteration duration dynamics
            stats_csv = stats_prefix + "-LNS.csv"
            durations = []
            improvements = 0
            cost_history = []
            if os.path.exists(stats_csv):
                with open(stats_csv, "r") as f:
                    lines = [line.strip() for line in f if line.strip()]
                if len(lines) > 1:
                    prev_t = 0.0
                    prev_cost = 999999999
                    for row in lines[1:]:
                        parts = row.split(",")
                        cost = int(parts[1])
                        t = float(parts[2])
                        dt = max(0.0, t - prev_t)
                        durations.append(dt)
                        if cost < prev_cost:
                            improvements += 1
                        prev_cost = cost
                        prev_t = t

            durations_ms = np.array(durations) * 1000.0 if durations else np.array([0.0])
            total_iters = len(durations)
            failed_iters = total_iters - improvements

            metrics = {
                "map": map_key,
                "agents": k,
                "exit_code": proc.returncode,
                "wall_time_sec": wall_time,
                "val_status": val_status,
                "independent_cost": indep_cost,
                "total_iterations": total_iters,
                "accepted_improvements": improvements,
                "failed_or_rejected_iterations": failed_iters,
                "fail_rate_percent": (failed_iters / max(1, total_iters)) * 100.0,
                "mean_duration_ms": float(np.mean(durations_ms)),
                "p50_duration_ms": float(np.percentile(durations_ms, 50)),
                "p90_duration_ms": float(np.percentile(durations_ms, 90)),
                "p95_duration_ms": float(np.percentile(durations_ms, 95)),
                "p99_duration_ms": float(np.percentile(durations_ms, 99)),
                "max_duration_ms": float(np.max(durations_ms)),
                "iters_gt_1ms_percent": float(np.mean(durations_ms > 1.0) * 100.0),
                "iters_gt_5ms_percent": float(np.mean(durations_ms > 5.0) * 100.0),
                "iters_gt_10ms_percent": float(np.mean(durations_ms > 10.0) * 100.0)
            }
            results.append(metrics)

            print(f"[{map_key} | {k:3d} agents] Iters: {total_iters:6d} | Fails: {failed_iters:6d} ({metrics['fail_rate_percent']:5.1f}%) | "
                  f"Mean dt: {metrics['mean_duration_ms']:5.3f} ms | p95: {metrics['p95_duration_ms']:5.3f} ms | Max: {metrics['max_duration_ms']:5.2f} ms | "
                  f">1ms: {metrics['iters_gt_1ms_percent']:4.1f}% | Valid: {val_status}")

    summary_file = os.path.join(OUT_DIR, "training_dynamics_summary.json")
    with open(summary_file, "w") as f:
        json.dump(results, f, indent=2)

    print(f"\nProfiling complete. Summary written to {summary_file}")
    return 0

if __name__ == "__main__":
    sys.exit(profile())
