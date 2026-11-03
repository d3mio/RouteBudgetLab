#!/usr/bin/env python3
"""
RouteBudget Lab: Phase 5 Estimator Training & Evaluation Script
Component 3: Search-Progress Feature Extraction & Remaining-Work Estimator (O4, FR-04, WP4)

Trains lightweight Ridge regression models and establishes transparent tabular baselines.
Fits preprocessing and models on training partition only; evaluates on validation partition.
Exports model parameters to JSON for C++ inference.
"""

import os
import json
import numpy as np
import pandas as pd
from sklearn.linear_model import Ridge
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import mean_absolute_error, mean_squared_error, r2_score

FEATURE_NAMES = [
    "action_type",
    "neighborhood_size",
    "completed_prefix_ratio",
    "cumulative_expansions",
    "cumulative_active_time_s",
    "repeated_expansions",
    "remaining_agents_count",
    "remaining_agents_lowerbound_cost",
    "current_neighborhood_cost",
    "prefix_cost_ratio",
    "local_congestion_density",
    "requested_slice_duration_s",
    "remaining_global_budget_ratio"
]

TARGET_A = "target_accepted_improvement"
TARGET_B = "target_active_search_time_s"

def evaluate_predictions(y_true, y_pred, name="Model"):
    mae = mean_absolute_error(y_true, y_pred)
    rmse = np.sqrt(mean_squared_error(y_true, y_pred))
    # R2 can be very negative if variance of y_true is near 0
    var = np.var(y_true)
    r2 = r2_score(y_true, y_pred) if var > 1e-12 else 0.0
    return {"MAE": float(mae), "RMSE": float(rmse), "R2": float(r2)}

def main():
    train_path = "results/traces/training_traces.csv"
    val_path = "results/traces/validation_traces.csv"

    if not os.path.exists(train_path) or not os.path.exists(val_path):
        print(f"Error: Trace files missing ({train_path} or {val_path})")
        return

    df_train = pd.read_csv(train_path)
    df_val = pd.read_csv(val_path)

    print("=" * 70)
    print("RouteBudget Lab: Offline Estimator Training & Honest Evaluation (Phase 5)")
    print("=" * 70)
    print(f"Training Samples:   {len(df_train)} (Maps: {df_train['map_name'].unique().tolist()})")
    print(f"Validation Samples: {len(df_val)} (Maps: {df_val['map_name'].unique().tolist()})")

    # Audit partition isolation
    train_maps = set(df_train['map_name'].unique())
    val_maps = set(df_val['map_name'].unique())
    assert len(train_maps.intersection(val_maps)) == 0, "DATA LEAKAGE: Training and Validation maps overlap!"
    print("Partition Isolation Check: PASSED (Zero map overlap between Train and Val)")

    # Data counts & distributions
    print("\n--- Training Set Outcome Distribution ---")
    print(df_train['outcome_type_str'].value_counts())
    print("\n--- Training Set Action Distribution ---")
    print(df_train['action_type_str'].value_counts())

    print("\n--- Validation Set Outcome Distribution ---")
    print(df_val['outcome_type_str'].value_counts())
    print("\n--- Validation Set Action Distribution ---")
    print(df_val['action_type_str'].value_counts())

    # Zero-inflation check on Target A
    train_zero_pct = (df_train[TARGET_A] == 0.0).mean() * 100.0
    val_zero_pct = (df_val[TARGET_A] == 0.0).mean() * 100.0
    print(f"\nZero-Inflation Audit (Target A - Accepted Improvement):")
    print(f"  Training Zero Labels:   {train_zero_pct:.2f}% (Non-zero improvements: {(df_train[TARGET_A] > 0).sum()})")
    print(f"  Validation Zero Labels: {val_zero_pct:.2f}% (Non-zero improvements: {(df_val[TARGET_A] > 0).sum()})")
    print("  NOTE: Near-zero improvement distribution means a trivial zero predictor has deceptively low MAE.")

    X_train = df_train[FEATURE_NAMES].values
    y_train_A = df_train[TARGET_A].values
    y_train_B = df_train[TARGET_B].values

    X_val = df_val[FEATURE_NAMES].values
    y_val_A = df_val[TARGET_A].values
    y_val_B = df_val[TARGET_B].values

    # 1. Establish Transparent Baselines (Constant and Tabular)
    # Baseline 1: Global Training Mean
    mean_A = float(np.mean(y_train_A))
    mean_B = float(np.mean(y_train_B))

    y_pred_base_A = np.full_like(y_val_A, mean_A)
    y_pred_base_B = np.full_like(y_val_B, mean_B)

    # Baseline 2: Tabular by (action_type, slice_duration)
    tabular_lookup = {}
    for (act, slc), group in df_train.groupby(["action_type", "requested_slice_duration_s"]):
        tabular_lookup[(float(act), float(slc))] = {
            "mean_A": float(group[TARGET_A].mean()),
            "mean_B": float(group[TARGET_B].mean())
        }

    def predict_tabular(df):
        pred_A = []
        pred_B = []
        for _, row in df.iterrows():
            key = (float(row["action_type"]), float(row["requested_slice_duration_s"]))
            if key in tabular_lookup:
                pred_A.append(tabular_lookup[key]["mean_A"])
                pred_B.append(tabular_lookup[key]["mean_B"])
            else:
                pred_A.append(mean_A)
                pred_B.append(mean_B)
        return np.array(pred_A), np.array(pred_B)

    y_pred_tab_A, y_pred_tab_B = predict_tabular(df_val)

    # 2. Fit Preprocessing (Scaler) on TRAINING DATA ONLY
    scaler = StandardScaler()
    X_train_scaled = scaler.fit_transform(X_train)
    X_val_scaled = scaler.transform(X_val)

    # 3. Fit Ridge Models on TRAINING DATA ONLY
    model_A = Ridge(alpha=1.0)
    model_A.fit(X_train_scaled, y_train_A)

    model_B = Ridge(alpha=1.0)
    model_B.fit(X_train_scaled, y_train_B)

    # Predictions with physical constraints (clipping)
    raw_pred_val_A = model_A.predict(X_val_scaled)
    # Expected improvement cannot be negative
    y_pred_ridge_A = np.maximum(0.0, raw_pred_val_A)

    raw_pred_val_B = model_B.predict(X_val_scaled)
    # Expected search time must be in [0, requested_slice_duration]
    delta_val = df_val["requested_slice_duration_s"].values
    y_pred_ridge_B = np.clip(raw_pred_val_B, 0.0, delta_val)

    # 4. Comprehensive Evaluation Comparison
    print("\n" + "=" * 70)
    print("MODEL EVALUATION ON HELD-OUT VALIDATION MAPS")
    print("=" * 70)

    print("\n--- Target A: Expected Accepted Improvement (grid units) ---")
    eval_const_A = evaluate_predictions(y_val_A, y_pred_base_A)
    eval_tab_A   = evaluate_predictions(y_val_A, y_pred_tab_A)
    eval_ridge_A = evaluate_predictions(y_val_A, y_pred_ridge_A)
    print(f"Constant Baseline:  MAE = {eval_const_A['MAE']:.5f} | RMSE = {eval_const_A['RMSE']:.5f} | R2 = {eval_const_A['R2']:.5f}")
    print(f"Tabular Baseline:   MAE = {eval_tab_A['MAE']:.5f} | RMSE = {eval_tab_A['RMSE']:.5f} | R2 = {eval_tab_A['R2']:.5f}")
    print(f"Fitted Ridge Model: MAE = {eval_ridge_A['MAE']:.5f} | RMSE = {eval_ridge_A['RMSE']:.5f} | R2 = {eval_ridge_A['R2']:.5f}")

    print("\n--- Target B: Active Search Time min(T_rem, Delta) (seconds) ---")
    eval_const_B = evaluate_predictions(y_val_B, y_pred_base_B)
    eval_tab_B   = evaluate_predictions(y_val_B, y_pred_tab_B)
    eval_ridge_B = evaluate_predictions(y_val_B, y_pred_ridge_B)
    print(f"Constant Baseline:  MAE = {eval_const_B['MAE']:.5f} | RMSE = {eval_const_B['RMSE']:.5f} | R2 = {eval_const_B['R2']:.5f}")
    print(f"Tabular Baseline:   MAE = {eval_tab_B['MAE']:.5f} | RMSE = {eval_tab_B['RMSE']:.5f} | R2 = {eval_tab_B['R2']:.5f}")
    print(f"Fitted Ridge Model: MAE = {eval_ridge_B['MAE']:.5f} | RMSE = {eval_ridge_B['RMSE']:.5f} | R2 = {eval_ridge_B['R2']:.5f}")

    # Subgroup breakdown on Validation
    print("\n--- Breakdown: Fresh vs Resumed Actions on Validation ---")
    for act_str in ["FRESH", "RESUME"]:
        mask = (df_val["action_type_str"] == act_str).values
        n_sub = mask.sum()
        if n_sub == 0: continue
        mae_tab_B = mean_absolute_error(y_val_B[mask], y_pred_tab_B[mask])
        mae_ridge_B = mean_absolute_error(y_val_B[mask], y_pred_ridge_B[mask])
        mae_tab_A = mean_absolute_error(y_val_A[mask], y_pred_tab_A[mask])
        mae_ridge_A = mean_absolute_error(y_val_A[mask], y_pred_ridge_A[mask])
        print(f"  {act_str:6s} (n={n_sub:3d}): Target A [Tab MAE={mae_tab_A:.5f}, Ridge MAE={mae_ridge_A:.5f}] | Target B [Tab MAE={mae_tab_B:.5f}, Ridge MAE={mae_ridge_B:.5f}]")

    print("\n--- Breakdown: Across Slice Durations Delta on Validation ---")
    for slc in sorted(df_val["requested_slice_duration_s"].unique()):
        mask = (df_val["requested_slice_duration_s"] == slc).values
        n_sub = mask.sum()
        mae_tab_B = mean_absolute_error(y_val_B[mask], y_pred_tab_B[mask])
        mae_ridge_B = mean_absolute_error(y_val_B[mask], y_pred_ridge_B[mask])
        print(f"  Delta = {slc*1000:4.1f} ms (n={n_sub:3d}): Target B [Tabular MAE = {mae_tab_B*1000:6.3f} ms, Ridge MAE = {mae_ridge_B*1000:6.3f} ms]")

    # 5. Export Model Parameters for C++ Inference
    # Convert tabular lookup dictionary with string keys for JSON serialization
    serialized_tabular = {
        f"{act}_{slc}": vals for (act, slc), vals in tabular_lookup.items()
    }

    model_export = {
        "metadata": {
            "version": "1.0.0",
            "feature_contract": "Phase 5 RouteBudget Lab",
            "provenance": {
                "training_maps": list(train_maps),
                "validation_maps": list(val_maps),
                "training_samples": len(df_train),
                "validation_samples": len(df_val),
            },
            "metrics_validation": {
                "target_A": {"tabular": eval_tab_A, "ridge": eval_ridge_A},
                "target_B": {"tabular": eval_tab_B, "ridge": eval_ridge_B}
            }
        },
        "feature_names": FEATURE_NAMES,
        "scaler": {
            "means": scaler.mean_.tolist(),
            "scales": scaler.scale_.tolist()
        },
        "ridge_model_A": {
            "description": "Expected Accepted Improvement",
            "intercept": float(model_A.intercept_),
            "coefficients": model_A.coef_.tolist()
        },
        "ridge_model_B": {
            "description": "Expected Active Search Time min(T_rem, Delta)",
            "intercept": float(model_B.intercept_),
            "coefficients": model_B.coef_.tolist()
        },
        "baseline_constant": {
            "mean_A": mean_A,
            "mean_B": mean_B
        },
        "baseline_tabular": serialized_tabular
    }

    out_model_path = "src/engine/estimator_model.json"
    with open(out_model_path, "w") as f:
        json.dump(model_export, f, indent=2)
    print(f"\nModel exported successfully to: {out_model_path}")

    # 6. Generate Parity Verification Test Vectors
    # Pick 2 fresh and 2 resumed feature vectors from validation set
    test_indices = [0, 10, 6, 55]
    verification_cases = []
    for idx in test_indices:
        feat = X_val[idx].tolist()
        # compute Ridge predictions
        feat_scaled = (np.array(feat) - scaler.mean_) / scaler.scale_
        p_A = max(0.0, float(model_A.intercept_ + np.dot(model_A.coef_, feat_scaled)))
        delta_val_i = feat[FEATURE_NAMES.index("requested_slice_duration_s")]
        p_B = min(delta_val_i, max(0.0, float(model_B.intercept_ + np.dot(model_B.coef_, feat_scaled))))
        verification_cases.append({
            "index": idx,
            "features": feat,
            "expected_pred_A": p_A,
            "expected_pred_B": p_B
        })

    with open("src/engine/estimator_verification_cases.json", "w") as f:
        json.dump(verification_cases, f, indent=2)
    print("Verification cases exported to: src/engine/estimator_verification_cases.json")

if __name__ == "__main__":
    main()
