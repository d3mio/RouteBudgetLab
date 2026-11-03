#!/usr/bin/env python3
"""
RouteBudget Lab - Revalidate All 24 Initial-Plan Fixtures
Iterates over all 24 initial-plan fixtures in results/phase7/init_paths/,
converts linear-coordinate representations to Moving AI format,
runs the independent validator (rbl_validate), and asserts fail-safe validity.
"""

import os
import sys
import json
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from safe_validator import (
    validate_plan_file,
    convert_linear_paths_to_movingai,
    DEFAULT_VALIDATOR
)

INIT_PATHS_DIR = REPO_ROOT / "results" / "phase7" / "init_paths"
MAPS_DIR = REPO_ROOT / "data" / "test" / "maps"
SCENS_DIR = REPO_ROOT / "data" / "test" / "scenarios"


def revalidate_all():
    fixture_files = sorted(list(INIT_PATHS_DIR.glob("*.paths")))
    print(f"Found {len(fixture_files)} initial plan fixtures in {INIT_PATHS_DIR}")
    
    records = []
    all_passed = True

    for p in fixture_files:
        name = p.stem # e.g. warehouse-20-40-10-2-1_warehouse-20-40-10-2-1-random-1_a50
        parts = name.split("_")
        # Format: <map_name>_<scen_name>_a<agents>
        # Note: map_name and scen_name can contain dashes or underscores
        # The last part is a<agents>
        agents_str = parts[-1]
        agents = int(agents_str.replace("a", ""))
        scen_name = parts[-2]
        map_name = "_".join(parts[:-2])

        map_path = MAPS_DIR / f"{map_name}.map"
        scen_path = SCENS_DIR / f"{scen_name}.scen"

        if not map_path.exists():
            print(f"ERROR: Map {map_path} does not exist for {p.name}", file=sys.stderr)
            all_passed = False
            continue
        if not scen_path.exists():
            print(f"ERROR: Scenario {scen_path} does not exist for {p.name}", file=sys.stderr)
            all_passed = False
            continue

        # Convert to temp Moving AI format
        with tempfile.NamedTemporaryFile("w", suffix=".paths", delete=False) as tmp:
            tmp_path = tmp.name

        try:
            convert_linear_paths_to_movingai(p, map_path, tmp_path)
            # Run safe validation with coordinates in (col, row) = (x, y) -> coords_xy=True
            res = validate_plan_file(map_path, scen_path, agents, tmp_path, coords_xy=True)
            
            rec = {
                "fixture_file": p.name,
                "map": map_name,
                "scenario": scen_name,
                "agents": agents,
                "is_valid": res["is_valid"],
                "status": res["status"],
                "exit_code": res["exit_code"],
                "independent_sum_of_costs": res.get("independent_sum_of_costs"),
                "makespan": res.get("makespan"),
                "issue_count": len(res.get("issues", [])),
                "error_detail": res.get("error_detail", "")
            }
            records.append(rec)
            
            status_str = "PASS" if res["is_valid"] else f"FAIL ({res['status']})"
            print(f"[{status_str:12s}] {p.name:65s} | Cost: {res.get('independent_sum_of_costs')} | Exit: {res['exit_code']}")
            if not res["is_valid"]:
                all_passed = False
                print(f"   Error: {res.get('error_detail')}", file=sys.stderr)
                for iss in res.get("issues", []):
                    print(f"   Issue: {iss}", file=sys.stderr)
        finally:
            Path(tmp_path).unlink(missing_ok=True)

    out_json = REPO_ROOT / "results" / "phase7" / "initial_fixtures_validation_report.json"
    with open(out_json, "w") as f:
        json.dump({
            "total_fixtures": len(fixture_files),
            "valid_fixtures": sum(1 for r in records if r["is_valid"]),
            "all_passed": all_passed,
            "fixtures": records
        }, f, indent=2)

    print("\n--------------------------------------------------------------------------------")
    print(f"INITIAL FIXTURE REVALIDATION: {sum(1 for r in records if r['is_valid'])}/{len(fixture_files)} valid.")
    print(f"Full report saved to {out_json}")
    print("--------------------------------------------------------------------------------")
    return all_passed


if __name__ == "__main__":
    succ = revalidate_all()
    sys.exit(0 if succ else 1)
