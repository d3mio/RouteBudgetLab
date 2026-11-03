#!/usr/bin/env python3
"""
RouteBudget Lab - Phase 7 Verified Reference Benchmark Runner
Executes sequential single-process reference benchmarks with:
  1. Complete trajectory retention for every run (outside declared planning timer).
  2. Immediate independent validation using rbl_validate via safe_validator.
  3. SHA-256 hash provenance recorded for every retained trajectory.
  4. Separate tracking of scenario identifiers, random seeds, and timing protocols.
  5. Clean incremental resumption (skips already verified runs).
"""

import os
import sys
import json
import time
import hashlib
import argparse
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))
import safe_validator

BENCH_BIN = REPO_ROOT / "build" / "bench-release" / "rbl_eval_runner"
BASELINE_BIN = REPO_ROOT / "build" / "baseline-release-cxx14" / "balance"
VERIFIED_DIR = REPO_ROOT / "results" / "phase7" / "verified_reference"
RAW_DIR = VERIFIED_DIR / "raw"
PATHS_DIR = VERIFIED_DIR / "paths"
BASELINE_TMP_DIR = VERIFIED_DIR / "baseline_tmp"
INIT_PATHS_DIR = REPO_ROOT / "results" / "phase7" / "init_paths"
MANIFEST_PATH = VERIFIED_DIR / "verified_reference_manifest.json"

TEST_MAPS = [
    "warehouse-20-40-10-2-1",
    "random-64-64-20",
    "room-64-64-8",
    "ost003d"
]

ALL_SCENARIO_IDS = [1, 2, 3]
ALL_AGENT_COUNTS = [50, 100]
ALL_BUDGETS_S = [1.0, 5.0, 10.0, 30.0]

METHODS = [
    ("RESTART_ONLY_K0", "PROTOCOL_A_CONTROLLED_REFINEMENT", 42),
    ("FIXED_FIFO_RESUME", "PROTOCOL_A_CONTROLLED_REFINEMENT", 42),
    ("SCORE_BASED_FIXED", "PROTOCOL_A_CONTROLLED_REFINEMENT", 42),
    ("SCORE_BASED_ADAPTIVE", "PROTOCOL_A_CONTROLLED_REFINEMENT", 42),
    ("ELAPSED_TIME_ONLY", "PROTOCOL_A_CONTROLLED_REFINEMENT", 42),
    ("DISCARD_ON_CHANGE", "PROTOCOL_A_CONTROLLED_REFINEMENT", 42),
    ("RBL_END_TO_END", "PROTOCOL_B_END_TO_END", 42),
    ("BASELINE_BALANCE", "PROTOCOL_B_END_TO_END", 0)
]

def ensure_dirs():
    RAW_DIR.mkdir(parents=True, exist_ok=True)
    PATHS_DIR.mkdir(parents=True, exist_ok=True)
    BASELINE_TMP_DIR.mkdir(parents=True, exist_ok=True)
    INIT_PATHS_DIR.mkdir(parents=True, exist_ok=True)

def hash_file(filepath):
    if not filepath.exists():
        return "MISSING"
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def load_manifest():
    if MANIFEST_PATH.exists():
        try:
            with open(MANIFEST_PATH, "r") as f:
                return json.load(f)
        except Exception:
            pass
    return {
        "metadata": {
            "execution_mode": "SEQUENTIAL_VERIFIED_REFERENCE",
            "concurrency_workers": 1,
            "machine_context": "Apple Silicon (macOS), sequential single-process reference",
            "status": "IN_PROGRESS"
        },
        "runs": {}
    }

def save_manifest(manifest):
    # Compute summary counts
    runs = manifest.get("runs", {})
    completed = len(runs)
    solver_valid = sum(1 for r in runs.values() if r.get("solver_reported_valid", False))
    indep_valid = sum(1 for r in runs.values() if r.get("independently_validated", False))
    retained_paths = sum(1 for r in runs.values() if r.get("retained_paths_file") and Path(r["retained_paths_file"]).exists())
    unverified = completed - indep_valid

    manifest["summary"] = {
        "total_runs_completed": completed,
        "solver_reported_valid_count": solver_valid,
        "retained_trajectories_count": retained_paths,
        "independently_validated_count": indep_valid,
        "unverified_count": unverified
    }
    
    with open(MANIFEST_PATH, "w") as f:
        json.dump(manifest, f, indent=2)

def parse_balance_output(csv_path, map_path, scen_path, agents, budget_s, seed):
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
        iterations = int(data.get("iterations", 0))
        failed_iters = int(data.get("failed iterations", 0))
        
        return {
            "map_path": str(map_path),
            "scen_path": str(scen_path),
            "map_name": Path(map_path).name,
            "scenario_name": Path(scen_path).name,
            "agent_count": agents,
            "budget_s": budget_s,
            "seed": seed,
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
            "total_decisions": iterations,
            "fresh_actions_count": iterations,
            "resumed_actions_count": 0,
            "accepted_repairs": iterations - failed_iters,
            "accepted_from_resumed": 0,
            "accepted_from_fresh": iterations - failed_iters,
            "non_improving_repairs": failed_iters,
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

def execute_run(task, manifest):
    m_info, map_name, scen_idx, agents, budget_s = task
    method, protocol, seed = m_info
    scen_name = f"{map_name}-random-{scen_idx}"
    run_id = f"{method}_{map_name}_{scen_name}_a{agents}_b{budget_s:.1f}s"
    
    # Check if already completed and independently validated in manifest
    if run_id in manifest.get("runs", {}):
        rec = manifest["runs"][run_id]
        if rec.get("process_completed", False) and rec.get("independently_validated", False) and rec.get("cost_matches", False):
            return "SKIPPED_VALIDATED", rec.get("final_cost"), 0.0

    map_path = REPO_ROOT / "data" / "test" / "maps" / f"{map_name}.map"
    scen_path = REPO_ROOT / "data" / "test" / "scenarios" / f"{scen_name}.scen"
    raw_file = RAW_DIR / f"{run_id}.json"
    paths_file = PATHS_DIR / f"{run_id}.paths"
    init_file = INIT_PATHS_DIR / f"{map_name}_{scen_name}_a{agents}.paths"

    t0 = time.time()
    
    if method == "BASELINE_BALANCE":
        prefix = BASELINE_TMP_DIR / f"bal_{run_id}"
        cmd = [
            str(BASELINE_BIN),
            "-m", str(map_path),
            "-a", str(scen_path),
            "-k", str(agents),
            "-t", str(budget_s),
            "--maxIterations", "1000000",
            "-o", str(prefix),
            "--outputPaths", str(paths_file),
            "-s", str(seed)
        ]
        try:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=budget_s + 60)
            elapsed = time.time() - t0
            csv_path = Path(f"{prefix}-LNS.csv")
            rec = parse_balance_output(csv_path, map_path, scen_path, agents, budget_s, seed)
            if not rec:
                return "FAILED_BALANCE_PARSE", None, elapsed
            
            with open(raw_file, "w") as f:
                json.dump(rec, f, indent=2)
            
            try:
                csv_path.unlink(missing_ok=True)
                Path(f"{prefix}-initLNS.csv").unlink(missing_ok=True)
            except Exception:
                pass
                
        except Exception as e:
            return f"ERROR: {e}", None, time.time() - t0

    else:
        # RouteBudget Lab methods
        cmd = [
            str(BENCH_BIN),
            "--map", str(map_path),
            "--scen", str(scen_path),
            "--agents", str(agents),
            "--budget", str(budget_s),
            "--output", str(raw_file),
            "--dump-final-paths", str(paths_file),
            "--seed", str(seed)
        ]
        if protocol == "PROTOCOL_B_END_TO_END":
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
            if not raw_file.exists() or raw_file.stat().st_size == 0:
                return "NO_JSON_OUTPUT", None, elapsed
            with open(raw_file, "r") as f:
                rec = json.load(f)
        except Exception as e:
            return f"ERROR: {e}", None, time.time() - t0

    # Retained trajectory independent validation
    sha256_hash = hash_file(paths_file) if paths_file.exists() else "MISSING"
    final_cost = rec.get("final_cost")
    
    coords_xy = (method != "BASELINE_BALANCE")
    val_res = safe_validator.validate_plan_file(
        map_path=str(map_path),
        scen_path=str(scen_path),
        num_agents=agents,
        paths_file=str(paths_file),
        reported_cost=final_cost,
        coords_xy=coords_xy
    )

    manifest_record = {
        "run_id": run_id,
        "json_file": str(raw_file),
        "method": method,
        "timing_protocol": protocol,
        "map_name": str(map_name),
        "scenario_name": str(scen_name),
        "scenario_id": scen_idx,
        "agent_count": agents,
        "budget_s": budget_s,
        "seed": seed,
        "process_completed": True,
        "solver_reported_valid": rec.get("final_solution_valid", False),
        "retained_paths_file": str(paths_file),
        "retained_paths_sha256": sha256_hash,
        "independently_validated": val_res.get("is_valid", False),
        "validation_status": val_res.get("status", "UNKNOWN"),
        "cost_matches": val_res.get("cost_matches", False),
        "final_cost": final_cost,
        "independent_cost": val_res.get("independent_sum_of_costs"),
        "deadline_overshoot_s": rec.get("deadline_overshoot_s", 0.0),
        "total_solve_time_s": rec.get("total_solve_time_s", 0.0),
        "repeated_work_ratio": rec.get("repeated_work_ratio", 0.0),
        "median_decision_latency_us": rec.get("median_decision_latency_us", 0.0)
    }

    manifest["runs"][run_id] = manifest_record
    save_manifest(manifest)

    status_str = "VALID" if val_res.get("is_valid", False) else f"INVALID({val_res.get('status')})"
    return status_str, final_cost, elapsed

def main():
    parser = argparse.ArgumentParser(description="Phase 7 Verified Reference Benchmark Runner")
    parser.add_argument("--scenarios", type=str, default="1", help="Comma-separated scenario IDs, e.g. '1' or '1,2,3'")
    parser.add_argument("--budgets", type=str, default="1.0,5.0,10.0,30.0", help="Comma-separated budgets, e.g. '1.0,5.0'")
    parser.add_argument("--maps", type=str, default=",".join(TEST_MAPS), help="Comma-separated map names")
    parser.add_argument("--agents", type=str, default="50,100", help="Comma-separated agent counts")
    args = parser.parse_args()

    ensure_dirs()
    manifest = load_manifest()

    scen_ids = [int(s.strip()) for s in args.scenarios.split(",") if s.strip()]
    budgets = [float(b.strip()) for b in args.budgets.split(",") if b.strip()]
    maps = [m.strip() for m in args.maps.split(",") if m.strip()]
    agents_list = [int(a.strip()) for a in args.agents.split(",") if a.strip()]

    tasks = []
    # Order: ascending budget, map, scen, agents, method
    for b in budgets:
        for map_name in maps:
            for s_id in scen_ids:
                for a in agents_list:
                    for m_info in METHODS:
                        tasks.append((m_info, map_name, s_id, a, b))

    total = len(tasks)
    print("================================================================================")
    print("ROUTEBUDGET LAB - VERIFIED REFERENCE BENCHMARK EXECUTION")
    print(f"Total planned tasks: {total} (Strict sequential single-worker execution)")
    print(f"Scenarios: {scen_ids} | Budgets: {budgets}s | Agents: {agents_list}")
    print(f"Output Directory: {VERIFIED_DIR}")
    print("================================================================================")

    t_start = time.time()
    for idx, t in enumerate(tasks, start=1):
        m_info, map_name, scen_idx, a, b = t
        method, protocol, seed = m_info
        status, cost, elapsed = execute_run(t, manifest)
        pct = (idx / total) * 100.0
        print(f"[{idx:3d}/{total}] ({pct:5.1f}%) {method:20s} | {map_name:22s} | scen={scen_idx} a={a:3d} b={b:4.1f}s | Val: {status:10s} Cost: {str(cost):6s} ({elapsed:5.1f}s)")

    t_total = time.time() - t_start
    print(f"\nExecution finished in {t_total:.1f}s ({t_total/60.0:.1f}m).")
    save_manifest(manifest)

if __name__ == "__main__":
    main()
