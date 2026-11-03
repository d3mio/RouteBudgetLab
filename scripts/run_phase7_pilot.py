#!/usr/bin/env python3
"""
RouteBudget Lab - Phase 7 Representative Development Pilot (Training & Validation Maps)
Runs 50 and 100 agents on training and validation maps under matched policies:
  - RESTART_ONLY_K0 (K=0, fixed 10ms)
  - FIXED_FIFO_RESUME (K=2, fixed 10ms)
  - SCORE_BASED_FIXED (K=2, fixed 10ms matched control)
  - SCORE_BASED_ADAPTIVE (K=2, variable slice grid {2, 5, 10, 20, 50}ms)
  - ELAPSED_TIME_ONLY (K=2, ablation with masked progress features)

Ensures 100% matched initial solution by precomputing and sharing initial paths.
Saves immutable per-run JSON records to results/pilot/ and writes summary table.
"""

import os
import sys
import json
import subprocess
import glob
import time

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RUNNER = os.path.join(REPO_ROOT, "build/bench-release/rbl_eval_runner")
OUT_DIR = os.path.join(REPO_ROOT, "results/pilot")
INIT_DIR = os.path.join(REPO_ROOT, "results/pilot/init_paths")
os.makedirs(OUT_DIR, exist_ok=True)
os.makedirs(INIT_DIR, exist_ok=True)

PILOT_CASES = [
    # Training Maps
    {
        "map": "data/training/maps/random-32-32-20.map",
        "scen": "data/training/scenarios/random-32-32-20-random-1.scen",
        "tag": "training_random32",
        "agents_list": [50, 100],
        "budgets": [1.0, 5.0]
    },
    {
        "map": "data/training/maps/warehouse-10-20-10-2-1.map",
        "scen": "data/training/scenarios/warehouse-10-20-10-2-1-random-1.scen",
        "tag": "training_warehouse10",
        "agents_list": [50, 100],
        "budgets": [1.0, 5.0]
    },
    {
        "map": "data/training/maps/maze-32-32-2.map",
        "scen": "data/training/scenarios/maze-32-32-2-random-1.scen",
        "tag": "training_maze32",
        "agents_list": [50, 100],
        "budgets": [1.0, 5.0]
    },
    # Validation Maps
    {
        "map": "data/validation/maps/warehouse-10-20-10-2-2.map",
        "scen": "data/validation/scenarios/warehouse-10-20-10-2-2-random-1.scen",
        "tag": "validation_warehouse10",
        "agents_list": [50, 100],
        "budgets": [1.0, 5.0]
    },
    {
        "map": "data/validation/maps/random-64-64-10.map",
        "scen": "data/validation/scenarios/random-64-64-10-random-1.scen",
        "tag": "validation_random64",
        "agents_list": [50, 100],
        "budgets": [1.0, 5.0]
    }
]

POLICIES = [
    "RESTART_ONLY_K0",
    "FIXED_FIFO_RESUME",
    "SCORE_BASED_FIXED",
    "SCORE_BASED_ADAPTIVE",
    "ELAPSED_TIME_ONLY"
]

def run_cmd(cmd):
    env = os.environ.copy()
    env["PATH"] = "/Library/Developer/CommandLineTools/usr/bin:" + env.get("PATH", "")
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
    return p.returncode, p.stdout, p.stderr

def run_pilot():
    print("================================================================================")
    print("Starting RouteBudget Lab Phase 7 Development Pilot (50 & 100 Agents)")
    print("================================================================================")
    
    all_results = []
    
    for case in PILOT_CASES:
        map_path = os.path.join(REPO_ROOT, case["map"])
        scen_path = os.path.join(REPO_ROOT, case["scen"])
        map_name = os.path.basename(map_path)
        
        for agents in case["agents_list"]:
            init_file = os.path.join(INIT_DIR, f"{case['tag']}_{agents}a_init.paths")
            
            # Step 1: Ensure initial plan is computed once and cached
            if not os.path.exists(init_file):
                print(f"[*] Precomputing matched initial plan for {map_name} ({agents} agents)...")
                cmd = [
                    RUNNER,
                    "--map", map_path,
                    "--scen", scen_path,
                    "--agents", str(agents),
                    "--budget", "0.01",
                    "--policy", "RESTART_ONLY_K0",
                    "--dump-init-paths", init_file
                ]
                rc, stdout, stderr = run_cmd(cmd)
                if not os.path.exists(init_file) or os.path.getsize(init_file) == 0:
                    print(f"[-] Initial planning failed or timed out for {map_name} with {agents} agents. Skipping.")
                    continue
                print(f"[+] Initial plan saved to {os.path.basename(init_file)}")
                
            for budget in case["budgets"]:
                for pol in POLICIES:
                    run_tag = f"{case['tag']}_{agents}a_{int(budget)}s_{pol}"
                    json_out = os.path.join(OUT_DIR, f"{run_tag}.json")
                    
                    if os.path.exists(json_out) and os.path.getsize(json_out) > 50:
                        print(f"[CACHE] {run_tag} already completed.")
                        with open(json_out, "r") as f:
                            all_results.append(json.load(f))
                        continue
                    
                    print(f"[RUN] {map_name} | {agents} ag | {budget}s | {pol} ... ", end="", flush=True)
                    t0 = time.time()
                    
                    cmd = [
                        RUNNER,
                        "--map", map_path,
                        "--scen", scen_path,
                        "--agents", str(agents),
                        "--budget", str(budget),
                        "--policy", pol,
                        "--load-init-paths", init_file,
                        "--output", json_out
                    ]
                    rc, stdout, stderr = run_cmd(cmd)
                    elapsed = time.time() - t0
                    
                    if os.path.exists(json_out) and os.path.getsize(json_out) > 50:
                        with open(json_out, "r") as f:
                            data = json.load(f)
                            all_results.append(data)
                            valid = "VALID" if data.get("final_solution_valid") else "INVALID"
                            cost = data.get("final_cost", 0)
                            imp = data.get("total_improvement", 0)
                            res_cnt = data.get("resumed_actions_count", 0)
                            intr_cnt = data.get("interrupted_slices", 0)
                            nonempty = data.get("interrupted_with_nonempty_prefix", 0)
                            ovh = data.get("deadline_overshoot_s", 0.0) * 1000.0
                            rep = data.get("repeated_work_ratio", 0.0) * 100.0
                            print(f"DONE in {elapsed:.2f}s -> {valid}, cost={cost} (imp={imp}), resumed={res_cnt}, intr={intr_cnt} (nonempty={nonempty}), rep={rep:.1f}%, ovh={ovh:.2f}ms")
                    else:
                        print(f"FAILED (rc={rc}): {stderr[:150]}")
    
    # Save combined summary
    summary_path = os.path.join(REPO_ROOT, "results/pilot_summary.json")
    with open(summary_path, "w") as f:
        json.dump(all_results, f, indent=2)
    print(f"\n[+] Saved {len(all_results)} pilot run records to {summary_path}")

if __name__ == "__main__":
    run_pilot()
