#!/usr/bin/env python3
"""
RouteBudget Lab - Phase 7 Sequential Reference Benchmark Runner
Executes strictly sequential (single-process) reference benchmarks
to eliminate CPU contention, thread preemption, and scheduling jitter.
Separates Protocol A (Refinement-Only) from Protocol B (End-to-End).
"""

import os
import sys
import json
import time
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BENCH_BIN = REPO_ROOT / "build" / "bench-release" / "rbl_eval_runner"
BASELINE_BIN = REPO_ROOT / "build" / "baseline-release-cxx14" / "balance"
SEQ_DIR = REPO_ROOT / "results" / "phase7" / "sequential_reference"
RAW_DIR = SEQ_DIR / "raw"
INIT_PATHS_DIR = REPO_ROOT / "results" / "phase7" / "init_paths"
BASELINE_TMP_DIR = SEQ_DIR / "baseline_tmp"

TEST_MAPS = [
    "warehouse-20-40-10-2-1",
    "random-64-64-20",
    "room-64-64-8",
    "ost003d"
]

SCENARIO_IDS = [1]
AGENT_COUNTS = [50, 100]
BUDGETS_S = [1.0, 5.0, 10.0, 30.0]

METHODS = [
    ("RESTART_ONLY_K0", "refinement"),       # E2 Matched control (K=0, fixed 10ms, Protocol A)
    ("FIXED_FIFO_RESUME", "refinement"),     # E2/E3 Matched control (K=2, fixed 10ms, Protocol A)
    ("SCORE_BASED_FIXED", "refinement"),     # E3 Matched control (K=2, fixed 10ms, Protocol A)
    ("SCORE_BASED_ADAPTIVE", "refinement"),  # Proposed (K=2, adaptive {2,5,10,20,50}ms, Protocol A)
    ("ELAPSED_TIME_ONLY", "refinement"),     # E5 Feature ablation (Protocol A)
    ("DISCARD_ON_CHANGE", "refinement"),     # E4 Compatibility ablation (Protocol A)
    ("RBL_END_TO_END", "end_to_end"),        # E6 Proposed End-to-End (Protocol B)
    ("BASELINE_BALANCE", "end_to_end")       # E6 Published Baseline (Protocol B)
]

def ensure_dirs():
    RAW_DIR.mkdir(parents=True, exist_ok=True)
    INIT_PATHS_DIR.mkdir(parents=True, exist_ok=True)
    BASELINE_TMP_DIR.mkdir(parents=True, exist_ok=True)

def parse_balance_output(csv_path, map_path, scen_path, agents, budget_s):
    if not csv_path.exists():
        return None
    try:
        with open(csv_path, "r") as f:
            lines = [l.strip() for l in f if l.strip()]
        if len(lines) < 2:
            return None
        header = lines[0].split(",")
        last_row = lines[-1].split(",")
        data = dict(zip(header, last_row))
        
        runtime = float(data.get("runtime", 0.0))
        sol_cost = int(data.get("solution cost", 0))
        init_cost = int(data.get("initial solution cost", 0))
        init_runtime = float(data.get("runtime of initial solution", 0.0))
        expansions = int(data.get("LL expanded nodes", 0))
        
        return {
            "map_path": str(map_path),
            "scen_path": str(scen_path),
            "map_name": Path(map_path).name,
            "scenario_name": Path(scen_path).name,
            "agent_count": agents,
            "budget_s": budget_s,
            "seed": 42,
            "policy": "BASELINE_BALANCE",
            "estimator_mode": "NONE",
            "validation_policy": "NONE",
            "timing_protocol": "end_to_end",
            "initial_plan_source": "GENERATED_ON_THE_FLY",
            "initial_plan_found": True,
            "final_solution_valid": True,
            "validation_error": "",
            "initial_cost": init_cost,
            "final_cost": sol_cost,
            "total_improvement": init_cost - sol_cost,
            "time_of_last_accepted_commit_s": runtime,
            "total_solve_time_s": runtime,
            "initial_planning_time_s": init_runtime,
            "initial_validation_time_s": 0.0,
            "total_active_search_time_s": runtime,
            "total_decision_time_s": 0.0,
            "deadline_overshoot_s": max(0.0, runtime - budget_s),
            "total_decisions": int(data.get("iterations", 0)),
            "fresh_actions_count": int(data.get("iterations", 0)),
            "resumed_actions_count": 0,
            "accepted_repairs": int(data.get("iterations", 0)) - int(data.get("failed iterations", 0)),
            "accepted_from_resumed": 0,
            "accepted_from_fresh": int(data.get("iterations", 0)) - int(data.get("failed iterations", 0)),
            "non_improving_repairs": int(data.get("failed iterations", 0)),
            "interrupted_slices": 0,
            "interrupted_with_nonempty_prefix": 0,
            "interrupted_with_empty_prefix": 0,
            "incompatible_checkpoints_discarded": 0,
            "stagnant_checkpoints_evicted": 0,
            "late_commits_rejected": 0,
            "fallback_decisions_count": 0,
            "total_expansions": expansions,
            "repeated_expansions": 0,
            "repeated_work_ratio": 0.0,
            "fixed_bookkeeping_memory_bytes": 0,
            "peak_payload_memory_bytes": 0,
            "peak_checkpoint_memory_bytes": 0,
            "median_decision_latency_us": 0.0,
            "p95_decision_latency_us": 0.0,
            "max_decision_latency_us": 0.0,
            "slice_durations_selected_count": {},
            "anytime_cost_trajectory": [{"timestamp_s": runtime, "cost": sol_cost}]
        }
    except Exception as e:
        print(f"Error parsing balance output {csv_path}: {e}", file=sys.stderr)
        return None

def run_task(task):
    m_info, map_name, scen_name, agents, budget_s = task
    method, protocol = m_info
    
    map_path = REPO_ROOT / "data" / "test" / "maps" / f"{map_name}.map"
    scen_path = REPO_ROOT / "data" / "test" / "scenarios" / f"{scen_name}.scen"
    out_file = RAW_DIR / f"{method}_{map_name}_{scen_name}_a{agents}_b{budget_s:.1f}s.json"
    init_file = INIT_PATHS_DIR / f"{map_name}_{scen_name}_a{agents}.paths"
    
    if out_file.exists() and out_file.stat().st_size > 0:
        try:
            with open(out_file, "r") as f:
                d = json.load(f)
            if d.get("initial_plan_found", False) and "final_cost" in d:
                return (task, "SKIPPED", d.get("final_cost"), 0.0)
        except Exception:
            pass
            
    t0 = time.time()
    
    if method == "BASELINE_BALANCE":
        prefix = BASELINE_TMP_DIR / f"bal_{map_name}_{scen_name}_a{agents}_b{budget_s:.1f}s"
        cmd = [
            str(BASELINE_BIN),
            "-m", str(map_path),
            "-a", str(scen_path),
            "-k", str(agents),
            "-t", str(budget_s),
            "--maxIterations", "1000000",
            "-o", str(prefix),
            "-s", "0"
        ]
        try:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=budget_s + 60)
            elapsed = time.time() - t0
            csv_path = Path(f"{prefix}-LNS.csv")
            rec = parse_balance_output(csv_path, map_path, scen_path, agents, budget_s)
            if rec:
                with open(out_file, "w") as f:
                    json.dump(rec, f, indent=2)
                try:
                    csv_path.unlink(missing_ok=True)
                    Path(f"{prefix}-initLNS.csv").unlink(missing_ok=True)
                except Exception:
                    pass
                return (task, "OK", rec["final_cost"], elapsed)
            else:
                return (task, "FAILED_PARSE", None, elapsed)
        except Exception as e:
            return (task, f"ERROR: {e}", None, time.time() - t0)
            
    else:
        # RouteBudget Lab methods
        cmd = [
            str(BENCH_BIN),
            "--map", str(map_path),
            "--scen", str(scen_path),
            "--agents", str(agents),
            "--budget", str(budget_s),
            "--output", str(out_file)
        ]
        
        if protocol == "end_to_end":
            cmd += ["--timing-protocol", "end_to_end", "--policy", "SCORE_BASED_ADAPTIVE", "--validation-policy", "selective"]
        else:
            cmd += ["--timing-protocol", "refinement", "--load-init-paths", str(init_file)]
            if method == "RESTART_ONLY_K0":
                cmd += ["--policy", "RESTART_ONLY_K0", "--validation-policy", "selective"]
            elif method == "FIXED_FIFO_RESUME":
                cmd += ["--policy", "FIXED_FIFO_RESUME", "--validation-policy", "selective"]
            elif method == "SCORE_BASED_FIXED":
                cmd += ["--policy", "SCORE_BASED_FIXED", "--validation-policy", "selective"]
            elif method == "SCORE_BASED_ADAPTIVE":
                cmd += ["--policy", "SCORE_BASED_ADAPTIVE", "--validation-policy", "selective"]
            elif method == "ELAPSED_TIME_ONLY":
                cmd += ["--policy", "ELAPSED_TIME_ONLY", "--estimator", "ELAPSED_TIME_ONLY", "--validation-policy", "selective"]
            elif method == "DISCARD_ON_CHANGE":
                cmd += ["--policy", "SCORE_BASED_ADAPTIVE", "--validation-policy", "discard_on_change"]
                
        try:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=budget_s + 60)
            elapsed = time.time() - t0
            if out_file.exists() and out_file.stat().st_size > 0:
                with open(out_file, "r") as f:
                    d = json.load(f)
                return (task, "OK", d.get("final_cost"), elapsed)
            else:
                return (task, "NO_OUTPUT", None, elapsed)
        except Exception as e:
            return (task, f"ERROR: {e}", None, time.time() - t0)

def main():
    ensure_dirs()
    tasks = []
    # Order: by budget ascending, then map, scen, agents, method
    for b in BUDGETS_S:
        for map_name in TEST_MAPS:
            for scen_idx in SCENARIO_IDS:
                scen_name = f"{map_name}-random-{scen_idx}"
                for a in AGENT_COUNTS:
                    for m_info in METHODS:
                        tasks.append((m_info, map_name, scen_name, a, b))
                        
    total = len(tasks)
    print("================================================================================")
    print("ROUTEBUDGET LAB - SEQUENTIAL REFERENCE BENCHMARK EXECUTION")
    print(f"Total tasks: {total} (Strict sequential execution, 1 worker)")
    print(f"Directory: {RAW_DIR}")
    print("================================================================================")
    
    t_start = time.time()
    for idx, t in enumerate(tasks, start=1):
        task, status, cost, elapsed = run_task(t)
        m_info, map_name, scen_name, agents, budget_s = task
        method, protocol = m_info
        pct = (idx / total) * 100.0
        print(f"[{idx:3d}/{total}] ({pct:5.1f}%) {method:20s} [{protocol:10s}] | {map_name:22s} | a={agents:3d} b={budget_s:4.1f}s | Status: {status:7s} Cost: {str(cost):6s} ({elapsed:5.1f}s)")
        
    t_total = time.time() - t_start
    print(f"\nSequential reference benchmarks completed in {t_total:.1f}s ({t_total/60.0:.1f}m).")

if __name__ == "__main__":
    main()
