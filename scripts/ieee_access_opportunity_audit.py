#!/usr/bin/env python3
"""
ieee_access_opportunity_audit.py
Runs offline_snapshots across the 4 unseen Moving AI test maps to measure:
- Total interrupted slices with completed prefix
- Slices with positive admissible prefix gain (Delta_max > 0)
- Slices with Delta_max > 0 AND minimal external blocker count |B| <= 2
  (broken down by |B|=0, |B|=1, |B|=2, |B|>=3)
"""

import csv
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BIN_PROFILER = REPO_ROOT / "build" / "reconcile_release" / "rbl_profile_partial_acceptance"
OUT_DIR = REPO_ROOT / "results" / "ieee_access_final" / "opportunity_audit"

UNSEEN_MAPS = [
    ("room-64-64-8", REPO_ROOT / "data/test/maps/room-64-64-8.map"),
    ("random-64-64-20", REPO_ROOT / "data/test/maps/random-64-64-20.map"),
    ("warehouse-20-40-10-2-1", REPO_ROOT / "data/test/maps/warehouse-20-40-10-2-1.map"),
    ("ost003d", REPO_ROOT / "data/test/maps/ost003d.map"),
]
SCEN_IDS = [1, 2]
AGENTS = [50, 100]


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    rows = []
    totals = {
        "snapshots": 0,
        "pos_admissible": 0,
        "pos_and_b_le_2": 0,
        "b0": 0,
        "b1": 0,
        "b2": 0,
        "b_ge_3": 0,
        "exact_pos_opps": 0,
        "exact_gain_sum": 0,
    }

    for map_name, map_path in UNSEEN_MAPS:
        for sid in SCEN_IDS:
            scen_path = REPO_ROOT / f"data/test/scenarios/{map_name}-random-{sid}.scen"
            for a in AGENTS:
                if map_name == "room-64-64-8" and sid == 2 and a == 100:
                    continue  # Sequential PP infeasible at agent 72 on room-64-64-8-random-2
                out_json = OUT_DIR / f"{map_name}_s{sid}_a{a}.json"
                if not out_json.exists():
                    cmd = [
                        str(BIN_PROFILER),
                        "--map", str(map_path),
                        "--scen", str(scen_path),
                        "--agents", str(a),
                        "--budget", "1.0",
                        "--slice", "0.005",
                        "--seed", "0",
                        "--mode", "offline_snapshots",
                        "--output", str(out_json),
                    ]
                    res = subprocess.run(cmd, capture_output=True, text=True)
                    if res.returncode != 0:
                        print(f"ERROR on {map_name} s{sid} a{a}: {res.stderr}", file=sys.stderr)
                        sys.exit(1)

                d = json.loads(out_json.read_text())
                snaps = d.get("snapshots", [])
                n_snaps = len(snaps)
                pos_adm = [s for s in snaps if s.get("admissible_gain_u", 0) > 0]
                b0 = sum(1 for s in pos_adm if s.get("pos_blocker_count", 0) == 0)
                b1 = sum(1 for s in pos_adm if s.get("pos_blocker_count", 0) == 1)
                b2 = sum(1 for s in pos_adm if s.get("pos_blocker_count", 0) == 2)
                b_ge_3 = sum(1 for s in pos_adm if s.get("pos_blocker_count", 0) >= 3)
                b_le_2 = b0 + b1 + b2
                exact_pos = sum(1 for s in snaps if s.get("exact_gain", 0) > 0)
                exact_gain = sum(s.get("exact_gain", 0) for s in snaps)

                rows.append({
                    "map": map_name,
                    "scen": f"{map_name}-random-{sid}.scen",
                    "agents": a,
                    "total_attempts": d["total_repair_attempts"],
                    "total_interruptions": d["total_interruptions"],
                    "snapshots_with_prefix": n_snaps,
                    "pos_admissible_count": len(pos_adm),
                    "pos_admissible_pct": round(100.0 * len(pos_adm) / n_snaps, 3) if n_snaps else 0.0,
                    "pos_and_b_le_2": b_le_2,
                    "pos_and_b_le_2_pct": round(100.0 * b_le_2 / n_snaps, 3) if n_snaps else 0.0,
                    "b0_count": b0,
                    "b1_count": b1,
                    "b2_count": b2,
                    "b_ge_3_count": b_ge_3,
                    "exact_pos_opps": exact_pos,
                    "exact_gain_sum": exact_gain,
                })
                totals["snapshots"] += n_snaps
                totals["pos_admissible"] += len(pos_adm)
                totals["pos_and_b_le_2"] += b_le_2
                totals["b0"] += b0
                totals["b1"] += b1
                totals["b2"] += b2
                totals["b_ge_3"] += b_ge_3
                totals["exact_pos_opps"] += exact_pos
                totals["exact_gain_sum"] += exact_gain

    csv_path = REPO_ROOT / "results" / "ieee_access_final" / "opportunity_audit_summary.csv"
    with open(csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    summary_path = REPO_ROOT / "results" / "ieee_access_final" / "opportunity_audit_summary.json"
    summary_path.write_text(json.dumps({"totals": totals, "per_cell": rows}, indent=2))
    print("Opportunity Audit Totals:", json.dumps(totals, indent=2))


if __name__ == "__main__":
    main()
