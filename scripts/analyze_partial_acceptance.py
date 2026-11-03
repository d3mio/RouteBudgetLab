#!/usr/bin/env python3
"""
RouteBudget Lab - Partial Acceptance Statistical & Opportunity Analyzer (Task 10)

Analyzes:
1. Offline Opportunity Analysis (results/partial_acceptance_offline/offline_opportunity_summary.json):
   - Interruptions, candidate counts, weight sign distributions, forbidden & transitive exclusions.
   - Multi-selector replay on identical snapshots (No-PA, Greedy, Exact, Nonnegative Ref, Exhaustive Oracle).
   - Breakdown by map and by completed prefix length r_done.
2. Matched 192-Run Live Pilot (results/partial_acceptance_pilot/all_runs.json):
   - Paired comparisons on complete key (map_stem, scen_stem, num_agents, seed, time_budget_s, slice_budget_s).
   - Wins / Ties / Losses, Mean & Median paired deltas, Bootstrap 95% CIs (10,000 resamples).
   - Exact two-sided sign test and Wilcoxon signed-rank test (with explicit tie/zero handling).
   - Stratified breakdowns by map, agent count, and planning budget.
   - Online overhead accounting and deadline compliance (cost_at_deadline vs final_cost).
"""

import argparse
import json
import math
import random
from collections import defaultdict
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(description="Analyze Partial Acceptance Experiments")
    parser.add_argument(
        "--pilot-dir",
        type=str,
        default="results/partial_acceptance_pilot",
        help="Directory containing pilot_manifest.json and all_runs.json",
    )
    parser.add_argument(
        "--offline-summary",
        type=str,
        default="results/partial_acceptance_offline/offline_opportunity_summary.json",
        help="Path to offline opportunity summary JSON",
    )
    parser.add_argument(
        "--output-md",
        type=str,
        default="docs/partial_acceptance_analysis.md",
        help="Output Markdown report path",
    )
    parser.add_argument(
        "--output-json",
        type=str,
        default="results/partial_acceptance_pilot/statistical_analysis.json",
        help="Output statistical analysis JSON path",
    )
    return parser.parse_args()


def quantile(sorted_vals, q):
    if not sorted_vals:
        return 0.0
    n = len(sorted_vals)
    pos = q * (n - 1)
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    if lo == hi:
        return float(sorted_vals[lo])
    return float(sorted_vals[lo] * (hi - pos) + sorted_vals[hi] * (pos - lo))


def bootstrap_ci(vals, n_boot=10000, seed=42):
    if not vals:
        return {"mean_ci": [0.0, 0.0], "median_ci": [0.0, 0.0]}
    rng = random.Random(seed)
    n = len(vals)
    boot_means = []
    boot_medians = []
    for _ in range(n_boot):
        sample = [vals[rng.randrange(n)] for _ in range(n)]
        boot_means.append(sum(sample) / n)
        sample.sort()
        boot_medians.append(quantile(sample, 0.5))
    boot_means.sort()
    boot_medians.sort()
    return {
        "mean_ci": [round(quantile(boot_means, 0.025), 4), round(quantile(boot_means, 0.975), 4)],
        "median_ci": [round(quantile(boot_medians, 0.025), 4), round(quantile(boot_medians, 0.975), 4)],
    }


def exact_sign_test_two_sided(wins, losses):
    n = wins + losses
    if n == 0:
        return 1.0
    k = min(wins, losses)
    total = 2 ** n
    tail = sum(math.comb(n, i) for i in range(k + 1))
    return min(1.0, 2.0 * tail / total)


def wilcoxon_signed_rank_normal_approx(deltas):
    """
    Wilcoxon signed-rank test on non-zero differences with tie correction.
    Returns (W_plus, W_minus, n_nonzero, p_value_two_sided).
    """
    nonzero = [d for d in deltas if d != 0]
    n = len(nonzero)
    if n == 0:
        return 0.0, 0.0, 0, 1.0

    abs_indexed = sorted(enumerate(nonzero), key=lambda x: abs(x[1]))
    ranks = [0.0] * n
    tie_groups = []
    i = 0
    while i < n:
        j = i
        while j < n and abs(abs_indexed[j][1]) == abs(abs_indexed[i][1]):
            j += 1
        avg_rank = (i + 1 + j) / 2.0
        for k in range(i, j):
            ranks[abs_indexed[k][0]] = avg_rank
        t_len = j - i
        if t_len > 1:
            tie_groups.append(t_len)
        i = j

    w_plus = sum(ranks[idx] for idx, d in enumerate(nonzero) if d > 0)
    w_minus = sum(ranks[idx] for idx, d in enumerate(nonzero) if d < 0)
    w_stat = min(w_plus, w_minus)

    mean_w = n * (n + 1) / 4.0
    tie_corr = sum(t * (t * t - 1) for t in tie_groups) / 48.0
    var_w = n * (n + 1) * (2 * n + 1) / 24.0 - tie_corr
    if var_w <= 0:
        return w_plus, w_minus, n, 1.0

    # Continuity correction
    z = (abs(w_stat - mean_w) - 0.5) / math.sqrt(var_w)
    p_val = math.erfc(max(0.0, z) / math.sqrt(2.0))
    return round(w_plus, 2), round(w_minus, 2), n, round(min(1.0, p_val), 6)


def compute_paired_stats(pairs, policy_a, policy_b, metric="cost_at_deadline"):
    """
    Computes paired statistics for (cost(B) - cost(A)).
    Positive delta means Policy A achieved LOWER (better) Sum of Costs than Policy B.
    """
    deltas = []
    pct_deltas = []
    for key, pol_map in sorted(pairs.items()):
        if policy_a in pol_map and policy_b in pol_map:
            ca = pol_map[policy_a][metric]
            cb = pol_map[policy_b][metric]
            init_c = pol_map[policy_a]["initial_cost"]
            d = cb - ca  # > 0 means A is better (lower cost) than B
            deltas.append(d)
            if init_c > 0:
                pct_deltas.append(100.0 * d / init_c)

    n = len(deltas)
    wins = sum(1 for d in deltas if d > 0)
    ties = sum(1 for d in deltas if d == 0)
    losses = sum(1 for d in deltas if d < 0)
    s_deltas = sorted(deltas)
    mean_d = sum(deltas) / n if n else 0.0
    med_d = quantile(s_deltas, 0.5)
    q25 = quantile(s_deltas, 0.25)
    q75 = quantile(s_deltas, 0.75)
    ci = bootstrap_ci(deltas)
    sign_p = exact_sign_test_two_sided(wins, losses)
    w_plus, w_minus, n_nonzero, wilcox_p = wilcoxon_signed_rank_normal_approx(deltas)

    return {
        "policy_a": policy_a,
        "policy_b": policy_b,
        "metric": metric,
        "n_pairs": n,
        "a_better_wins": wins,
        "ties": ties,
        "a_worse_losses": losses,
        "mean_soc_improvement_a_over_b": round(mean_d, 4),
        "mean_pct_initial_soc_improvement": round(sum(pct_deltas) / len(pct_deltas), 4) if pct_deltas else 0.0,
        "median_soc_improvement_a_over_b": round(med_d, 4),
        "iqr_soc_improvement": [round(q25, 4), round(q75, 4)],
        "min_soc_improvement": min(deltas) if deltas else 0,
        "max_soc_improvement": max(deltas) if deltas else 0,
        "bootstrap_95ci_mean": ci["mean_ci"],
        "bootstrap_95ci_median": ci["median_ci"],
        "exact_sign_test_p": round(sign_p, 6),
        "wilcoxon_w_plus": w_plus,
        "wilcoxon_w_minus": w_minus,
        "wilcoxon_n_nonzero": n_nonzero,
        "wilcoxon_p": wilcox_p,
    }


def main():
    args = parse_args()
    pilot_dir = Path(args.pilot_dir)
    manifest_path = pilot_dir / "pilot_manifest.json"
    runs_path = pilot_dir / "all_runs.json"
    offline_path = Path(args.offline_summary)

    with open(manifest_path, "r") as f:
        manifest = json.load(f)
    with open(runs_path, "r") as f:
        all_runs = json.load(f)
    with open(offline_path, "r") as f:
        offline = json.load(f)

    # Group runs by policy and by complete experimental key
    runs_by_policy = defaultdict(list)
    pairs_all = defaultdict(dict)
    pairs_by_map = defaultdict(lambda: defaultdict(dict))
    pairs_by_budget = defaultdict(lambda: defaultdict(dict))
    pairs_by_agents = defaultdict(lambda: defaultdict(dict))

    for r in all_runs:
        pol = r["policy"]
        runs_by_policy[pol].append(r)
        key = (
            r["map_stem"],
            r["scen_stem"],
            r["num_agents"],
            r["seed"],
            r["time_budget_s"],
            r["slice_budget_s"],
        )
        pairs_all[key][pol] = r
        pairs_by_map[r["map_stem"]][key][pol] = r
        pairs_by_budget[str(r["time_budget_s"])][key][pol] = r
        pairs_by_agents[str(r["num_agents"])][key][pol] = r

    policy_order = [
        "DISCARD_NO_PA",
        "RESUMPTION_NO_PA",
        "GREEDY_PARTIAL_ACCEPTANCE",
        "EXACT_PARTIAL_ACCEPTANCE",
    ]

    policy_summaries = {}
    for pol in policy_order:
        recs = runs_by_policy[pol]
        n = len(recs)
        reds_dl = sorted(r["cost_reduction_at_deadline"] for r in recs)
        reds_fin = sorted(r["cost_reduction"] for r in recs)
        attempts = sorted(r["total_repair_attempts"] for r in recs)
        Interrupts = sorted(r["total_interrupted_repairs"] for r in recs)
        int_comp = sorted(r["interruptions_with_complete_paths"] for r in recs)
        pa_att = sum(r["pa_attempts"] for r in recs)
        pa_pos = sum(r["pa_positive_opportunities"] for r in recs)
        pa_com = sum(r["pa_committed"] for r in recs)
        pa_skip = sum(r["pa_budget_skipped"] for r in recs)
        pa_gain = sum(r["pa_total_gain"] for r in recs)
        pa_time_s = sum(r["pa_total_time_us"] for r in recs) / 1e6
        tot_solve_s = sum(r["total_solve_time_s"] for r in recs)
        overshoots_us = sorted(r["deadline_overshoot_s"] * 1e6 for r in recs)
        dl_vs_fin_diff = sum(1 for r in recs if r["cost_at_deadline"] != r["final_cost"])

        gb_meds = sorted(r["pa_telemetry"]["graph_construction_time_us"]["median"] for r in recs if r["pa_attempts"] > 0)
        sel_meds = sorted(r["pa_telemetry"]["selection_time_us"]["median"] for r in recs if r["pa_attempts"] > 0)
        val_meds = sorted(r["pa_telemetry"]["validation_time_us"]["median"] for r in recs if r["pa_committed"] > 0)
        tot_meds = sorted(r["pa_telemetry"]["total_pa_overhead_us"]["median"] for r in recs if r["pa_attempts"] > 0)
        tot_p95s = sorted(r["pa_telemetry"]["total_pa_overhead_us"]["p95"] for r in recs if r["pa_attempts"] > 0)
        tot_maxs = sorted(r["pa_telemetry"]["total_pa_overhead_us"]["max"] for r in recs if r["pa_attempts"] > 0)

        policy_summaries[pol] = {
            "runs": n,
            "valid_runs": sum(1 for r in recs if r["plan_valid"] and r["cli_validator_passed"]),
            "mean_initial_cost": round(sum(r["initial_cost"] for r in recs) / n, 2),
            "mean_cost_at_deadline": round(sum(r["cost_at_deadline"] for r in recs) / n, 2),
            "mean_final_cost": round(sum(r["final_cost"] for r in recs) / n, 2),
            "cost_at_deadline_ne_final_cost_count": dl_vs_fin_diff,
            "mean_cost_reduction_at_deadline": round(sum(reds_dl) / n, 2),
            "median_cost_reduction_at_deadline": round(quantile(reds_dl, 0.5), 2),
            "iqr_cost_reduction_at_deadline": [round(quantile(reds_dl, 0.25), 2), round(quantile(reds_dl, 0.75), 2)],
            "mean_cost_reduction_final": round(sum(reds_fin) / n, 2),
            "mean_repair_attempts": round(sum(attempts) / n, 1),
            "median_repair_attempts": round(quantile(attempts, 0.5), 1),
            "mean_interrupted_repairs": round(sum(Interrupts) / n, 1),
            "mean_interruptions_with_complete_paths": round(sum(int_comp) / n, 1),
            "mean_accepted_repairs": round(sum(r["accepted_repairs"] for r in recs) / n, 2),
            "total_pa_attempts": pa_att,
            "total_pa_positive_opportunities": pa_pos,
            "total_pa_committed": pa_com,
            "total_pa_budget_skipped": pa_skip,
            "total_pa_gain": pa_gain,
            "total_pa_time_s": round(pa_time_s, 4),
            "pa_share_of_solve_time_pct": round(100.0 * pa_time_s / tot_solve_s, 3) if tot_solve_s > 0 else 0.0,
            "pa_candidates_evaluated": sum(r["pa_telemetry"]["total_candidates_evaluated"] for r in recs),
            "pa_positive_weights": sum(r["pa_telemetry"]["positive_weights"] for r in recs),
            "pa_zero_weights": sum(r["pa_telemetry"]["zero_weights"] for r in recs),
            "pa_negative_weights": sum(r["pa_telemetry"]["negative_weights"] for r in recs),
            "pa_feasible_negative_weights": sum(r["pa_telemetry"]["feasible_negative_weights"] for r in recs),
            "pa_nonnegative_fast_path_eligible": sum(r["pa_telemetry"]["nonnegative_fast_path_eligible_count"] for r in recs),
            "pa_selected_positive_weights": sum(r["pa_telemetry"]["selected_positive_weights"] for r in recs),
            "pa_selected_zero_weights": sum(r["pa_telemetry"]["selected_zero_weights"] for r in recs),
            "pa_selected_negative_weights": sum(r["pa_telemetry"]["selected_negative_weights"] for r in recs),
            "median_run_graph_build_us": round(quantile(gb_meds, 0.5), 2) if gb_meds else 0.0,
            "median_run_selection_us": round(quantile(sel_meds, 0.5), 3) if sel_meds else 0.0,
            "median_run_validation_us": round(quantile(val_meds, 0.5), 2) if val_meds else 0.0,
            "median_run_total_pa_us": round(quantile(tot_meds, 0.5), 2) if tot_meds else 0.0,
            "p95_run_total_pa_us": round(quantile(tot_p95s, 0.95), 2) if tot_p95s else 0.0,
            "max_run_total_pa_us": round(max(tot_maxs), 2) if tot_maxs else 0.0,
            "deadline_exceeded_runs": sum(1 for r in recs if r["deadline_exceeded_flag"]),
            "median_deadline_overshoot_us": round(quantile(overshoots_us, 0.5), 2),
            "p95_deadline_overshoot_us": round(quantile(overshoots_us, 0.95), 2),
            "max_deadline_overshoot_us": round(max(overshoots_us), 2) if overshoots_us else 0.0,
        }

    comparisons_overall = [
        compute_paired_stats(pairs_all, "EXACT_PARTIAL_ACCEPTANCE", "DISCARD_NO_PA"),
        compute_paired_stats(pairs_all, "EXACT_PARTIAL_ACCEPTANCE", "RESUMPTION_NO_PA"),
        compute_paired_stats(pairs_all, "EXACT_PARTIAL_ACCEPTANCE", "GREEDY_PARTIAL_ACCEPTANCE"),
        compute_paired_stats(pairs_all, "GREEDY_PARTIAL_ACCEPTANCE", "RESUMPTION_NO_PA"),
        compute_paired_stats(pairs_all, "RESUMPTION_NO_PA", "DISCARD_NO_PA"),
    ]

    comparisons_by_map = {}
    for m_name, m_pairs in sorted(pairs_by_map.items()):
        comparisons_by_map[m_name] = [
            compute_paired_stats(m_pairs, "EXACT_PARTIAL_ACCEPTANCE", "DISCARD_NO_PA"),
            compute_paired_stats(m_pairs, "EXACT_PARTIAL_ACCEPTANCE", "RESUMPTION_NO_PA"),
            compute_paired_stats(m_pairs, "EXACT_PARTIAL_ACCEPTANCE", "GREEDY_PARTIAL_ACCEPTANCE"),
            compute_paired_stats(m_pairs, "GREEDY_PARTIAL_ACCEPTANCE", "RESUMPTION_NO_PA"),
        ]

    comparisons_by_budget = {}
    for b_str, b_pairs in sorted(pairs_by_budget.items()):
        comparisons_by_budget[b_str] = [
            compute_paired_stats(b_pairs, "EXACT_PARTIAL_ACCEPTANCE", "DISCARD_NO_PA"),
            compute_paired_stats(b_pairs, "EXACT_PARTIAL_ACCEPTANCE", "RESUMPTION_NO_PA"),
            compute_paired_stats(b_pairs, "EXACT_PARTIAL_ACCEPTANCE", "GREEDY_PARTIAL_ACCEPTANCE"),
        ]

    comparisons_by_agents = {}
    for a_str, a_pairs in sorted(pairs_by_agents.items()):
        comparisons_by_agents[a_str] = [
            compute_paired_stats(a_pairs, "EXACT_PARTIAL_ACCEPTANCE", "DISCARD_NO_PA"),
            compute_paired_stats(a_pairs, "EXACT_PARTIAL_ACCEPTANCE", "RESUMPTION_NO_PA"),
            compute_paired_stats(a_pairs, "EXACT_PARTIAL_ACCEPTANCE", "GREEDY_PARTIAL_ACCEPTANCE"),
        ]

    analysis_out = {
        "manifest": manifest,
        "offline_opportunity_summary": {
            k: v for k, v in offline.items() if k != "cell_summaries"
        },
        "online_policy_summaries": policy_summaries,
        "paired_comparisons_overall": comparisons_overall,
        "paired_comparisons_by_map": comparisons_by_map,
        "paired_comparisons_by_budget": comparisons_by_budget,
        "paired_comparisons_by_agents": comparisons_by_agents,
    }

    out_json_path = Path(args.output_json)
    out_json_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_json_path, "w") as f:
        json.dump(analysis_out, f, indent=2)

    # Generate comprehensive Markdown analysis report
    md = []
    md.append("# RouteBudget Lab — Partial-Acceptance Empirical & Statistical Analysis")
    md.append("")
    md.append(f"- **Experiment ID:** `{manifest['experiment_name']}` (v{manifest['version']})")
    md.append(f"- **Git Revision:** `{manifest['git'].get('commit', 'unknown')}` on branch `{manifest['git'].get('branch', 'unknown')}`")
    md.append(f"- **Config Fingerprint (SHA-256):** `{manifest['config_sha256']}`")
    md.append(f"- **Run Reconciliation:** Expected = `{manifest['reconciliation']['expected_runs']}`, Completed = `{manifest['reconciliation']['completed_runs']}`, Validated (Internal + CLI `rbl_validate`) = `{manifest['reconciliation']['valid_runs']}`, Failed = `{manifest['reconciliation']['failed_runs']}`")
    md.append("")
    md.append("## 1. Offline Opportunity Analysis (24 Validation Cells, $1.0\\text{ s}$ Slice-5ms Collection)")
    md.append("")
    md.append("Natural interrupted repair snapshots were captured under `RESUMPTION_NO_PA` (with partial acceptance disabled so the search trajectory is unperturbed) and replayed offline through all five selectors:")
    md.append("")
    md.append("| Metric | Total Across 24 Cells | `random-64-64-10` (12 cells) | `warehouse-10-20-10-2-2` (12 cells) |")
    md.append("|---|---:|---:|---:|")
    r_map = offline["by_map"].get("random-64-64-10", {})
    w_map = offline["by_map"].get("warehouse-10-20-10-2-2", {})
    md.append(f"| Total Repair Attempts | {offline['total_repair_attempts']} | {r_map.get('total_repair_attempts', 0)} | {w_map.get('total_repair_attempts', 0)} |")
    md.append(f"| Interrupted Slices Observed | {offline['total_interruptions_observed']} | {r_map.get('total_interruptions_observed', 0)} | {w_map.get('total_interruptions_observed', 0)} |")
    md.append(f"| Snapshots with Completed Prefix ($r_{{\\text{{done}}}} \\ge 1$) | {offline['total_snapshots_with_completed_prefix']} | {r_map.get('snapshots_captured', 0)} | {w_map.get('snapshots_captured', 0)} |")
    md.append(f"| Total Candidate Paths Evaluated | {offline['total_candidates_evaluated']} | {r_map.get('positive_weights', 0) + r_map.get('zero_weights', 0) + r_map.get('negative_weights', 0)} | {w_map.get('positive_weights', 0) + w_map.get('zero_weights', 0) + w_map.get('negative_weights', 0)} |")
    md.append(f"| Positive-Weight Candidates ($w_i > 0$) | {offline['weight_counts']['positive']} | {r_map.get('positive_weights', 0)} | {w_map.get('positive_weights', 0)} |")
    md.append(f"| Zero-Weight Candidates ($w_i = 0$) | {offline['weight_counts']['zero']} | {r_map.get('zero_weights', 0)} | {w_map.get('zero_weights', 0)} |")
    md.append(f"| Negative-Weight Candidates ($w_i < 0$) | {offline['weight_counts']['negative']} | {r_map.get('negative_weights', 0)} | {w_map.get('negative_weights', 0)} |")
    md.append(f"| Feasible Negative-Weight Candidates | {offline['feasible_negative_candidates']} | {r_map.get('feasible_negative', 0)} | {w_map.get('feasible_negative', 0)} |")
    md.append(f"| Forbidden Candidates (Outside-Neighborhood Conflict) | {offline['forbidden_candidates']} | {r_map.get('forbidden_nodes', 0)} | {w_map.get('forbidden_nodes', 0)} |")
    md.append(f"| Transitively Excluded Candidates | {offline['transitively_excluded_candidates']} | {r_map.get('transitively_excluded', 0)} | {w_map.get('transitively_excluded', 0)} |")
    md.append(f"| Dependency Arcs Constructed | {offline['dependency_arcs']} | {r_map.get('dependency_arcs', 0)} | {w_map.get('dependency_arcs', 0)} |")
    md.append(f"| Snapshots with Positive Feasible Gain ($\\Delta > 0$) | **{offline['snapshots_with_positive_exact_gain']}** | **{r_map.get('positive_exact_snapshots', 0)}** | **{w_map.get('positive_exact_snapshots', 0)}** |")
    md.append(f"| Cumulative Immediate Gain — `No-PA` | 0 | 0 | 0 |")
    md.append(f"| Cumulative Immediate Gain — `Greedy` | {offline['total_gain_greedy']} | {r_map.get('total_gain_greedy', 0)} | {w_map.get('total_gain_greedy', 0)} |")
    md.append(f"| Cumulative Immediate Gain — `Exact Closure` | {offline['total_gain_exact']} | {r_map.get('total_gain_exact', 0)} | {w_map.get('total_gain_exact', 0)} |")
    md.append(f"| Cumulative Immediate Gain — `Nonnegative Ref` | {offline['total_gain_nonneg_ref']} | {r_map.get('total_gain_nonneg_ref', 0)} | {w_map.get('total_gain_nonneg_ref', 0)} |")
    md.append(f"| Cumulative Immediate Gain — `Exhaustive 2^r Oracle` | {offline['total_gain_exhaustive_oracle']} | {r_map.get('total_gain_oracle', 0)} | {w_map.get('total_gain_oracle', 0)} |")
    md.append(f"| Snapshots where `Exact > Greedy` | {offline['snapshots_exact_gt_greedy']} | {r_map.get('exact_gt_greedy_snapshots', 0)} | {w_map.get('exact_gt_greedy_snapshots', 0)} |")
    md.append(f"| Negative Candidates Selected by `Exact` | {offline['selected_negative_candidates_exact']} | {r_map.get('selected_negative_exact', 0)} | {w_map.get('selected_negative_exact', 0)} |")
    md.append(f"| `Exact` vs `Exhaustive Oracle` Mismatches | **{offline['snapshots_exact_ne_oracle']}** | **{r_map.get('oracle_mismatches', 0)}** | **{w_map.get('oracle_mismatches', 0)}** |")
    md.append("")
    md.append("### 1.1 Breakdown by Completed Prefix Length ($r_{\\text{done}}$)")
    md.append("")
    md.append("| $r_{\\text{done}}$ | Snapshots | Pos Weights | Zero Weights | Neg Weights | Forbidden | Trans. Excluded | Dep. Arcs | Pos Gain Snaps | Exact Gain | Greedy Gain | Nonneg Ref Gain | Oracle Gain |")
    md.append("|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for r_key in sorted(offline["by_r_done"].keys(), key=lambda x: int(x)):
        row = offline["by_r_done"][r_key]
        md.append(
            f"| {row['r_done']} | {row['snapshots']} | {row['positive_weights']} | {row['zero_weights']} | "
            f"{row['negative_weights']} | {row['forbidden_nodes']} | {row['transitively_excluded']} | "
            f"{row['dependency_arcs']} | {row['positive_exact_snapshots']} | {row['total_gain_exact']} | "
            f"{row['total_gain_greedy']} | {row['total_gain_nonneg_ref']} | {row['total_gain_oracle']} |"
        )
    md.append("")
    md.append("## 2. Matched 192-Run Live Pilot Summary")
    md.append("")
    md.append("| Policy | Runs (Valid) | Mean Init SoC | Mean SoC @ Deadline | Mean Final SoC | Mean Red. @ Deadline | Median Red. (IQR) | Mean Attempts | PA Commits (Gain) | PA Time % | Median PA Latency ($\\mu$s) |")
    md.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for pol in policy_order:
        s = policy_summaries[pol]
        iqr_str = f"{s['iqr_cost_reduction_at_deadline'][0]:.1f}–{s['iqr_cost_reduction_at_deadline'][1]:.1f}"
        md.append(
            f"| `{pol}` | {s['runs']} ({s['valid_runs']}) | {s['mean_initial_cost']:.2f} | "
            f"{s['mean_cost_at_deadline']:.2f} | {s['mean_final_cost']:.2f} | "
            f"**{s['mean_cost_reduction_at_deadline']:.2f}** | {s['median_cost_reduction_at_deadline']:.1f} ({iqr_str}) | "
            f"{s['mean_repair_attempts']:.1f} | {s['total_pa_committed']} ({s['total_pa_gain']}) | "
            f"{s['pa_share_of_solve_time_pct']:.2f}% | {s['median_run_total_pa_us']:.2f} |"
        )
    md.append("")
    md.append("## 3. Paired Statistical Comparisons ($N = 48$ Matched Instance-Seed-Budget Pairs)")
    md.append("")
    md.append("Positive $\\Delta\\text{SoC} = \\text{SoC}(B) - \\text{SoC}(A)$ indicates Policy $A$ achieved lower (better) Sum of Costs at the deadline than Policy $B$.")
    md.append("")
    md.append("| Comparison ($A$ vs $B$) | Pairs | Wins / Ties / Losses | Mean $\\Delta\\text{SoC}$ [95% Boot CI] | Median $\\Delta\\text{SoC}$ (IQR) | Sign Test $p$ | Wilcoxon $p$ ($n_{\\neq 0}$) |")
    md.append("|---|---:|---:|---:|---:|---:|---:|")
    for c in comparisons_overall:
        ci_str = f"[{c['bootstrap_95ci_mean'][0]:+.2f}, {c['bootstrap_95ci_mean'][1]:+.2f}]"
        iqr_str = f"{c['iqr_soc_improvement'][0]:+.1f} to {c['iqr_soc_improvement'][1]:+.1f}"
        md.append(
            f"| `{c['policy_a']}` vs `{c['policy_b']}` | {c['n_pairs']} | "
            f"{c['a_better_wins']} / {c['ties']} / {c['a_worse_losses']} | "
            f"**{c['mean_soc_improvement_a_over_b']:+.2f}** {ci_str} | "
            f"{c['median_soc_improvement_a_over_b']:+.1f} ({iqr_str}) | "
            f"{c['exact_sign_test_p']:.4f} | {c['wilcoxon_p']:.4f} ({c['wilcoxon_n_nonzero']}) |"
        )
    md.append("")
    md.append("### 3.1 Stratified Paired Comparisons by Map ($N = 24$ Pairs per Map)")
    md.append("")
    md.append("| Map | Comparison ($A$ vs $B$) | Wins / Ties / Losses | Mean $\\Delta\\text{SoC}$ [95% Boot CI] | Median $\\Delta\\text{SoC}$ | Sign Test $p$ | Wilcoxon $p$ |")
    md.append("|---|---|---:|---:|---:|---:|---:|")
    for m_name, comp_list in sorted(comparisons_by_map.items()):
        for c in comp_list:
            ci_str = f"[{c['bootstrap_95ci_mean'][0]:+.2f}, {c['bootstrap_95ci_mean'][1]:+.2f}]"
            md.append(
                f"| `{m_name}` | `{c['policy_a']}` vs `{c['policy_b']}` | "
                f"{c['a_better_wins']} / {c['ties']} / {c['a_worse_losses']} | "
                f"**{c['mean_soc_improvement_a_over_b']:+.2f}** {ci_str} | "
                f"{c['median_soc_improvement_a_over_b']:+.1f} | {c['exact_sign_test_p']:.4f} | {c['wilcoxon_p']:.4f} |"
            )
    md.append("")
    md.append("### 3.2 Stratified Paired Comparisons by Budget ($N = 24$ Pairs per Budget)")
    md.append("")
    md.append("| Budget (s) | Comparison ($A$ vs $B$) | Wins / Ties / Losses | Mean $\\Delta\\text{SoC}$ [95% Boot CI] | Median $\\Delta\\text{SoC}$ | Sign Test $p$ | Wilcoxon $p$ |")
    md.append("|---|---:|---:|---:|---:|---:|---:|")
    for b_str, comp_list in sorted(comparisons_by_budget.items()):
        for c in comp_list:
            ci_str = f"[{c['bootstrap_95ci_mean'][0]:+.2f}, {c['bootstrap_95ci_mean'][1]:+.2f}]"
            md.append(
                f"| `{b_str}s` | `{c['policy_a']}` vs `{c['policy_b']}` | "
                f"{c['a_better_wins']} / {c['ties']} / {c['a_worse_losses']} | "
                f"**{c['mean_soc_improvement_a_over_b']:+.2f}** {ci_str} | "
                f"{c['median_soc_improvement_a_over_b']:+.1f} | {c['exact_sign_test_p']:.4f} | {c['wilcoxon_p']:.4f} |"
            )
    md.append("")
    md.append("## 4. Online Overhead, Telemetry & Necessity of General Max-Flow")
    md.append("")
    ex_s = policy_summaries["EXACT_PARTIAL_ACCEPTANCE"]
    gr_s = policy_summaries["GREEDY_PARTIAL_ACCEPTANCE"]
    md.append(f"- **Online Candidate Weight Distribution (`EXACT_PARTIAL_ACCEPTANCE` across 48 runs):**")
    md.append(f"  - Total PA evaluations: `{ex_s['total_pa_attempts']}`, total candidate paths: `{ex_s['pa_candidates_evaluated']}`")
    md.append(f"  - Weight signs: `{ex_s['pa_positive_weights']}` positive ($w_i > 0$), `{ex_s['pa_zero_weights']}` zero ($w_i = 0$), `{ex_s['pa_negative_weights']}` negative ($w_i < 0$, of which `{ex_s['pa_feasible_negative_weights']}` were non-forbidden/feasible).")
    md.append(f"  - Nonnegative fast-path eligibility (`feasible_negative_weights == 0`): `{ex_s['pa_nonnegative_fast_path_eligible']}` / `{ex_s['total_pa_attempts']}` attempts ({100.0 * ex_s['pa_nonnegative_fast_path_eligible'] / max(1, ex_s['total_pa_attempts']):.2f}%).")
    md.append(f"  - Selected replacement weights in committed closures: `{ex_s['pa_selected_positive_weights']}` positive, `{ex_s['pa_selected_zero_weights']}` zero, `{ex_s['pa_selected_negative_weights']}` negative.")
    md.append(f"- **Latency & Deadline Accounting (`EXACT_PARTIAL_ACCEPTANCE`):**")
    md.append(f"  - Median graph build time: `{ex_s['median_run_graph_build_us']:.2f} us`, median Dinic selection time: `{ex_s['median_run_selection_us']:.3f} us`, median whole-plan validation time (on commit): `{ex_s['median_run_validation_us']:.2f} us`, median total PA overhead per attempt: `{ex_s['median_run_total_pa_us']:.2f} us` (p95: `{ex_s['p95_run_total_pa_us']:.2f} us`, max: `{ex_s['max_run_total_pa_us']:.2f} us`).")
    md.append(f"  - `cost_at_deadline != final_cost` occurrences: `{ex_s['cost_at_deadline_ne_final_cost_count']}` / `{ex_s['runs']}` runs.")
    md.append("")

    out_md_path = Path(args.output_md)
    out_md_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_md_path, "w") as f:
        f.write("\n".join(md) + "\n")

    print(f"Saved statistical analysis JSON to {out_json_path}")
    print(f"Saved markdown report to {out_md_path}")


if __name__ == "__main__":
    main()
