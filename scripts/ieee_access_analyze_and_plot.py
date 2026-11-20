#!/usr/bin/env python3
"""
ieee_access_analyze_and_plot.py
Computes exact paired statistics (Win/Tie/Loss, Mean/Median Delta SoC, exact two-sided
binomial sign test, Wilcoxon signed-rank test, Holm-Bonferroni correction, per-map breakdown,
deadline overshoot telemetry) on:
  1) 384-run Development dataset: completion_20261110_1146/analysis/bounded_eval_per_run.csv
  2) 240-run Unseen-Map Confirmation dataset: results/ieee_access_final/unseen_per_run.csv
  3) 20,694-snapshot Opportunity Audit: results/ieee_access_final/opportunity_audit_summary.csv

Exports:
  - results/ieee_access_final/paired_summary_all.csv
  - results/ieee_access_final/per_map_summary.csv
  - results/ieee_access_final/ieee_access_metrics.json
  - paper/figures/fig1_opportunity_audit.{pdf,png}
  - paper/figures/fig2_paired_delta_soc.{pdf,png}
  - paper/figures/fig3_per_map_comparison.{pdf,png}
  - paper/figures/fig4_overshoot_and_throughput.{pdf,png}
"""

import csv
import json
from collections import defaultdict
from pathlib import Path
import numpy as np
import pandas as pd
from scipy.stats import binomtest, wilcoxon
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

REPO_ROOT = Path(__file__).resolve().parent.parent
RES_DIR = REPO_ROOT / "results" / "ieee_access_final"
FIG_DIR = REPO_ROOT / "paper" / "figures"

# Colorblind-safe Okabe-Ito palette
OKABE_BLUE = "#0072B2"
OKABE_ORANGE = "#D55E00"
OKABE_GREEN = "#009E73"
OKABE_PURPLE = "#CC79A7"
OKABE_SKY = "#56B4E9"
OKABE_GREY = "#7F7F7F"


def holm_bonferroni(p_values):
    m = len(p_values)
    indexed = sorted(enumerate(p_values), key=lambda x: x[1])
    adjusted = [1.0] * m
    running_max = 0.0
    for rank, (orig_idx, p) in enumerate(indexed):
        adj = min(1.0, (m - rank) * p)
        running_max = max(running_max, adj)
        adjusted[orig_idx] = running_max
    return adjusted


def compute_paired_stats(df, key_cols, treat_pol, ref_pol, label, study_name):
    grouped = df.groupby(key_cols + ["policy"])["cost_at_deadline"].mean().unstack("policy")
    if treat_pol not in grouped.columns or ref_pol not in grouped.columns:
        return None, []
    valid = grouped.dropna(subset=[treat_pol, ref_pol])
    deltas = (valid[treat_pol] - valid[ref_pol]).to_numpy(dtype=float)
    rel_pcts = (100.0 * (valid[ref_pol] - valid[treat_pol]) / valid[ref_pol]).to_numpy(dtype=float)

    n = len(deltas)
    wins = int(np.sum(deltas < -1e-9))
    ties = int(np.sum(np.abs(deltas) <= 1e-9))
    losses = int(np.sum(deltas > 1e-9))
    trials = wins + losses
    sign_p = float(binomtest(wins, trials, 0.5, alternative="two-sided").pvalue) if trials > 0 else 1.0
    nonzero = deltas[np.abs(deltas) > 1e-9]
    if len(nonzero) >= 1:
        wilcoxon_p = float(wilcoxon(nonzero, zero_method="wilcox", alternative="two-sided").pvalue)
    else:
        wilcoxon_p = 1.0

    rec = {
        "study": study_name,
        "comparison": label,
        "treatment": treat_pol,
        "reference": ref_pol,
        "n_conditions": n,
        "wins": wins,
        "ties": ties,
        "losses": losses,
        "mean_delta_soc": float(np.mean(deltas)),
        "median_delta_soc": float(np.median(deltas)),
        "std_delta_soc": float(np.std(deltas, ddof=1)) if n > 1 else 0.0,
        "mean_rel_imp_pct": float(np.mean(rel_pcts)),
        "sign_p": sign_p,
        "wilcoxon_p": wilcoxon_p,
    }
    return rec, deltas


def main():
    FIG_DIR.mkdir(parents=True, exist_ok=True)
    RES_DIR.mkdir(parents=True, exist_ok=True)

    dev_csv = REPO_ROOT / "completion_20261110_1146" / "analysis" / "bounded_eval_per_run.csv"
    unseen_csv = RES_DIR / "unseen_per_run.csv"
    audit_json = RES_DIR / "opportunity_audit_summary.json"

    df_dev = pd.read_csv(dev_csv)
    df_dev["map_name"] = df_dev["map"].apply(lambda x: Path(x).stem)
    df_dev["scen_name"] = df_dev["scen"].apply(lambda x: Path(x).name)
    df_dev["overshoot_ms"] = df_dev["deadline_overshoot_s"] * 1000.0

    df_unseen = pd.read_csv(unseen_csv)
    df_unseen["map_name"] = df_unseen["map"]
    df_unseen["scen_name"] = df_unseen["scen"]
    df_unseen["overshoot_ms"] = df_unseen["deadline_overshoot_ms"]

    dev_keys = ["map_name", "scen_name", "num_agents", "time_budget_s", "seed"]
    unseen_keys = ["map_name", "scen_name", "num_agents", "time_budget_s", "seed"]

    comparisons = [
        (df_dev, dev_keys, "RESUMPTION_NO_PA", "DISCARD_NO_PA", "Resumption vs. Restart-Only", "Dev (384 runs)"),
        (df_dev, dev_keys, "RESUMPTION_NO_PA", "UNINTERRUPTED_REPAIR", "Resumption vs. Unsliced LNS", "Dev (384 runs)"),
        (df_dev, dev_keys, "PA_EVERY_INTERRUPTION_FILTERED", "RESUMPTION_NO_PA", "Selective Recovery vs. Resumption", "Dev (384 runs)"),
        (df_unseen, unseen_keys, "RESUMPTION_NO_PA", "DISCARD_NO_PA", "Resumption vs. Restart-Only", "Unseen (240 runs)"),
        (df_unseen, unseen_keys, "RESUMPTION_NO_PA", "UNINTERRUPTED_REPAIR", "Resumption vs. Unsliced LNS", "Unseen (240 runs)"),
        (df_unseen, unseen_keys, "GATED_BLOCKER_CLOSURE", "RESUMPTION_NO_PA", "Gated Blocker-Closure vs. Resumption", "Unseen (240 runs)"),
        (df_unseen, unseen_keys, "GATED_BLOCKER_CLOSURE", "DISCARD_NO_PA", "Gated Blocker-Closure vs. Restart-Only", "Unseen (240 runs)"),
    ]

    paired_records = []
    delta_vectors = {}
    for df, keys, t_pol, r_pol, label, study in comparisons:
        rec, deltas = compute_paired_stats(df, keys, t_pol, r_pol, label, study)
        if rec is not None:
            paired_records.append(rec)
            delta_vectors[(study, label)] = deltas

    # Holm-Bonferroni within each study family
    for study in ["Dev (384 runs)", "Unseen (240 runs)"]:
        idxs = [i for i, r in enumerate(paired_records) if r["study"] == study]
        adj_sign = holm_bonferroni([paired_records[i]["sign_p"] for i in idxs])
        adj_wil = holm_bonferroni([paired_records[i]["wilcoxon_p"] for i in idxs])
        for k, i in enumerate(idxs):
            paired_records[i]["sign_p_holm"] = adj_sign[k]
            paired_records[i]["wilcoxon_p_holm"] = adj_wil[k]

    pd.DataFrame(paired_records).to_csv(RES_DIR / "paired_summary_all.csv", index=False)

    # Per-map breakdown across all 6 maps
    per_map_rows = []
    for study, df, keys, extra_pol in [
        ("Dev", df_dev, dev_keys, "PA_EVERY_INTERRUPTION_FILTERED"),
        ("Unseen", df_unseen, unseen_keys, "GATED_BLOCKER_CLOSURE"),
    ]:
        for m in sorted(df["map_name"].unique()):
            sub = df[df["map_name"] == m]
            for t_pol, r_pol, label in [
                ("RESUMPTION_NO_PA", "DISCARD_NO_PA", "Resumption vs. Restart-Only"),
                ("RESUMPTION_NO_PA", "UNINTERRUPTED_REPAIR", "Resumption vs. Unsliced LNS"),
                (extra_pol, "RESUMPTION_NO_PA", f"{extra_pol} vs. Resumption"),
            ]:
                rec, _ = compute_paired_stats(sub, keys, t_pol, r_pol, label, f"{study}:{m}")
                if rec:
                    rec["map"] = m
                    per_map_rows.append(rec)
    pd.DataFrame(per_map_rows).to_csv(RES_DIR / "per_map_summary.csv", index=False)

    # Timing & telemetry summaries
    telemetry = {}
    for study, df in [("dev", df_dev), ("unseen", df_unseen)]:
        pol_stats = {}
        for pol, grp in df.groupby("policy"):
            pol_stats[pol] = {
                "runs": int(len(grp)),
                "valid_runs": int(grp["plan_valid"].sum()),
                "mean_iterations": float(grp["total_iterations"].mean()),
                "mean_resumed": float(grp["resumed_actions_count"].mean()),
                "mean_overshoot_ms": float(grp["overshoot_ms"].mean()),
                "p95_overshoot_ms": float(np.percentile(grp["overshoot_ms"], 95)),
                "max_overshoot_ms": float(grp["overshoot_ms"].max()),
                "overshoots_gt_1us": int((grp["overshoot_ms"] > 0.001).sum()),
                "pa_attempts_sum": int(grp["pa_attempts"].sum()),
                "pa_committed_sum": int(grp["pa_committed"].sum()),
                "pa_total_gain_sum": int(grp["pa_total_gain"].sum()),
                "filter_skips_sum": int(grp["filter_skips"].sum()) if "filter_skips" in grp else 0,
            }
        telemetry[study] = pol_stats

    audit_data = json.loads(audit_json.read_text()) if audit_json.exists() else {}
    summary_payload = {
        "paired_comparisons": paired_records,
        "per_map": per_map_rows,
        "telemetry": telemetry,
        "opportunity_audit": audit_data.get("totals", {}),
    }
    (RES_DIR / "ieee_access_metrics.json").write_text(json.dumps(summary_payload, indent=2))

    # --- 300-DPI Publication Figures ---
    plt.rcParams.update({"font.size": 10, "font.family": "sans-serif", "axes.spines.top": False, "axes.spines.right": False})

    # Figure 1: Opportunity Audit across 4 unseen maps
    if audit_data.get("per_cell"):
        df_aud = pd.DataFrame(audit_data["per_cell"])
        by_map = df_aud.groupby("map")[["snapshots_with_prefix", "b0_count", "b1_count", "b2_count", "b_ge_3_count"]].sum()
        maps = list(by_map.index)
        b0_pct = 100.0 * by_map["b0_count"] / by_map["snapshots_with_prefix"]
        b1_pct = 100.0 * by_map["b1_count"] / by_map["snapshots_with_prefix"]
        b2_pct = 100.0 * by_map["b2_count"] / by_map["snapshots_with_prefix"]
        b3_pct = 100.0 * by_map["b_ge_3_count"] / by_map["snapshots_with_prefix"]

        fig, ax = plt.subplots(figsize=(7.2, 3.8))
        x = np.arange(len(maps))
        ax.bar(x, b0_pct, label="|B| = 0 (Direct Closure)", color=OKABE_GREEN)
        ax.bar(x, b1_pct, bottom=b0_pct, label="|B| = 1 Blocker", color=OKABE_BLUE)
        ax.bar(x, b2_pct, bottom=b0_pct + b1_pct, label="|B| = 2 Blockers", color=OKABE_SKY)
        ax.bar(x, b3_pct, bottom=b0_pct + b1_pct + b2_pct, label="|B| >= 3 Blockers", color=OKABE_ORANGE)
        ax.set_xticks(x)
        ax.set_xticklabels(maps, rotation=15, ha="right")
        ax.set_ylabel("Preempted Slices (% of 20,694)")
        ax.set_title("Opportunity Audit on Unseen Moving AI Maps (Delta_max > 0)")
        ax.legend(frameon=False, ncol=2)
        ax.grid(axis="y", linestyle=":", alpha=0.5)
        fig.tight_layout()
        for ext in ("pdf", "png"):
            fig.savefig(FIG_DIR / f"fig1_opportunity_audit.{ext}", dpi=300)
        plt.close(fig)

    # Figure 2: Paired Delta SoC (Dev vs Unseen)
    fig, ax = plt.subplots(figsize=(7.4, 4.0))
    box_labels = [
        "Dev: Resume\nvs Restart",
        "Dev: Resume\nvs Unsliced",
        "Dev: Selective\nvs Resume",
        "Unseen: Resume\nvs Restart",
        "Unseen: Resume\nvs Unsliced",
        "Unseen: Gated\nvs Resume",
    ]
    box_data = [
        delta_vectors[("Dev (384 runs)", "Resumption vs. Restart-Only")],
        delta_vectors[("Dev (384 runs)", "Resumption vs. Unsliced LNS")],
        delta_vectors[("Dev (384 runs)", "Selective Recovery vs. Resumption")],
        delta_vectors[("Unseen (240 runs)", "Resumption vs. Restart-Only")],
        delta_vectors[("Unseen (240 runs)", "Resumption vs. Unsliced LNS")],
        delta_vectors[("Unseen (240 runs)", "Gated Blocker-Closure vs. Resumption")],
    ]
    bp = ax.boxplot(box_data, patch_artist=True, tick_labels=box_labels, showfliers=True)
    colors = [OKABE_BLUE, OKABE_ORANGE, OKABE_PURPLE, OKABE_GREEN, OKABE_ORANGE, OKABE_SKY]
    for patch, c in zip(bp["boxes"], colors):
        patch.set_facecolor(c)
        patch.set_alpha(0.75)
    ax.axhline(0.0, color="#333333", linestyle="--", linewidth=1.0)
    ax.set_ylabel("Paired Delta SoC (Negative = Treatment Better)")
    ax.set_title("Paired Deadline Cost Differences Across Development (N=32) & Unseen (N=60) Studies")
    ax.grid(axis="y", linestyle=":", alpha=0.5)
    fig.tight_layout()
    for ext in ("pdf", "png"):
        fig.savefig(FIG_DIR / f"fig2_paired_delta_soc.{ext}", dpi=300)
    plt.close(fig)

    # Figure 3: Per-Map Mean Delta SoC (Resumption vs Restart-Only & Unsliced)
    df_pm = pd.DataFrame(per_map_rows)
    pm_r = df_pm[df_pm["comparison"] == "Resumption vs. Restart-Only"].set_index("map")
    pm_u = df_pm[df_pm["comparison"] == "Resumption vs. Unsliced LNS"].set_index("map")
    all_maps = list(pm_r.index)
    fig, ax = plt.subplots(figsize=(7.4, 3.8))
    x = np.arange(len(all_maps))
    w = 0.36
    ax.bar(x - w/2, pm_r["mean_delta_soc"], width=w, label="Resume vs. Restart-Only", color=OKABE_BLUE)
    ax.bar(x + w/2, pm_u["mean_delta_soc"], width=w, label="Resume vs. Unsliced LNS", color=OKABE_ORANGE)
    ax.axhline(0.0, color="#333333", linestyle="--", linewidth=1.0)
    ax.set_xticks(x)
    ax.set_xticklabels(all_maps, rotation=18, ha="right")
    ax.set_ylabel("Mean Paired Delta SoC")
    ax.set_title("Per-Map Mean Deadline SoC Difference (Dev & Unseen Maps)")
    ax.legend(frameon=False)
    ax.grid(axis="y", linestyle=":", alpha=0.5)
    fig.tight_layout()
    for ext in ("pdf", "png"):
        fig.savefig(FIG_DIR / f"fig3_per_map_comparison.{ext}", dpi=300)
    plt.close(fig)

    # Figure 4: Unseen-Map Iteration Throughput & Max Overshoot
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(7.8, 3.5))
    pol_order = ["DISCARD_NO_PA", "RESUMPTION_NO_PA", "UNINTERRUPTED_REPAIR", "GATED_BLOCKER_CLOSURE"]
    pol_labels = ["Restart\nOnly", "Prefix\nResume", "Unsliced\nLNS", "Gated\nClosure"]
    iters = [telemetry["unseen"][p]["mean_iterations"] for p in pol_order]
    max_ov = [telemetry["unseen"][p]["max_overshoot_ms"] for p in pol_order]
    bar_colors = [OKABE_GREY, OKABE_BLUE, OKABE_GREEN, OKABE_SKY]
    ax1.bar(pol_labels, iters, color=bar_colors)
    ax1.set_ylabel("Mean LNS Iterations / Run")
    ax1.set_title("Unseen-Map Throughput")
    ax1.grid(axis="y", linestyle=":", alpha=0.5)
    ax2.bar(pol_labels, max_ov, color=bar_colors)
    ax2.set_ylabel("Max Deadline Overshoot (ms)")
    ax2.set_title("Max Cooperative Overshoot")
    ax2.grid(axis="y", linestyle=":", alpha=0.5)
    fig.tight_layout()
    for ext in ("pdf", "png"):
        fig.savefig(FIG_DIR / f"fig4_overshoot_and_throughput.{ext}", dpi=300)
    plt.close(fig)

    print("Analysis & 300-DPI figures complete.")
    print(json.dumps(paired_records, indent=2))


if __name__ == "__main__":
    main()
