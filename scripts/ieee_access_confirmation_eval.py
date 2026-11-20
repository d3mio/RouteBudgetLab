#!/usr/bin/env python3
"""
ieee_access_confirmation_eval.py
Controlled unseen-map confirmation evaluation across 4 Moving AI test maps:
- room-64-64-8, random-64-64-20, warehouse-20-40-10-2-1, ost003d
- 2 scenarios each (random-1, random-2), agents {50, 100} (excluding infeasible room s2 a100),
  budgets {1.0s, 5.0s}, seeds {0, 1} -> 60 matched conditions x 4 policies = 240 sequential runs.
- Policies: DISCARD_NO_PA, RESUMPTION_NO_PA, UNINTERRUPTED_REPAIR, GATED_BLOCKER_CLOSURE
- Dual validation: C++ rbl_validate AND Python safe_validator.py on every final plan.
"""

import csv
import hashlib
import json
import random
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))
import safe_validator

BIN_PROFILER = REPO_ROOT / "build" / "reconcile_release" / "rbl_profile_partial_acceptance"
BIN_VALIDATOR = REPO_ROOT / "build" / "reconcile_release" / "rbl_validate"

RES_DIR = REPO_ROOT / "results" / "ieee_access_final"
RAW_DIR = RES_DIR / "raw"
PATHS_DIR = RES_DIR / "paths"
INIT_DIR = RES_DIR / "init_paths"
VAL_DIR = RES_DIR / "validation"

MAPS = [
    ("room-64-64-8", REPO_ROOT / "data/test/maps/room-64-64-8.map"),
    ("random-64-64-20", REPO_ROOT / "data/test/maps/random-64-64-20.map"),
    ("warehouse-20-40-10-2-1", REPO_ROOT / "data/test/maps/warehouse-20-40-10-2-1.map"),
    ("ost003d", REPO_ROOT / "data/test/maps/ost003d.map"),
]
SCEN_IDS = [1, 2]
AGENTS = [50, 100]
BUDGETS = [1.0, 5.0]
SEEDS = [0, 1]
POLICIES = [
    "DISCARD_NO_PA",
    "RESUMPTION_NO_PA",
    "UNINTERRUPTED_REPAIR",
    "GATED_BLOCKER_CLOSURE",
]


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()


def precompute_inits():
    INIT_DIR.mkdir(parents=True, exist_ok=True)
    inits = {}
    for map_name, map_path in MAPS:
        for sid in SCEN_IDS:
            scen_path = REPO_ROOT / f"data/test/scenarios/{map_name}-random-{sid}.scen"
            for a in AGENTS:
                if map_name == "room-64-64-8" and sid == 2 and a == 100:
                    continue
                init_file = INIT_DIR / f"{map_name}__s{sid}__a{a}.init"
                if not init_file.exists():
                    cmd = [
                        str(BIN_PROFILER),
                        "--map", str(map_path),
                        "--scen", str(scen_path),
                        "--agents", str(a),
                        "--budget", "0.001",
                        "--policy", "DISCARD_NO_PA",
                        "--dump-init-paths", str(init_file),
                        "--output", "/tmp/ieee_init_tmp.json",
                    ]
                    res = subprocess.run(cmd, capture_output=True, text=True)
                    if res.returncode != 0 or not init_file.exists():
                        print(f"FATAL init {map_name} s{sid} a{a}: {res.stderr}", file=sys.stderr)
                        sys.exit(1)
                inits[(map_name, sid, a)] = (map_path, scen_path, init_file, sha256_file(init_file))
    print(f"Precomputed {len(inits)} shared initial plans.", flush=True)
    return inits


def main():
    for d in (RAW_DIR, PATHS_DIR, INIT_DIR, VAL_DIR):
        d.mkdir(parents=True, exist_ok=True)

    inits = precompute_inits()
    cells = []
    for (map_name, sid, a), (map_path, scen_path, init_file, init_hash) in sorted(inits.items()):
        for budget in BUDGETS:
            for seed in SEEDS:
                cells.append((map_name, map_path, sid, scen_path, a, init_file, init_hash, budget, seed))

    rng = random.Random(42)
    rng.shuffle(cells)

    runs = []
    for c_idx, (map_name, map_path, sid, scen_path, a, init_file, init_hash, budget, seed) in enumerate(cells):
        pol_order = list(POLICIES)
        rng.shuffle(pol_order)
        for pol in pol_order:
            run_id = f"{map_name}__s{sid}__a{a}__b{budget}__seed{seed}__{pol}"
            runs.append({
                "run_id": run_id,
                "cell_idx": c_idx,
                "map": map_name,
                "map_path": str(map_path),
                "scen": f"{map_name}-random-{sid}.scen",
                "scen_path": str(scen_path),
                "agents": a,
                "budget_s": budget,
                "seed": seed,
                "policy": pol,
                "init_file": str(init_file),
                "init_hash": init_hash,
            })

    total = len(runs)
    print(f"Starting {total} sequential unseen-map runs...", flush=True)
    t0_wall = time.time()
    py_val_cache = {}

    for idx, r in enumerate(runs):
        out_json = RAW_DIR / f"{r['run_id']}.json"
        out_paths = PATHS_DIR / f"{r['run_id']}.paths"
        val_json = VAL_DIR / f"{r['run_id']}.val.json"

        if not (out_json.exists() and out_paths.exists() and val_json.exists()):
            cmd = [
                str(BIN_PROFILER),
                "--map", r["map_path"],
                "--scen", r["scen_path"],
                "--agents", str(r["agents"]),
                "--budget", str(r["budget_s"]),
                "--slice", "0.005",
                "--seed", str(r["seed"]),
                "--policy", r["policy"],
                "--load-init-paths", r["init_file"],
                "--dump-final-paths", str(out_paths),
                "--output", str(out_json),
            ]
            res = subprocess.run(cmd, capture_output=True, text=True)
            if res.returncode != 0:
                print(f"FATAL solver {r['run_id']}: {res.stderr}", file=sys.stderr)
                sys.exit(1)

            # C++ independent validator
            vcmd = [
                str(BIN_VALIDATOR),
                "--map", r["map_path"],
                "--scen", r["scen_path"],
                "--agents", str(r["agents"]),
                "--paths", str(out_paths),
                "--coords", "rc",
                "--json", str(val_json),
            ]
            vres = subprocess.run(vcmd, capture_output=True, text=True)
            if vres.returncode != 0:
                print(f"FATAL C++ validator {r['run_id']}: {vres.stdout} {vres.stderr}", file=sys.stderr)
                sys.exit(1)

        # Python safe_validator check on every run
        py_rep = safe_validator.validate_plan_file(
            r["map_path"],
            r["scen_path"],
            r["agents"],
            str(out_paths),
            coords_xy=False,
            validator_bin=str(BIN_VALIDATOR),
        )
        if py_rep.get("status") != "VALID":
            print(f"FATAL Python validator {r['run_id']}: {py_rep}", file=sys.stderr)
            sys.exit(1)

        if (idx + 1) % 24 == 0 or idx == total - 1:
            elapsed = time.time() - t0_wall
            eta = (elapsed / (idx + 1)) * (total - idx - 1)
            print(f"Progress [{idx+1}/{total}] ({100*(idx+1)/total:.1f}%) | Elapsed {elapsed/60:.1f}m | ETA {eta/60:.1f}m", flush=True)

    # Export unseen_per_run.csv
    csv_path = RES_DIR / "unseen_per_run.csv"
    csv_rows = []
    for r in runs:
        d = json.loads((RAW_DIR / f"{r['run_id']}.json").read_text())
        csv_rows.append({
            "run_id": r["run_id"],
            "cell_idx": r["cell_idx"],
            "map": r["map"],
            "scen": r["scen"],
            "num_agents": d["num_agents"],
            "time_budget_s": d["time_budget_s"],
            "seed": d["seed"],
            "policy": d.get("experiment_policy") or d.get("policy"),
            "initial_cost": d["initial_cost"],
            "cost_at_deadline": d["cost_at_deadline"],
            "final_cost": d["final_cost"],
            "total_solve_time_s": d["total_solve_time_s"],
            "deadline_overshoot_ms": d.get("deadline_overshoot_s", 0.0) * 1000.0,
            "total_iterations": d["total_iterations"],
            "resumed_actions_count": d["resumed_actions_count"],
            "fresh_actions_count": d["fresh_actions_count"],
            "pa_attempts": d.get("pa_attempts", 0),
            "pa_committed": d.get("pa_committed", 0),
            "pa_total_gain": d.get("pa_total_gain", 0),
            "filter_skips": d.get("filter_skips", 0),
            "plan_valid": d["plan_valid"],
            "initial_plan_sha256": d.get("initial_plan_sha256", r["init_hash"]),
        })
    with open(csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(csv_rows[0].keys()))
        writer.writeheader()
        writer.writerows(csv_rows)

    print(f"Completed all {total} unseen-map runs (0 C++/Python validation failures) -> {csv_path}", flush=True)


if __name__ == "__main__":
    main()
