#!/usr/bin/env python3
"""
RouteBudget Lab - Validate Sample Final Solutions
Executes representative final solution runs across all 4 benchmark maps,
dumps the full final path solutions to disk, and runs the independent
validator (rbl_validate) via safe_validator to independently verify
that final solution paths produced by the RBL engine are strictly valid MAPF solutions.
"""

import os
import sys
import json
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from safe_validator import validate_plan_file, DEFAULT_VALIDATOR

BENCH_BIN = REPO_ROOT / "build" / "bench-release" / "rbl_eval_runner"
INIT_PATHS_DIR = REPO_ROOT / "results" / "phase7" / "init_paths"
MAPS_DIR = REPO_ROOT / "data" / "test" / "maps"
SCENS_DIR = REPO_ROOT / "data" / "test" / "scenarios"
OUT_DIR = REPO_ROOT / "results" / "phase7" / "sample_final_validation"

TEST_CONDITIONS = [
    ("warehouse-20-40-10-2-1", "warehouse-20-40-10-2-1-random-1", 50, 1.0),
    ("random-64-64-20", "random-64-64-20-random-1", 50, 1.0),
    ("room-64-64-8", "room-64-64-8-random-1", 50, 1.0),
    ("ost003d", "ost003d-random-1", 50, 1.0)
]

def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    print("================================================================================")
    print("ROUTEBUDGET LAB - SAMPLE FINAL SOLUTION INDEPENDENT VALIDATION")
    print(f"Executing representative final runs and verifying with {DEFAULT_VALIDATOR}")
    print("================================================================================")

    records = []
    all_valid = True

    for map_name, scen_name, agents, budget in TEST_CONDITIONS:
        map_path = MAPS_DIR / f"{map_name}.map"
        scen_path = SCENS_DIR / f"{scen_name}.scen"
        init_file = INIT_PATHS_DIR / f"{map_name}_{scen_name}_a{agents}.paths"
        final_paths_file = OUT_DIR / f"final_{map_name}_{scen_name}_a{agents}.paths"
        out_json = OUT_DIR / f"final_{map_name}_{scen_name}_a{agents}.json"

        cmd = [
            str(BENCH_BIN),
            "--map", str(map_path),
            "--scen", str(scen_path),
            "--agents", str(agents),
            "--budget", str(budget),
            "--policy", "SCORE_BASED_ADAPTIVE",
            "--load-init-paths", str(init_file),
            "--dump-final-paths", str(final_paths_file),
            "--output", str(out_json)
        ]

        print(f"Running solver on {map_name} (a={agents}, b={budget}s)...")
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            print(f"ERROR: Solver failed: {proc.stderr}", file=sys.stderr)
            all_valid = False
            continue

        with open(out_json, "r") as f:
            sol_data = json.load(f)

        reported_cost = sol_data.get("final_cost")

        # Independent validation
        val_res = validate_plan_file(
            map_path=map_path,
            scen_path=scen_path,
            num_agents=agents,
            paths_file=final_paths_file,
            reported_cost=reported_cost,
            coords_xy=True
        )

        status_str = "PASS" if val_res["is_valid"] else "FAIL"
        print(f"[{status_str:4s}] {map_name:24s} | Reported: {reported_cost} | Indep: {val_res.get('independent_sum_of_costs')} | Exit: {val_res['exit_code']}")
        if not val_res["is_valid"]:
            all_valid = False
            print(f"  Error: {val_res.get('error_detail')}", file=sys.stderr)
            for iss in val_res.get("issues", []):
                print(f"  Issue: {iss}", file=sys.stderr)

        records.append({
            "map_name": map_name,
            "scenario_name": scen_name,
            "agents": agents,
            "budget_s": budget,
            "reported_final_cost": reported_cost,
            "independent_cost": val_res.get("independent_sum_of_costs"),
            "is_valid": val_res["is_valid"],
            "status": val_res["status"],
            "exit_code": val_res["exit_code"],
            "error_detail": val_res.get("error_detail", "")
        })

    report_file = REPO_ROOT / "results" / "phase7" / "sample_final_validation_report.json"
    with open(report_file, "w") as f:
        json.dump({
            "all_valid": all_valid,
            "total_tested": len(records),
            "results": records
        }, f, indent=2)

    print("\n--------------------------------------------------------------------------------")
    print(f"SAMPLE FINAL SOLUTION REVALIDATION: {sum(1 for r in records if r['is_valid'])}/{len(records)} valid.")
    print(f"Report saved to: {report_file}")
    print("--------------------------------------------------------------------------------")
    return all_valid

if __name__ == "__main__":
    succ = main()
    sys.exit(0 if succ else 1)
