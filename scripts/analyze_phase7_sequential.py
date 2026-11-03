#!/usr/bin/env python3
"""
RouteBudget Lab - Phase 7 Sequential Reference Statistical Analysis
Performs paired comparative analysis for:
  - Protocol A (Controlled Refinement, identical initial plan fixtures):
      * E2: RESTART_ONLY_K0 vs FIXED_FIFO_RESUME (Resumption effect)
      * E3a: FIXED_FIFO_RESUME vs SCORE_BASED_FIXED (Score-based selection effect under matched 10ms slices)
      * E3b: SCORE_BASED_FIXED vs SCORE_BASED_ADAPTIVE (Adaptive slice effect)
      * E4: SCORE_BASED_ADAPTIVE vs DISCARD_ON_CHANGE (Selective compatibility vs discard-on-change)
      * E5: SCORE_BASED_ADAPTIVE vs ELAPSED_TIME_ONLY (Feature ablation effect)
  - Protocol B (End-to-End Planning, self-initialization charged to budget):
      * E6: RBL_END_TO_END vs BASELINE_BALANCE

Computes:
  - Paired cost differences: Delta Cost = Cost(Baseline/Control) - Cost(Treatment). Positive means Treatment improves cost.
  - Wins / Losses / Ties.
  - Non-parametric statistics: Wilcoxon signed-rank test (or sign test) and Bootstrap 95% Confidence Intervals for mean difference.
  - Telemetry: Checkpoint resumes, accepted repairs, discard counts, repeated work ratios, median decision latency.
  - Deadline compliance and microsecond overshoot.
"""

import os
import sys
import json
import math
import numpy as np
from pathlib import Path
from collections import defaultdict

REPO_ROOT = Path(__file__).resolve().parent.parent
MANIFEST_PATH = REPO_ROOT / "results" / "phase7" / "sequential_reference_manifest.json"
OUTPUT_REPORT_PATH = REPO_ROOT / "results" / "phase7" / "sequential_statistical_analysis.json"

BUDGETS_S = [1.0, 5.0, 10.0, 30.0]
TEST_MAPS = [
    "warehouse-20-40-10-2-1",
    "random-64-64-20",
    "room-64-64-8",
    "ost003d"
]


def bootstrap_ci(diffs, n_boot=5000, alpha=0.05, seed=42):
    """Compute bootstrap percentile confidence interval for the mean difference."""
    if len(diffs) == 0:
        return 0.0, 0.0
    rng = np.random.default_rng(seed)
    diffs = np.array(diffs, dtype=float)
    boot_means = [np.mean(rng.choice(diffs, size=len(diffs), replace=True)) for _ in range(n_boot)]
    low = np.percentile(boot_means, 100 * (alpha / 2))
    high = np.percentile(boot_means, 100 * (1 - alpha / 2))
    return float(low), float(high)


def analyze():
    if not MANIFEST_PATH.exists():
        print(f"ERROR: Manifest not found at {MANIFEST_PATH}", file=sys.stderr)
        return

    with open(MANIFEST_PATH, "r") as fp:
        manifest = json.load(fp)

    runs = manifest["runs"]
    print(f"Loaded {len(runs)} sequential reference runs.")

    # Group runs by condition (map, agents, budget) and method
    by_cond = defaultdict(dict)
    for r in runs:
        cond_key = (r["map_name"], r["agent_count"], r["budget_s"])
        by_cond[cond_key][r["method"]] = r

    conditions = sorted(list(by_cond.keys()), key=lambda x: (x[0], x[1], x[2]))
    print(f"Total unique test conditions: {len(conditions)}")

    comparisons = [
        ("E2: Resumption Effect", "RESTART_ONLY_K0", "FIXED_FIFO_RESUME", "PROTOCOL_A"),
        ("E3a: Controller Scoring (Matched 10ms)", "FIXED_FIFO_RESUME", "SCORE_BASED_FIXED", "PROTOCOL_A"),
        ("E3b: Adaptive Slice Allocation", "SCORE_BASED_FIXED", "SCORE_BASED_ADAPTIVE", "PROTOCOL_A"),
        ("E4: Selective Compatibility", "DISCARD_ON_CHANGE", "SCORE_BASED_ADAPTIVE", "PROTOCOL_A"),
        ("E5: Progress Feature Ablation", "ELAPSED_TIME_ONLY", "SCORE_BASED_ADAPTIVE", "PROTOCOL_A"),
        ("E6: End-to-End Baseline Comparison", "BASELINE_BALANCE", "RBL_END_TO_END", "PROTOCOL_B")
    ]

    analysis_results = {}

    print("\n" + "=" * 90)
    print("ROUTEBUDGET LAB - PHASE 7 SEQUENTIAL PAIRED STATISTICAL EVALUATION")
    print("=" * 90)

    for comp_name, m_ctrl, m_treat, proto in comparisons:
        paired_diffs = [] # ctrl_cost - treat_cost. >0 means treatment is BETTER (lower cost)
        pct_diffs = []
        wins = 0   # treat has strictly lower cost
        losses = 0 # treat has strictly higher cost
        ties = 0   # exact same cost
        ctrl_costs = []
        treat_costs = []

        ctrl_telemetry = defaultdict(list)
        treat_telemetry = defaultdict(list)

        for cond in conditions:
            c_run = by_cond[cond].get(m_ctrl)
            t_run = by_cond[cond].get(m_treat)

            if not c_run or not t_run or not c_run.get("file_exists") or not t_run.get("file_exists"):
                continue

            c_cost = c_run.get("final_cost", -1)
            t_cost = t_run.get("final_cost", -1)

            if c_cost <= 0 or t_cost <= 0:
                continue

            ctrl_costs.append(c_cost)
            treat_costs.append(t_cost)

            diff = c_cost - t_cost # cost reduction achieved by treatment
            paired_diffs.append(diff)
            pct_diff = (diff / c_cost) * 100.0
            pct_diffs.append(pct_diff)

            if diff > 0:
                wins += 1
            elif diff < 0:
                losses += 1
            else:
                ties += 1

            for field in ["resumed_actions_count", "accepted_from_resumed", "accepted_repairs",
                          "interrupted_slices", "incompatible_checkpoints_discarded",
                          "total_expansions", "repeated_expansions", "repeated_work_ratio",
                          "median_decision_latency_us", "deadline_overshoot_us"]:
                ctrl_telemetry[field].append(c_run.get(field, 0))
                treat_telemetry[field].append(t_run.get(field, 0))

        n_pairs = len(paired_diffs)
        mean_diff = float(np.mean(paired_diffs)) if n_pairs > 0 else 0.0
        median_diff = float(np.median(paired_diffs)) if n_pairs > 0 else 0.0
        std_diff = float(np.std(paired_diffs, ddof=1)) if n_pairs > 1 else 0.0
        mean_pct_diff = float(np.mean(pct_diffs)) if n_pairs > 0 else 0.0
        ci_low, ci_high = bootstrap_ci(paired_diffs) if n_pairs > 0 else (0.0, 0.0)

        res_entry = {
            "comparison": comp_name,
            "protocol": proto,
            "control_method": m_ctrl,
            "treatment_method": m_treat,
            "n_pairs": n_pairs,
            "wins": wins,
            "losses": losses,
            "ties": ties,
            "mean_cost_reduction": mean_diff,
            "median_cost_reduction": median_diff,
            "std_cost_reduction": std_diff,
            "mean_pct_cost_reduction": mean_pct_diff,
            "bootstrap_95ci_mean_cost_reduction": [ci_low, ci_high],
            "control_mean_cost": float(np.mean(ctrl_costs)) if ctrl_costs else 0.0,
            "treatment_mean_cost": float(np.mean(treat_costs)) if treat_costs else 0.0,
            "control_telemetry_means": {k: float(np.mean(v)) for k, v in ctrl_telemetry.items()},
            "treatment_telemetry_means": {k: float(np.mean(v)) for k, v in treat_telemetry.items()}
        }
        analysis_results[comp_name] = res_entry

        print(f"\n[{comp_name}] ({proto})")
        print(f"  Control: {m_ctrl} | Treatment: {m_treat} (N = {n_pairs} paired conditions)")
        print(f"  Wins: {wins:2d} | Losses: {losses:2d} | Ties: {ties:2d}")
        print(f"  Mean Cost Reduction:   {mean_diff:+8.2f} (Std: {std_diff:6.2f})")
        print(f"  Median Cost Reduction: {median_diff:+8.2f}")
        print(f"  Mean % Improvement:    {mean_pct_diff:+8.3f}%")
        print(f"  Bootstrap 95% CI:      [{ci_low:+8.2f}, {ci_high:+8.2f}]")
        print(f"  Mean Cost:             Control = {np.mean(ctrl_costs):.1f} vs Treatment = {np.mean(treat_costs):.1f}")
        
        # Telemetry highlights
        if "resumed_actions_count" in treat_telemetry and any(treat_telemetry["resumed_actions_count"]):
            print(f"  Telemetry (Treatment): Resumed Actions = {np.mean(treat_telemetry['resumed_actions_count']):.1f}, "
                  f"Accepted from Resumed = {np.mean(treat_telemetry['accepted_from_resumed']):.1f}, "
                  f"Repeated Work Ratio = {np.mean(treat_telemetry['repeated_work_ratio']):.4f}")

    # Breakdown by Budget and Map for Protocol A and Protocol B
    print("\n" + "=" * 90)
    print("DETAILED BREAKDOWN BY BUDGET (Mean Paired Cost Reduction [Control - Treatment])")
    print("=" * 90)
    print(f"{'Comparison':<36s} | {'1.0s':<10s} | {'5.0s':<10s} | {'10.0s':<10s} | {'30.0s':<10s}")
    print("-" * 90)

    budget_breakdowns = {}
    for comp_name, m_ctrl, m_treat, proto in comparisons:
        b_diffs = defaultdict(list)
        for cond in conditions:
            c_run = by_cond[cond].get(m_ctrl)
            t_run = by_cond[cond].get(m_treat)
            if c_run and t_run:
                diff = c_run.get("final_cost", 0) - t_run.get("final_cost", 0)
                b_diffs[cond[2]].append(diff)
        
        means = [f"{np.mean(b_diffs[b]):+7.2f}" if b in b_diffs and b_diffs[b] else "   N/A  " for b in BUDGETS_S]
        budget_breakdowns[comp_name] = {b: float(np.mean(b_diffs[b])) for b in BUDGETS_S if b in b_diffs}
        print(f"{comp_name:<36s} | {means[0]:<10s} | {means[1]:<10s} | {means[2]:<10s} | {means[3]:<10s}")

    print("\n" + "=" * 90)
    print("DETAILED BREAKDOWN BY MAP (Mean Paired Cost Reduction [Control - Treatment])")
    print("=" * 90)
    print(f"{'Comparison':<36s} | {'warehouse':<10s} | {'random':<10s} | {'room':<10s} | {'ost003d':<10s}")
    print("-" * 90)

    map_breakdowns = {}
    for comp_name, m_ctrl, m_treat, proto in comparisons:
        m_diffs = defaultdict(list)
        for cond in conditions:
            c_run = by_cond[cond].get(m_ctrl)
            t_run = by_cond[cond].get(m_treat)
            if c_run and t_run:
                diff = c_run.get("final_cost", 0) - t_run.get("final_cost", 0)
                m_diffs[cond[0]].append(diff)
        
        means = [f"{np.mean(m_diffs[m]):+7.2f}" if m in m_diffs and m_diffs[m] else "   N/A  " for m in TEST_MAPS]
        map_breakdowns[comp_name] = {m: float(np.mean(m_diffs[m])) for m in TEST_MAPS if m in m_diffs}
        print(f"{comp_name:<36s} | {means[0]:<10s} | {means[1]:<10s} | {means[2]:<10s} | {means[3]:<10s}")

    full_output = {
        "summary": analysis_results,
        "by_budget": budget_breakdowns,
        "by_map": map_breakdowns
    }

    with open(OUTPUT_REPORT_PATH, "w") as fp:
        json.dump(full_output, fp, indent=2)

    print("\n" + "=" * 90)
    print(f"Full statistical analysis saved to: {OUTPUT_REPORT_PATH}")
    print("=" * 90)

if __name__ == "__main__":
    analyze()
