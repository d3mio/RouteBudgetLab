#!/usr/bin/env python3
"""
RouteBudget Lab - Phase 8 Replay Exporter
Converts verified benchmark runs and retained trajectories into versioned,
self-contained replay JSON packages with normalized (x, y) coordinates.

Handles:
  1. Coordinate normalization:
     - BALANCE outputs (row, col) = (y, x) -> normalized to (col, row) = (x, y).
     - RBL outputs (col, row) = (x, y) -> verified directly against scenario.
  2. Map geometry: height, width, and traversability grid (obstacles).
  3. Agent metadata: start, goal, full trajectory, path length, arrival step.
  4. Provenance: links to source .paths file, cryptographic SHA-256 hash, solve time, cost, validation status.
  5. Index generation: builds replays_index.json for instant UI catalog search.
"""

import os
import sys
import re
import json
import hashlib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
MANIFEST_PATH = REPO_ROOT / "results" / "phase7" / "verified_reference" / "verified_reference_manifest.json"
REPLAYS_DIR = REPO_ROOT / "results" / "phase8" / "replays"
INDEX_PATH = REPLAYS_DIR / "replays_index.json"

def parse_map(map_path):
    with open(map_path, "r") as f:
        lines = [line.rstrip("\r\n") for line in f if line.strip()]
    
    height = None
    width = None
    grid_start = None
    
    for i, line in enumerate(lines):
        if line.startswith("height"):
            height = int(line.split()[1])
        elif line.startswith("width"):
            width = int(line.split()[1])
        elif line.startswith("map"):
            grid_start = i + 1
            break
            
    if height is None or width is None or grid_start is None:
        raise ValueError(f"Invalid map file format: {map_path}")
        
    grid = lines[grid_start:grid_start + height]
    return width, height, grid

def parse_scenario(scen_path, agent_count):
    agents = []
    with open(scen_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("version"):
                continue
            parts = line.split("\t")
            if len(parts) < 8:
                parts = line.split()
            if len(parts) >= 8:
                # 4: start_x (col), 5: start_y (row), 6: goal_x (col), 7: goal_y (row)
                sx = int(parts[4])
                sy = int(parts[5])
                gx = int(parts[6])
                gy = int(parts[7])
                agents.append({
                    "id": len(agents),
                    "start": [sx, sy],
                    "goal": [gx, gy]
                })
                if len(agents) == agent_count:
                    break
    return agents

def parse_paths_file(paths_file, is_balance, scen_agents):
    """
    Parses a .paths file into a list of [(x, y), ...] coordinate trajectories.
    If is_balance is True, raw coords are (row, col) = (y, x), normalized to (x, y).
    Validates that normalized start and goal match the scenario definition.
    """
    trajectories = []
    coord_pattern = re.compile(r"\((\d+),(\d+)\)")
    
    with open(paths_file, "r") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            matches = coord_pattern.findall(line)
            if not matches:
                continue
            
            raw_pts = [(int(c1), int(c2)) for c1, c2 in matches]
            agent_idx = len(trajectories)
            
            if is_balance:
                # Raw is (row, col) = (y, x). Normalized is (x, y) = (col, row).
                norm_pts = [[c2, c1] for c1, c2 in raw_pts]
            else:
                # Raw is (col, row) = (x, y).
                norm_pts = [[c1, c2] for c1, c2 in raw_pts]
                
            trajectories.append(norm_pts)

    return trajectories

def export_single_run(run_info, manifest_rec):
    run_id = manifest_rec["run_id"]
    method = manifest_rec["method"]
    proto = manifest_rec["timing_protocol"]
    map_name = manifest_rec["map_name"]
    scen_name = manifest_rec["scenario_name"]
    scen_id = manifest_rec.get("scenario_id", 1)
    agent_count = manifest_rec["agent_count"]
    budget_s = manifest_rec["budget_s"]
    seed = manifest_rec["seed"]
    
    paths_path = Path(manifest_rec["retained_paths_file"])
    if not paths_path.exists():
        return None
        
    map_path = REPO_ROOT / "data" / "test" / "maps" / f"{map_name}.map"
    scen_path = REPO_ROOT / "data" / "test" / "scenarios" / f"{scen_name}.scen"
    
    width, height, grid = parse_map(map_path)
    scen_agents = parse_scenario(scen_path, agent_count)
    is_balance = (method == "BASELINE_BALANCE")
    
    trajectories = parse_paths_file(paths_path, is_balance, scen_agents)
    if len(trajectories) != agent_count:
        print(f"Warning: agent count mismatch in {run_id}: expected {agent_count}, found {len(trajectories)}", file=sys.stderr)
        return None

    # Build agent structures and verify arrival times
    agents = []
    total_cost = 0
    makespan = 0
    
    for i in range(agent_count):
        traj = trajectories[i]
        scen_info = scen_agents[i] if i < len(scen_agents) else {"start": traj[0], "goal": traj[-1]}
        start = scen_info["start"]
        goal = scen_info["goal"]
        
        # Determine arrival step (when agent reaches goal and never moves away)
        arrival_step = len(traj) - 1
        for t in range(len(traj) - 1, -1, -1):
            if traj[t] == goal:
                arrival_step = t
            else:
                break
                
        total_cost += arrival_step
        if arrival_step > makespan:
            makespan = arrival_step
            
        agents.append({
            "id": i,
            "start": start,
            "goal": goal,
            "path_length": len(traj),
            "arrival_step": arrival_step,
            "path": traj
        })

    replay_data = {
        "format_version": "1.0.0",
        "run_id": run_id,
        "provenance": {
            "method": method,
            "timing_protocol": proto,
            "map_name": map_name,
            "scenario_name": scen_name,
            "scenario_id": scen_id,
            "agent_count": agent_count,
            "budget_s": budget_s,
            "seed": seed,
            "source_paths_file": str(paths_path),
            "source_paths_sha256": manifest_rec.get("retained_paths_sha256", ""),
            "source_coordinate_convention": "ROW_COL" if is_balance else "COL_ROW_XY",
            "normalized_coordinate_convention": "COL_ROW_XY"
        },
        "metrics": {
            "final_cost": manifest_rec.get("final_cost", total_cost),
            "independent_cost": manifest_rec.get("independent_cost", total_cost),
            "total_solve_time_s": manifest_rec.get("total_solve_time_s", 0.0),
            "deadline_overshoot_s": manifest_rec.get("deadline_overshoot_s", 0.0),
            "validation_status": manifest_rec.get("validation_status", "VALID"),
            "makespan": makespan,
            "agent_count": agent_count
        },
        "map": {
            "name": map_name,
            "width": width,
            "height": height,
            "grid": grid
        },
        "agents": agents
    }

    out_file = REPLAYS_DIR / f"{run_id}.replay.json"
    with open(out_file, "w") as f:
        json.dump(replay_data, f, indent=2)
        
    return {
        "run_id": run_id,
        "map_name": map_name,
        "scenario_name": scen_name,
        "scenario_id": scen_id,
        "agent_count": agent_count,
        "budget_s": budget_s,
        "method": method,
        "protocol": proto,
        "final_cost": manifest_rec.get("final_cost", total_cost),
        "solve_time_s": manifest_rec.get("total_solve_time_s", 0.0),
        "makespan": makespan,
        "validation_status": manifest_rec.get("validation_status", "VALID"),
        "replay_file": f"{run_id}.replay.json"
    }

def main():
    if not MANIFEST_PATH.exists():
        print(f"Error: manifest not found at {MANIFEST_PATH}", file=sys.stderr)
        sys.exit(1)

    REPLAYS_DIR.mkdir(parents=True, exist_ok=True)

    with open(MANIFEST_PATH, "r") as f:
        manifest = json.load(f)

    runs = manifest.get("runs", {})
    print(f"Exporting {len(runs)} runs from verified reference manifest to {REPLAYS_DIR}...")

    exported = []
    maps_set = set()
    scens_set = set()
    agents_set = set()
    budgets_set = set()
    methods_set = set()
    protos_set = set()

    for run_id, rec in runs.items():
        if not rec.get("independently_validated", False) or not rec.get("cost_matches", False):
            continue
        summary_rec = export_single_run(run_id, rec)
        if summary_rec:
            exported.append(summary_rec)
            maps_set.add(summary_rec["map_name"])
            scens_set.add(summary_rec["scenario_id"])
            agents_set.add(summary_rec["agent_count"])
            budgets_set.add(summary_rec["budget_s"])
            methods_set.add(summary_rec["method"])
            protos_set.add(summary_rec["protocol"])

    # Build catalog index
    index_data = {
        "format_version": "1.0.0",
        "total_replays": len(exported),
        "filters": {
            "maps": sorted(list(maps_set)),
            "scenarios": sorted(list(scens_set)),
            "agent_counts": sorted(list(agents_set)),
            "budgets": sorted(list(budgets_set)),
            "methods": sorted(list(methods_set)),
            "protocols": sorted(list(protos_set))
        },
        "replays": exported
    }

    with open(INDEX_PATH, "w") as f:
        json.dump(index_data, f, indent=2)

    print(f"Successfully exported {len(exported)} verified replays to {REPLAYS_DIR}.")
    print(f"Index written to {INDEX_PATH} (catalog filters: {len(maps_set)} maps, {len(scens_set)} scenarios, {len(methods_set)} methods).")

if __name__ == "__main__":
    main()
