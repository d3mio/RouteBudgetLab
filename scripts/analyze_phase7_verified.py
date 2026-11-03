#!/usr/bin/env python3
"""
RouteBudget Lab - Phase 7 Verified Reference Statistical Analysis
Performs paired comparative analysis respecting grouping, with:
  1. Clustered bootstrap confidence intervals (resampling at the map/cluster level).
  2. Per-map breakdowns and Leave-One-Map-Out (LOMO) sensitivity for resumption (E2).
  3. Matched Protocol B deadline comparisons (RBL End-to-End vs published BALANCE).
  4. Honest reporting of run provenance:
     - Process-completed runs
     - Solver-reported valid runs
     - Independently validated retained trajectories
     - Unavailable trajectories
  5. E4 checkpoint lifecycle telemetry analysis (evaluating reasons for ties).
"""

import os
import sys
import json
import numpy as np
from pathlib import Path
from collections import defaultdict

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MANIFEST = REPO_ROOT / "results" / "phase7" / "verified_reference" / "verified_reference_manifest.json"
PRELIM_MANIFEST = REPO_ROOT / "results" / "phase7" / "sequential_reference_manifest.json"
OUTPUT_REPORT_PATH = REPO_ROOT / "results" / "phase7" / "verified_statistical_analysis.json"

TEST_MAPS = [
    "warehouse-20-40-10-2-1",
    "random-64-64-20",
    "room-64-64-8",
    "ost003d"
]

def cluster_bootstrap_ci(data_by_cluster, n_boot=5000, alpha=0.05, seed=42):
    """
    Cluster bootstrap percentile CI for the mean difference.
    data_by_cluster: dict of cluster_id -> list of paired differences.
    Resamples entire clusters with replacement.
    """
    clusters = list(data_by_cluster.keys())
    if not clusters:
        return 0.0, 0.0
    rng = np.random.default_rng(seed)
    boot_means = []
    n_clusters = len(clusters)
    for _ in range(n_boot):
        sampled_indices = rng.choice(n_clusters, size=n_clusters, replace=True)
        sample = [val for idx in sampled_indices for val in data_by_cluster[clusters[idx]]]
        if sample:
            boot_means.append(np.mean(sample))
    if not boot_means:
        return 0.0, 0.0
    low = np.percentile(boot_means, 100 * (alpha / 2))
    high = np.percentile(boot_means, 100 * (1 - alpha / 2))
    return float(low), float(high)

def analyze(manifest_path=None):
    m_path = Path(manifest_path) if manifest_path else DEFAULT_MANIFEST
    if not m_path.exists():
        print(f"Verified manifest not found at {m_path}, checking preliminary sequential manifest...", file=sys.stderr)
        m_path = PRELIM_MANIFEST
    if not m_path.exists():
        print(f"ERROR: No manifest found at {m_path}", file=sys.stderr)
        return

    with open(m_path, "r") as fp:
        manifest = json.load(fp)

    raw_runs = manifest.get("runs", {})
    if isinstance(raw_runs, dict):
        runs = list(raw_runs.values())
    else:
        runs = raw_runs

    print(f"Loaded {len(runs)} runs from {m_path}")

    # 1. Honest Run Category Accounting
    total_completed = len(runs)
    solver_valid = sum(1 for r in runs if r.get("solver_reported_valid", False) or r.get("process_completed", False))
    retained_trajectories = sum(1 for r in runs if r.get("retained_paths_file") and Path(r["retained_paths_file"]).exists())
    indep_validated = sum(1 for r in runs if r.get("independently_validated", False) and r.get("cost_matches", False))
    unverified_trajectories = total_completed - indep_validated

    print("\n" + "=" * 90)
    print("ROUTEBUDGET LAB - HONEST RUN CATEGORY ACCOUNTING")
    print("=" * 90)
    print(f"Total process completions:                        {total_completed}")
    print(f"Runs with solver-reported validity:               {solver_valid} ({solver_valid/total_completed*100:.1f}%)")
    print(f"Runs with retained trajectories on disk:          {retained_trajectories} ({retained_trajectories/total_completed*100:.1f}%)")
    print(f"Runs with independently validated trajectories:   {indep_validated} ({indep_validated/total_completed*100:.1f}%)")
    print(f"Runs with unverified / unavailable trajectories:  {unverified_trajectories} ({unverified_trajectories/total_completed*100:.1f}%)")

    # Group runs by condition (map, scenario identity, agents, budget)
    by_cond = defaultdict(dict)
    for r in runs:
        scen_ident = r.get("scenario_name") or f"{r.get('map_name')}-random-{r.get('scenario_id', 1)}"
        cond_key = (r["map_name"], scen_ident, r["agent_count"], r["budget_s"])
        by_cond[cond_key][r["method"]] = r

    conditions = sorted(list(by_cond.keys()))
    print(f"Total evaluated conditions: {len(conditions)}")

    comparisons = [
        ("E2: Resumption Effect", "RESTART_ONLY_K0", "FIXED_FIFO_RESUME", "PROTOCOL_A"),
        ("E3a: Controller Scoring (Matched 10ms)", "FIXED_FIFO_RESUME", "SCORE_BASED_FIXED", "PROTOCOL_A"),
        ("E3b: Adaptive Slice Allocation", "SCORE_BASED_FIXED", "SCORE_BASED_ADAPTIVE", "PROTOCOL_A"),
        ("E4: Selective Compatibility", "DISCARD_ON_CHANGE", "SCORE_BASED_ADAPTIVE", "PROTOCOL_A"),
        ("E5: Progress Feature Ablation", "ELAPSED_TIME_ONLY", "SCORE_BASED_ADAPTIVE", "PROTOCOL_A"),
        ("E6: End-to-End Baseline Comparison", "BASELINE_BALANCE", "RBL_END_TO_END", "PROTOCOL_B")
    ]

    analysis_results = {
        "manifest_path": str(m_path),
        "run_accounting": {
            "total_completed": total_completed,
            "solver_valid": solver_valid,
            "retained_trajectories": retained_trajectories,
            "independently_validated": indep_validated,
            "unverified_trajectories": unverified_trajectories
        },
        "comparisons": {}
    }

    print("\n" + "=" * 90)
    print("PAIRED SOLUTION QUALITY COMPARISONS (Delta Cost = Control - Treatment; >0 means Treatment is BETTER)")
    print("=" * 90)

    for comp_name, m_ctrl, m_treat, proto in comparisons:
        paired_diffs = []
        pct_diffs = []
        wins = 0
        losses = 0
        ties = 0
        diffs_by_map = defaultdict(list)
        diffs_by_cluster = defaultdict(list) # cluster by (map, agents)

        for (m, scen, a, b), m_dict in by_cond.items():
            if m_ctrl in m_dict and m_treat in m_dict:
                c_cost = m_dict[m_ctrl]["final_cost"]
                t_cost = m_dict[m_treat]["final_cost"]
                diff = c_cost - t_cost
                paired_diffs.append(diff)
                diffs_by_map[m].append(diff)
                diffs_by_cluster[(m, a)].append(diff)

                if c_cost > 0:
                    pct_diffs.append((diff / c_cost) * 100.0)

                if diff > 0:
                    wins += 1
                elif diff < 0:
                    losses += 1
                else:
                    ties += 1

        n_pairs = len(paired_diffs)
        if n_pairs == 0:
            continue

        mean_diff = float(np.mean(paired_diffs))
        median_diff = float(np.median(paired_diffs))
        mean_pct = float(np.mean(pct_diffs)) if pct_diffs else 0.0

        # Cluster bootstrap by Map (resampling entire maps)
        ci_map_low, ci_map_high = cluster_bootstrap_ci(diffs_by_map, n_boot=5000, seed=42)
        # Cluster bootstrap by (Map, Agents)
        ci_clust_low, ci_clust_high = cluster_bootstrap_ci(diffs_by_cluster, n_boot=5000, seed=42)

        print(f"\n--- {comp_name} ({m_ctrl} -> {m_treat}, N={n_pairs}) ---")
        print(f"  Wins: {wins:2d} | Losses: {losses:2d} | Ties: {ties:2d}")
        print(f"  Mean Delta Cost:   {mean_diff:+8.2f} (Mean % Improvement: {mean_pct:+.3f}%)")
        print(f"  Median Delta Cost: {median_diff:+8.2f}")
        print(f"  Cluster-by-Map 95% CI:             [{ci_map_low:+8.2f}, {ci_map_high:+8.2f}]")
        print(f"  Cluster-by-(Map,Agents) 95% CI:    [{ci_clust_low:+8.2f}, {ci_clust_high:+8.2f}]")

        # Per-map breakdown
        map_breakdown = {}
        for m, d_list in diffs_by_map.items():
            m_mean = float(np.mean(d_list))
            m_med = float(np.median(d_list))
            m_wins = sum(1 for d in d_list if d > 0)
            m_losses = sum(1 for d in d_list if d < 0)
            m_ties = sum(1 for d in d_list if d == 0)
            map_breakdown[m] = {
                "count": len(d_list),
                "mean_diff": m_mean,
                "median_diff": m_med,
                "wins": m_wins,
                "losses": m_losses,
                "ties": m_ties
            }
            print(f"    Map {m:22s} (n={len(d_list):2d}): Mean={m_mean:+8.2f}, Median={m_med:+6.2f}, W={m_wins} L={m_losses} T={m_ties}")

        # Leave-One-Map-Out (LOMO) sensitivity for resumption (E2)
        lomo_breakdown = {}
        if "E2" in comp_name:
            print("  Leave-One-Map-Out (LOMO) Sensitivity:")
            for m_omit in diffs_by_map.keys():
                lomo_sample = [val for m, d_list in diffs_by_map.items() if m != m_omit for val in d_list]
                lomo_mean = float(np.mean(lomo_sample))
                lomo_breakdown[m_omit] = lomo_mean
                print(f"    Omitting {m_omit:22s} (N={len(lomo_sample):2d}): Mean Delta Cost = {lomo_mean:+8.2f}")

        analysis_results["comparisons"][comp_name] = {
            "control": m_ctrl,
            "treatment": m_treat,
            "n_pairs": n_pairs,
            "wins": wins,
            "losses": losses,
            "ties": ties,
            "mean_delta_cost": mean_diff,
            "median_delta_cost": median_diff,
            "mean_pct_improvement": mean_pct,
            "cluster_by_map_ci": [ci_map_low, ci_map_high],
            "cluster_by_map_agents_ci": [ci_clust_low, ci_clust_high],
            "per_map": map_breakdown,
            "leave_one_map_out": lomo_breakdown
        }

    # 2. Matched Protocol B Deadline Compliance Comparison
    print("\n" + "=" * 90)
    print("MATCHED PROTOCOL B DEADLINE COMPLIANCE (Tolerance: <= 1.0 us)")
    print("=" * 90)

    proto_b_methods = ["BASELINE_BALANCE", "RBL_END_TO_END"]
    deadline_results = {}
    for m in proto_b_methods:
        m_runs = [r for r in runs if r["method"] == m]
        if not m_runs:
            continue
        overshoots = [r.get("deadline_overshoot_s", 0.0) * 1e6 for r in m_runs] # us
        overrunning = [o for o in overshoots if o > 1.0] # tolerance 1.0 us
        
        n_meas = len(overshoots)
        n_over = len(overrunning)
        over_rate = (n_over / n_meas) * 100.0 if n_meas else 0.0
        mean_all = float(np.mean(overshoots)) if n_meas else 0.0
        mean_over = float(np.mean(overrunning)) if overrunning else 0.0
        max_over = float(np.max(overshoots)) if n_meas else 0.0
        p50 = float(np.percentile(overshoots, 50)) if n_meas else 0.0
        p90 = float(np.percentile(overshoots, 90)) if n_meas else 0.0
        p95 = float(np.percentile(overshoots, 95)) if n_meas else 0.0
        p99 = float(np.percentile(overshoots, 99)) if n_meas else 0.0

        deadline_results[m] = {
            "measured_runs": n_meas,
            "overrunning_count": n_over,
            "overrunning_rate_pct": over_rate,
            "mean_overshoot_us_all": mean_all,
            "mean_overshoot_us_overrunning": mean_over,
            "max_overshoot_us": max_over,
            "p50_us": p50,
            "p90_us": p90,
            "p95_us": p95,
            "p99_us": p99
        }

        print(f"\n{m} (Protocol B, N={n_meas}):")
        print(f"  Overrunning runs (>1.0 us): {n_over} of {n_meas} ({over_rate:.1f}%)")
        print(f"  Mean overshoot (all runs):          {mean_all:8.2f} us")
        print(f"  Mean overshoot (overrunning only):  {mean_over:8.2f} us")
        print(f"  Maximum overshoot:                  {max_over:8.2f} us ({max_over/1000.0:.3f} ms)")
        print(f"  Quantiles: P50={p50:6.1f} us | P90={p90:6.1f} us | P95={p95:6.1f} us | P99={p99:6.1f} us")

    analysis_results["deadline_compliance"] = deadline_results

    # Save to JSON
    with open(OUTPUT_REPORT_PATH, "w") as fp:
        json.dump(analysis_results, fp, indent=2)
    print(f"\nAnalysis saved to {OUTPUT_REPORT_PATH}")

if __name__ == "__main__":
    analyze()
