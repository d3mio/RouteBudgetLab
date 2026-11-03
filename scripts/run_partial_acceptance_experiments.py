#!/usr/bin/env python3
"""
Execute Task 8 (Offline Opportunity Analysis) and Task 9 (Matched 192-Run Live Pilot)
for RouteBudget Lab's Partial-Acceptance Extension.

Guarantees:
- Sequential execution (one process at a time) for clean reference wall-clock timing.
- Shared initial solutions per (map, scenario, num_agents) across all policies and seeds.
- Complete configuration fingerprinting and run reconciliation.
- Independent validation of every produced final plan via rbl_validate.
"""

import argparse
import hashlib
import json
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


def get_git_info(repo_root: Path) -> dict:
    try:
        commit = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=repo_root, text=True
        ).strip()
        branch = subprocess.check_output(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"], cwd=repo_root, text=True
        ).strip()
        status = subprocess.check_output(
            ["git", "status", "--porcelain"], cwd=repo_root, text=True
        ).strip()
        return {
            "commit": commit,
            "branch": branch,
            "dirty": bool(status),
        }
    except Exception as e:
        return {"error": str(e)}


def ensure_init_paths(
    repo_root: Path,
    profiler_bin: Path,
    init_dir: Path,
    map_rel: str,
    scen_rel: str,
    num_agents: int,
) -> Path:
    map_stem = Path(map_rel).stem
    scen_stem = Path(scen_rel).stem
    init_file = init_dir / f"{map_stem}__{scen_stem}__a{num_agents}.paths"
    if init_file.exists() and init_file.stat().st_size > 0:
        return init_file

    tmp_json = init_dir / f"init_{map_stem}__{scen_stem}__a{num_agents}.json"
    cmd = [
        str(profiler_bin),
        "--mode", "online",
        "--map", str(repo_root / map_rel),
        "--scen", str(repo_root / scen_rel),
        "--agents", str(num_agents),
        "--seed", "0",
        "--budget", "0.0001",
        "--slice", "0.005",
        "-k", "8",
        "--policy", "DISCARD_NO_PA",
        "--dump-init-paths", str(init_file),
        "--output", str(tmp_json),
    ]
    subprocess.run(cmd, cwd=repo_root, check=True, stdout=subprocess.DEVNULL)
    if tmp_json.exists():
        tmp_json.unlink()
    return init_file


def run_offline_opportunity_analysis(
    repo_root: Path,
    profiler_bin: Path,
    config: dict,
    init_dir: Path,
    offline_dir: Path,
) -> dict:
    raw_dir = offline_dir / "raw"
    raw_dir.mkdir(parents=True, exist_ok=True)

    factors = config["factors"]
    base_pol = config["base_repair_and_scheduling_policy"]
    maps = factors["maps"]
    agent_counts = factors["agent_counts"]
    seeds = factors["seeds"]
    slice_s = factors["slice_durations_seconds"][0]
    k = base_pol["neighborhood_size"]
    cap = base_pol["checkpoint_capacity_when_resumption_enabled"]
    collection_budget_s = 1.0

    cells = []
    for m_obj in maps:
        map_stem = m_obj["name"]
        map_rel = m_obj["path"]
        for scen_rel in m_obj["scenarios"]:
            scen_stem = Path(scen_rel).stem
            for num_agents in agent_counts:
                init_file = ensure_init_paths(
                    repo_root, profiler_bin, init_dir, map_rel, scen_rel, num_agents
                )
                for seed in seeds:
                    cells.append((map_rel, map_stem, scen_rel, scen_stem, num_agents, seed, init_file))

    print(f"[Offline Analysis] Running {len(cells)} snapshot collection & multi-selector replay cells...")
    cell_summaries = []
    agg = {
        "collection_cells": len(cells),
        "collection_budget_s": collection_budget_s,
        "slice_duration_s": slice_s,
        "neighborhood_size": k,
        "checkpoint_capacity": cap,
        "total_repair_attempts": 0,
        "total_interruptions_observed": 0,
        "total_snapshots_with_completed_prefix": 0,
        "total_candidates_evaluated": 0,
        "weight_counts": {"positive": 0, "zero": 0, "negative": 0},
        "feasible_negative_candidates": 0,
        "selected_negative_candidates_exact": 0,
        "forbidden_candidates": 0,
        "transitively_excluded_candidates": 0,
        "dependency_arcs": 0,
        "snapshots_with_positive_exact_gain": 0,
        "snapshots_with_positive_greedy_gain": 0,
        "snapshots_with_positive_nonneg_ref_gain": 0,
        "snapshots_with_positive_oracle_gain": 0,
        "snapshots_exact_gt_greedy": 0,
        "snapshots_exact_ne_nonneg_ref": 0,
        "snapshots_exact_ne_oracle": 0,
        "total_gain_no_pa": 0,
        "total_gain_greedy": 0,
        "total_gain_exact": 0,
        "total_gain_nonneg_ref": 0,
        "total_gain_exhaustive_oracle": 0,
        "by_map": {},
        "by_r_done": {},
        "positive_opportunity_examples": [],
    }

    for idx, (map_rel, map_stem, scen_rel, scen_stem, num_agents, seed, init_file) in enumerate(cells, 1):
        out_json = raw_dir / f"snap_{map_stem}__{scen_stem}__a{num_agents}__s{seed}.json"
        cmd = [
            str(profiler_bin),
            "--mode", "offline_snapshots",
            "--map", str(repo_root / map_rel),
            "--scen", str(repo_root / scen_rel),
            "--agents", str(num_agents),
            "--seed", str(seed),
            "--budget", str(collection_budget_s),
            "--slice", str(slice_s),
            "-k", str(k),
            "--policy", "RESUMPTION_NO_PA",
            "--load-init-paths", str(init_file),
            "--output", str(out_json),
        ]
        subprocess.run(cmd, cwd=repo_root, check=True, stdout=subprocess.DEVNULL)
        with open(out_json, "r") as f:
            data = json.load(f)

        agg["total_repair_attempts"] += data["total_repair_attempts"]
        agg["total_interruptions_observed"] += data["total_interruptions"]
        agg["total_snapshots_with_completed_prefix"] += data["captured_snapshot_count"]

        if map_stem not in agg["by_map"]:
            agg["by_map"][map_stem] = {
                "cells": 0,
                "total_repair_attempts": 0,
                "total_interruptions_observed": 0,
                "snapshots_captured": 0,
                "positive_weights": 0,
                "zero_weights": 0,
                "negative_weights": 0,
                "feasible_negative": 0,
                "selected_negative_exact": 0,
                "forbidden_nodes": 0,
                "transitively_excluded": 0,
                "dependency_arcs": 0,
                "positive_exact_snapshots": 0,
                "positive_greedy_snapshots": 0,
                "exact_gt_greedy_snapshots": 0,
                "oracle_mismatches": 0,
                "total_gain_exact": 0,
                "total_gain_greedy": 0,
                "total_gain_nonneg_ref": 0,
                "total_gain_oracle": 0,
            }
        m_agg = agg["by_map"][map_stem]
        m_agg["cells"] += 1
        m_agg["total_repair_attempts"] += data["total_repair_attempts"]
        m_agg["total_interruptions_observed"] += data["total_interruptions"]
        m_agg["snapshots_captured"] += data["captured_snapshot_count"]

        cell_pos_w = 0
        cell_zero_w = 0
        cell_neg_w = 0
        cell_feas_neg = 0
        cell_sel_neg = 0
        cell_forb = 0
        cell_trans = 0
        cell_arcs = 0
        cell_nn_gain = 0
        cell_or_gain = 0

        for snap in data.get("snapshots", []):
            r_done = snap["candidate_count"]
            r_key = str(r_done)
            if r_key not in agg["by_r_done"]:
                agg["by_r_done"][r_key] = {
                    "r_done": r_done,
                    "snapshots": 0,
                    "positive_weights": 0,
                    "zero_weights": 0,
                    "negative_weights": 0,
                    "forbidden_nodes": 0,
                    "transitively_excluded": 0,
                    "dependency_arcs": 0,
                    "positive_exact_snapshots": 0,
                    "total_gain_exact": 0,
                    "total_gain_greedy": 0,
                    "total_gain_nonneg_ref": 0,
                    "total_gain_oracle": 0,
                }
            r_agg = agg["by_r_done"][r_key]
            r_agg["snapshots"] += 1
            r_agg["positive_weights"] += snap["positive_weight_count"]
            r_agg["zero_weights"] += snap["zero_weight_count"]
            r_agg["negative_weights"] += snap["negative_weight_count"]
            r_agg["forbidden_nodes"] += snap["forbidden_count"]
            r_agg["transitively_excluded"] += snap["transitively_excluded_count"]
            r_agg["dependency_arcs"] += snap["dependency_arc_count"]
            if snap["exact_gain"] > 0:
                r_agg["positive_exact_snapshots"] += 1
            r_agg["total_gain_exact"] += snap["exact_gain"]
            r_agg["total_gain_greedy"] += snap["greedy_gain"]
            r_agg["total_gain_nonneg_ref"] += snap["nonnegative_ref_gain"]
            r_agg["total_gain_oracle"] += snap["oracle_gain"]

            cell_pos_w += snap["positive_weight_count"]
            cell_zero_w += snap["zero_weight_count"]
            cell_neg_w += snap["negative_weight_count"]
            cell_feas_neg += snap["feasible_negative_weight_count"]
            cell_sel_neg += snap["exact_selected_negative_count"]
            cell_forb += snap["forbidden_count"]
            cell_trans += snap["transitively_excluded_count"]
            cell_arcs += snap["dependency_arc_count"]
            cell_nn_gain += snap["nonnegative_ref_gain"]
            cell_or_gain += snap["oracle_gain"]

            agg["total_candidates_evaluated"] += r_done
            agg["weight_counts"]["positive"] += snap["positive_weight_count"]
            agg["weight_counts"]["zero"] += snap["zero_weight_count"]
            agg["weight_counts"]["negative"] += snap["negative_weight_count"]
            agg["feasible_negative_candidates"] += snap["feasible_negative_weight_count"]
            agg["selected_negative_candidates_exact"] += snap["exact_selected_negative_count"]
            agg["forbidden_candidates"] += snap["forbidden_count"]
            agg["transitively_excluded_candidates"] += snap["transitively_excluded_count"]
            agg["dependency_arcs"] += snap["dependency_arc_count"]

            if snap["exact_gain"] > 0:
                agg["snapshots_with_positive_exact_gain"] += 1
                if len(agg["positive_opportunity_examples"]) < 30:
                    agg["positive_opportunity_examples"].append({
                        "map": map_stem,
                        "scen": scen_stem,
                        "num_agents": num_agents,
                        "seed": seed,
                        "snapshot_id": snap["snapshot_id"],
                        "attempt_id": snap["attempt_id"],
                        "action_kind": snap["action_kind"],
                        "r_done": r_done,
                        "weights": snap["weights"],
                        "pos_weights": snap["positive_weight_count"],
                        "zero_weights": snap["zero_weight_count"],
                        "neg_weights": snap["negative_weight_count"],
                        "forbidden_count": snap["forbidden_count"],
                        "transitively_excluded_count": snap["transitively_excluded_count"],
                        "dependency_arcs": snap["dependency_arc_count"],
                        "exact_gain": snap["exact_gain"],
                        "greedy_gain": snap["greedy_gain"],
                        "nonneg_ref_gain": snap["nonnegative_ref_gain"],
                        "oracle_gain": snap["oracle_gain"],
                        "exact_selected_count": snap["exact_selected_count"],
                        "exact_selected_neg": snap["exact_selected_negative_count"],
                    })
            if snap["greedy_gain"] > 0:
                agg["snapshots_with_positive_greedy_gain"] += 1
            if snap["nonnegative_ref_gain"] > 0:
                agg["snapshots_with_positive_nonneg_ref_gain"] += 1
            if snap["oracle_gain"] > 0:
                agg["snapshots_with_positive_oracle_gain"] += 1
            if snap["exact_gain"] > snap["greedy_gain"]:
                agg["snapshots_exact_gt_greedy"] += 1
            if snap["exact_gain"] != snap["nonnegative_ref_gain"]:
                agg["snapshots_exact_ne_nonneg_ref"] += 1
            if snap["exact_gain"] != snap["oracle_gain"] or not snap["exact_mixture_valid"]:
                agg["snapshots_exact_ne_oracle"] += 1

            agg["total_gain_greedy"] += snap["greedy_gain"]
            agg["total_gain_exact"] += snap["exact_gain"]
            agg["total_gain_nonneg_ref"] += snap["nonnegative_ref_gain"]
            agg["total_gain_exhaustive_oracle"] += snap["oracle_gain"]

        m_agg["positive_weights"] += cell_pos_w
        m_agg["zero_weights"] += cell_zero_w
        m_agg["negative_weights"] += cell_neg_w
        m_agg["feasible_negative"] += cell_feas_neg
        m_agg["selected_negative_exact"] += cell_sel_neg
        m_agg["forbidden_nodes"] += cell_forb
        m_agg["transitively_excluded"] += cell_trans
        m_agg["dependency_arcs"] += cell_arcs
        m_agg["positive_exact_snapshots"] += data["summary"]["positive_feasible_opportunities"]
        m_agg["positive_greedy_snapshots"] += data["summary"]["greedy_positive_opportunities"]
        m_agg["exact_gt_greedy_snapshots"] += data["summary"]["exact_strictly_exceeds_greedy_count"]
        m_agg["oracle_mismatches"] += data["summary"]["exact_vs_oracle_mismatch_count"]
        m_agg["total_gain_exact"] += data["summary"]["total_exact_gain"]
        m_agg["total_gain_greedy"] += data["summary"]["total_greedy_gain"]
        m_agg["total_gain_nonneg_ref"] += cell_nn_gain
        m_agg["total_gain_oracle"] += cell_or_gain

        cell_summaries.append({
            "map": map_stem,
            "scen": scen_stem,
            "num_agents": num_agents,
            "seed": seed,
            "total_repair_attempts": data["total_repair_attempts"],
            "total_interruptions_observed": data["total_interruptions"],
            "snapshots_captured": data["captured_snapshot_count"],
            "total_positive_weights": cell_pos_w,
            "total_zero_weights": cell_zero_w,
            "total_negative_weights": cell_neg_w,
            "total_feasible_negative": cell_feas_neg,
            "total_selected_negative": cell_sel_neg,
            "total_forbidden_nodes": cell_forb,
            "total_transitively_excluded": cell_trans,
            "total_dependency_arcs": cell_arcs,
            "snapshots_with_positive_exact_gain": data["summary"]["positive_feasible_opportunities"],
            "snapshots_with_positive_greedy_gain": data["summary"]["greedy_positive_opportunities"],
            "snapshots_exact_gt_greedy": data["summary"]["exact_strictly_exceeds_greedy_count"],
            "snapshots_oracle_mismatch": data["summary"]["exact_vs_oracle_mismatch_count"],
            "total_exact_gain": data["summary"]["total_exact_gain"],
            "total_greedy_gain": data["summary"]["total_greedy_gain"],
            "total_nonneg_ref_gain": cell_nn_gain,
            "total_oracle_gain": cell_or_gain,
        })
        print(
            f"  [{idx:02d}/{len(cells):02d}] {map_stem} {scen_stem} a={num_agents} s={seed}: "
            f"snaps={data['captured_snapshot_count']}, pos_opps={data['summary']['positive_feasible_opportunities']}, "
            f"exact_gain={data['summary']['total_exact_gain']}, greedy_gain={data['summary']['total_greedy_gain']}, "
            f"mismatch={data['summary']['exact_vs_oracle_mismatch_count']}"
        )

    agg["cell_summaries"] = cell_summaries
    summary_path = offline_dir / "offline_opportunity_summary.json"
    with open(summary_path, "w") as f:
        json.dump(agg, f, indent=2)
    print(f"[Offline Analysis] Saved summary to {summary_path}")
    return agg


def run_live_pilot(
    repo_root: Path,
    profiler_bin: Path,
    validator_bin: Path,
    config_path: Path,
    config: dict,
    init_dir: Path,
    pilot_dir: Path,
) -> dict:
    raw_dir = pilot_dir / "raw"
    paths_dir = pilot_dir / "paths"
    raw_dir.mkdir(parents=True, exist_ok=True)
    paths_dir.mkdir(parents=True, exist_ok=True)

    config_bytes = config_path.read_bytes()
    config_sha256 = hashlib.sha256(config_bytes).hexdigest()

    factors = config["factors"]
    base_pol = config["base_repair_and_scheduling_policy"]
    maps = factors["maps"]
    agent_counts = factors["agent_counts"]
    seeds = factors["seeds"]
    budgets = factors["budgets_seconds"]
    slices = factors["slice_durations_seconds"]
    policies = [p["name"] for p in factors["policies"]]
    k = base_pol["neighborhood_size"]

    runs_spec = []
    for m_obj in maps:
        map_stem = m_obj["name"]
        map_rel = m_obj["path"]
        for scen_rel in m_obj["scenarios"]:
            scen_stem = Path(scen_rel).stem
            for num_agents in agent_counts:
                init_file = ensure_init_paths(
                    repo_root, profiler_bin, init_dir, map_rel, scen_rel, num_agents
                )
                for seed in seeds:
                    for budget_s in budgets:
                        for slice_s in slices:
                            for policy in policies:
                                runs_spec.append(
                                    (map_rel, map_stem, scen_rel, scen_stem, num_agents, seed, budget_s, slice_s, policy, init_file)
                                )

    expected_runs = config.get("total_planned_runs", len(runs_spec))
    assert len(runs_spec) == expected_runs, f"Run count mismatch: {len(runs_spec)} vs {expected_runs}"

    print(f"[Live Pilot] Executing {len(runs_spec)} matched runs sequentially...")
    all_runs = []
    completed_runs = 0
    valid_runs = 0
    failed_runs = 0
    external_validator_passed_count = 0

    t_pilot_start = time.monotonic()
    for idx, (map_rel, map_stem, scen_rel, scen_stem, num_agents, seed, budget_s, slice_s, policy, init_file) in enumerate(runs_spec, 1):
        run_id = f"{map_stem}__{scen_stem}__a{num_agents}__s{seed}__b{budget_s:.1f}__p{policy}"
        out_json = raw_dir / f"{run_id}.json"
        out_paths = paths_dir / f"{run_id}.paths"

        cmd = [
            str(profiler_bin),
            "--mode", "online",
            "--map", str(repo_root / map_rel),
            "--scen", str(repo_root / scen_rel),
            "--agents", str(num_agents),
            "--seed", str(seed),
            "--budget", str(budget_s),
            "--slice", str(slice_s),
            "-k", str(k),
            "--policy", policy,
            "--load-init-paths", str(init_file),
            "--dump-final-paths", str(out_paths),
            "--output", str(out_json),
        ]
        res = subprocess.run(cmd, cwd=repo_root, capture_output=True, text=True)
        if res.returncode != 0 or not out_json.exists():
            failed_runs += 1
            print(f"  [ERROR] Run {idx}/{expected_runs} ({run_id}) failed: {res.stderr}")
            continue

        completed_runs += 1
        with open(out_json, "r") as f:
            run_data = json.load(f)

        # Run external CLI rbl_validate on the dumped final paths
        val_cmd = [
            str(validator_bin),
            "--map", str(repo_root / map_rel),
            "--scen", str(repo_root / scen_rel),
            "--paths", str(out_paths),
            "--agents", str(num_agents),
            "--reported-cost", str(run_data["final_cost"]),
        ]
        val_res = subprocess.run(val_cmd, cwd=repo_root, capture_output=True, text=True)
        cli_valid = False
        cli_soc = -1
        if val_res.returncode == 0:
            try:
                val_json = json.loads(val_res.stdout.strip())
                cli_valid = (val_json.get("status") == "VALID")
                cli_soc = int(val_json.get("independent_sum_of_costs", -1))
            except Exception:
                cli_valid = False

        run_data["run_id"] = run_id
        run_data["map_stem"] = map_stem
        run_data["scen_stem"] = scen_stem
        run_data["cli_validator_passed"] = cli_valid
        run_data["cli_validator_soc"] = cli_soc
        run_data["soc_matches_cli_validator"] = (cli_soc == run_data["final_cost"])

        if (
            run_data.get("plan_valid", False)
            and cli_valid
            and cli_soc == run_data["final_cost"]
        ):
            valid_runs += 1
            external_validator_passed_count += 1
        else:
            failed_runs += 1
            print(
                f"  [INVALID] Run {idx}/{expected_runs} ({run_id}) failed validation! "
                f"plan_valid={run_data.get('plan_valid')} cli_valid={cli_valid} "
                f"cli_soc={cli_soc} final_cost={run_data['final_cost']} stdout={val_res.stdout}"
            )

        all_runs.append(run_data)

        if idx % 16 == 0 or idx == expected_runs:
            elapsed = time.monotonic() - t_pilot_start
            print(
                f"  [{idx:03d}/{expected_runs:03d}] ({elapsed:6.1f}s) {run_id}: "
                f"init={run_data['initial_cost']} -> final={run_data['final_cost']} "
                f"(pa_commits={run_data['pa_committed']}, "
                f"pa_gain={run_data['pa_total_gain']}, valid={cli_valid})"
            )

    manifest = {
        "experiment_name": config["experiment_name"],
        "version": config.get("version", "1.1.0"),
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "git": get_git_info(repo_root),
        "config_path": str(config_path.relative_to(repo_root)),
        "config_sha256": config_sha256,
        "execution_mode": "sequential_single_process",
        "reconciliation": {
            "expected_runs": expected_runs,
            "completed_runs": completed_runs,
            "valid_runs": valid_runs,
            "external_cli_validator_passed": external_validator_passed_count,
            "failed_runs": failed_runs,
        },
    }

    with open(pilot_dir / "pilot_manifest.json", "w") as f:
        json.dump(manifest, f, indent=2)
    with open(pilot_dir / "all_runs.json", "w") as f:
        json.dump(all_runs, f, indent=2)

    print(
        f"[Live Pilot] Complete: expected={expected_runs}, completed={completed_runs}, "
        f"valid={valid_runs}, failed={failed_runs}"
    )
    return manifest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="configs/partial_acceptance_pilot.json")
    parser.add_argument("--build-dir", default="build/bench-release")
    parser.add_argument("--skip-offline", action="store_true")
    parser.add_argument("--skip-online", action="store_true")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    config_path = repo_root / args.config
    with open(config_path, "r") as f:
        config = json.load(f)

    profiler_bin = repo_root / args.build_dir / "rbl_profile_partial_acceptance"
    validator_bin = repo_root / args.build_dir / "rbl_validate"
    if not profiler_bin.exists():
        sys.exit(f"Missing profiler binary: {profiler_bin}")
    if not validator_bin.exists():
        sys.exit(f"Missing validator binary: {validator_bin}")

    init_dir = repo_root / "results" / "partial_acceptance_pilot" / "init_paths"
    init_dir.mkdir(parents=True, exist_ok=True)

    if not args.skip_offline:
        offline_dir = repo_root / "results" / "partial_acceptance_offline"
        offline_dir.mkdir(parents=True, exist_ok=True)
        run_offline_opportunity_analysis(repo_root, profiler_bin, config, init_dir, offline_dir)

    if not args.skip_online:
        pilot_dir = repo_root / "results" / "partial_acceptance_pilot"
        pilot_dir.mkdir(parents=True, exist_ok=True)
        manifest = run_live_pilot(
            repo_root, profiler_bin, validator_bin, config_path, config, init_dir, pilot_dir
        )
        if manifest["reconciliation"]["failed_runs"] > 0:
            sys.exit(1)


if __name__ == "__main__":
    main()
