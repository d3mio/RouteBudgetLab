#!/usr/bin/env python3
"""
RouteBudget Lab - Analysis Script for Partial-Prefix Recovery Frozen Evaluation
Parses results/partial_recovery_evaluation/evaluation_summary.json and generates
comprehensive statistical comparisons across Policy A, Policy B, and Policy C.
"""

import json
import sys
from pathlib import Path
import numpy as np

REPO_ROOT = Path(__file__).resolve().parent.parent
SUMMARY_FILE = REPO_ROOT / "results" / "partial_recovery_evaluation" / "evaluation_summary.json"
PERTURB_FILE = REPO_ROOT / "results" / "partial_recovery_perturbation.json"

def main():
    if not SUMMARY_FILE.exists():
        print(f"Summary file not found at {SUMMARY_FILE}")
        return

    with open(SUMMARY_FILE, "r") as f:
        runs = json.load(f)

    print("=" * 80)
    print("ROUTEBUDGET LAB - PARTIAL-PREFIX RECOVERY STATISTICAL ANALYSIS")
    print(f"Total Evaluated Runs: {len(runs)}")
    print("=" * 80)

    # 1. Integrity check
    valid_count = sum(1 for r in runs if r.get("independent_validation_passed", False))
    print(f"Independent Validation Pass Rate: {valid_count}/{len(runs)} ({valid_count/len(runs)*100:.1f}%)")
    assert valid_count == len(runs), "All evaluated solutions must pass independent validation!"

    # 2. Group by Policy
    policies = ["Policy_A_RestartOnly", "Policy_B_WholeSelective", "Policy_C_PartialPrefix"]
    by_policy = {p: [] for p in policies}
    for r in runs:
        pol = r.get("policy_label")
        if pol in by_policy:
            by_policy[pol].append(r)

    print("\n--- Summary by Policy across all 72 conditions ---")
    header = f"{'Policy':<26} | {'Final Cost':<10} | {'Improvement':<11} | {'Rep Work %':<10} | {'Partial Rec':<11} | {'Salvaged Ag':<11}"
    print(header)
    print("-" * len(header))

    for p in policies:
        pruns = by_policy[p]
        costs = [r.get("final_cost", 0) for r in pruns]
        impr = [r.get("total_improvement", 0) for r in pruns]
        rep_ratio = [r.get("repeated_work_ratio", 0.0) * 100 for r in pruns]
        part_rec = sum(r.get("partial_recovery_count", 0) for r in pruns)
        salv_ag = sum(r.get("total_salvaged_prefix_agents", 0) for r in pruns)

        print(f"{p:<26} | {np.mean(costs):<10.2f} | {np.mean(impr):<11.2f} | {np.mean(rep_ratio):<9.2f}% | {part_rec:<11} | {salv_ag:<11}")

    # 3. Breakdown by Map and Budget
    print("\n--- Breakdown by Map and Budget ---")
    maps = sorted(list(set(r["map_name"] for r in runs)))
    budgets = sorted(list(set(r["budget_s"] for r in runs)))

    for m in maps:
        print(f"\nMap: {m}")
        print(f"{'Budget':<8} | {'Policy':<24} | {'Mean Cost':<10} | {'Mean Impr':<10} | {'Rep Ratio':<10} | {'Partial Rec':<11}")
        print("-" * 80)
        for b in budgets:
            for p in policies:
                subset = [r for r in runs if r["map_name"] == m and r["budget_s"] == b and r["policy_label"] == p]
                if not subset: continue
                c_mean = np.mean([r["final_cost"] for r in subset])
                i_mean = np.mean([r["total_improvement"] for r in subset])
                r_mean = np.mean([r.get("repeated_work_ratio", 0.0) * 100 for r in subset])
                p_sum = sum(r.get("partial_recovery_count", 0) for r in subset)
                print(f"{b:<8.1f} | {p:<24} | {c_mean:<10.2f} | {i_mean:<10.2f} | {r_mean:<9.2f}% | {p_sum:<11}")

    # 4. Controlled Perturbation Summary
    if PERTURB_FILE.exists():
        with open(PERTURB_FILE, "r") as f:
            pdata = json.load(f)
        print("\n" + "=" * 80)
        print("CONTROLLED PERTURBATION BENCHMARK SUMMARY")
        print("=" * 80)
        p_trials = pdata.get("trials", [])
        p_header = f"{'Condition':<30} | {'Policy':<22} | {'Verdict':<26} | {'Salvaged':<8} | {'Expansions':<10} | {'Val (us)':<8}"
        print(p_header)
        print("-" * len(p_header))
        for t in p_trials:
            c_name = t["condition_name"][:30]
            pol_name = t["policy_name"][:22]
            v_str = t["verdict"][:26]
            salv = f"{t['salvaged_prefix_len']}/{t['original_prefix_len']}"
            exp = t["expansions"]
            v_us = f"{t['validation_time_us']:.1f}"
            print(f"{c_name:<30} | {pol_name:<22} | {v_str:<26} | {salv:<8} | {exp:<10} | {v_us:<8}")

if __name__ == "__main__":
    main()
