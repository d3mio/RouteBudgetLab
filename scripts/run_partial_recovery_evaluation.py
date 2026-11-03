#!/usr/bin/env python3
"""
RouteBudget Lab - Frozen Evaluation Runner for Partial-Prefix Recovery
Isolates the research contribution of:
  Policy A: Restart-Only (Discard-on-Change)
  Policy B: Whole-Checkpoint Selective Retention
  Policy C: Partial-Prefix Recovery

Protocol:
  - Evaluation Maps: data/validation/maps/random-64-64-10.map
                     data/validation/maps/warehouse-10-20-10-2-2.map
  - Scenarios: 1, 2, 3
  - Agent Counts: 50
  - Budgets: 1.0s, 5.0s
  - Seeds: 42, 123
  - Controller: SCORE_BASED_ADAPTIVE with RIDGE_REGRESSION estimator
  - Fixed initial paths loaded from file across all 3 policies
  - Immediate independent validation of retained final trajectories via rbl_validate
"""

import os
import sys
import json
import time
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))
import safe_validator

BIN = REPO_ROOT / "build" / "bench-release" / "rbl_eval_runner"
VALIDATOR_BIN = REPO_ROOT / "build" / "bench-release" / "rbl_validate"
OUT_DIR = REPO_ROOT / "results" / "partial_recovery_evaluation"
RAW_DIR = OUT_DIR / "raw"
PATHS_DIR = OUT_DIR / "paths"
INIT_PATHS_DIR = OUT_DIR / "init_paths"

MAPS = [
    ("random-64-64-10", REPO_ROOT / "data" / "validation" / "maps" / "random-64-64-10.map"),
    ("warehouse-10-20-10-2-2", REPO_ROOT / "data" / "validation" / "maps" / "warehouse-10-20-10-2-2.map")
]

SCENARIO_IDS = [1, 2, 3]
AGENT_COUNTS = [50]
BUDGETS_S = [1.0, 5.0]
SEEDS = [42, 123]

POLICIES = [
    ("Policy_A_RestartOnly", "discard_on_change"),
    ("Policy_B_WholeSelective", "selective"),
    ("Policy_C_PartialPrefix", "partial_prefix")
]

def ensure_dirs():
    RAW_DIR.mkdir(parents=True, exist_ok=True)
    PATHS_DIR.mkdir(parents=True, exist_ok=True)
    INIT_PATHS_DIR.mkdir(parents=True, exist_ok=True)

def get_scen_path(map_name, scen_id):
    return REPO_ROOT / "data" / "validation" / "scenarios" / f"{map_name}-random-{scen_id}.scen"

def main():
    ensure_dirs()
    print("=" * 80)
    print("ROUTEBUDGET LAB - FROZEN EVALUATION OF PARTIAL-PREFIX RECOVERY")
    print(f"Maps: {[m[0] for m in MAPS]}")
    print(f"Scenarios: {SCENARIO_IDS} | Agents: {AGENT_COUNTS} | Budgets: {BUDGETS_S}s | Seeds: {SEEDS}")
    print(f"Policies: {[p[0] for p in POLICIES]}")
    print("=" * 80)

    total_runs = len(MAPS) * len(SCENARIO_IDS) * len(AGENT_COUNTS) * len(BUDGETS_S) * len(SEEDS) * len(POLICIES)
    run_idx = 0
    records = []

    for map_name, map_file in MAPS:
        for scen_id in SCENARIO_IDS:
            scen_file = get_scen_path(map_name, scen_id)
            for num_agents in AGENT_COUNTS:
                # 1. Precompute initial solution once per (map, scen, agents)
                init_path_file = INIT_PATHS_DIR / f"{map_name}_s{scen_id}_a{num_agents}.init_paths"
                if not init_path_file.exists():
                    print(f"\n[INIT] Generating shared initial paths for {map_name} scen {scen_id} a{num_agents}...")
                    dummy_json = INIT_PATHS_DIR / "dummy.json"
                    cmd_init = [
                        str(BIN),
                        "--map", str(map_file),
                        "--scen", str(scen_file),
                        "--agents", str(num_agents),
                        "--budget", "0.001",
                        "--policy", "RESTART_ONLY_K0",
                        "--dump-init-paths", str(init_path_file),
                        "--output", str(dummy_json)
                    ]
                    subprocess.run(cmd_init, check=True, stdout=subprocess.DEVNULL)
                    if dummy_json.exists(): dummy_json.unlink()

                for budget_s in BUDGETS_S:
                    for seed in SEEDS:
                        for pol_label, val_policy in POLICIES:
                            run_idx += 1
                            run_key = f"{pol_label}_{map_name}_s{scen_id}_a{num_agents}_b{budget_s:.1f}s_seed{seed}"
                            raw_json = RAW_DIR / f"{run_key}.json"
                            final_paths = PATHS_DIR / f"{run_key}.paths"

                            cmd = [
                                str(BIN),
                                "--map", str(map_file),
                                "--scen", str(scen_file),
                                "--agents", str(num_agents),
                                "--budget", str(budget_s),
                                "--policy", "SCORE_BASED_ADAPTIVE",
                                "--estimator", "RIDGE_REGRESSION",
                                "--validation-policy", val_policy,
                                "--seed", str(seed),
                                "--load-init-paths", str(init_path_file),
                                "--dump-final-paths", str(final_paths),
                                "--output", str(raw_json)
                            ]

                            print(f"[{run_idx}/{total_runs}] Running {run_key}...", end=" ", flush=True)
                            t0 = time.time()
                            proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                            elapsed = time.time() - t0

                            if proc.returncode != 0:
                                print(f"ERROR: {proc.stderr.strip()[:100]}")
                                continue

                            with open(raw_json, "r") as f:
                                data = json.load(f)

                            # Independent validation
                            val_ok = False
                            val_err = ""
                            if final_paths.exists():
                                rep = safe_validator.validate_plan_file(
                                    str(map_file),
                                    str(scen_file),
                                    num_agents,
                                    str(final_paths),
                                    reported_cost=data.get("final_cost"),
                                    validator_bin=str(VALIDATOR_BIN)
                                )
                                val_ok = rep["is_valid"]
                                if not val_ok:
                                    val_err = rep.get("error_detail", "validation failed")

                            data["run_key"] = run_key
                            data["policy_label"] = pol_label
                            data["independent_validation_passed"] = val_ok
                            data["independent_validation_error"] = val_err
                            data["host_runtime_s"] = elapsed

                            with open(raw_json, "w") as f:
                                json.dump(data, f, indent=2)

                            records.append(data)
                            print(f"done ({elapsed:.2f}s) | cost: {data.get('final_cost')} | partial_recoveries: {data.get('partial_recovery_count', 0)} | valid: {val_ok}")

    summary_file = OUT_DIR / "evaluation_summary.json"
    with open(summary_file, "w") as f:
        json.dump(records, f, indent=2)
    print("\n" + "=" * 80)
    print(f"Evaluation complete! {len(records)} runs recorded in {summary_file}")
    print("=" * 80)

if __name__ == "__main__":
    main()
