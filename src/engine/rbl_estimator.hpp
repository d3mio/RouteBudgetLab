#pragma once

#include <vector>
#include <string>
#include <array>
#include <unordered_map>
#include <map>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <iomanip>

#include "rbl_feature_extractor.hpp"

namespace rbl {

enum class EstimatorMode {
    RIDGE_REGRESSION,
    TABULAR_BASELINE,
    CONSTANT_BASELINE,
    ELAPSED_TIME_ONLY
};

inline const char* toString(EstimatorMode mode) {
    switch (mode) {
        case EstimatorMode::RIDGE_REGRESSION: return "RIDGE_REGRESSION";
        case EstimatorMode::TABULAR_BASELINE:  return "TABULAR_BASELINE";
        case EstimatorMode::CONSTANT_BASELINE: return "CONSTANT_BASELINE";
        case EstimatorMode::ELAPSED_TIME_ONLY: return "ELAPSED_TIME_ONLY";
        default: return "UNKNOWN";
    }
}

struct EstimationResult {
    double expected_accepted_improvement = 0.0; // Target A: Expected cost reduction (>= 0.0)
    double expected_active_search_time_s = 0.0; // Target B: min(T_rem, Delta) in [0, Delta]
    bool used_fallback = false;
    EstimatorMode mode_used = EstimatorMode::RIDGE_REGRESSION;
};

// Model Parameters and Provenance Metadata
struct EstimatorModelData {
    std::string version = "1.0.0";
    std::string feature_contract = "Phase 5 RouteBudget Lab";
    std::vector<std::string> training_maps = {
        "random-32-32-20.map",
        "maze-32-32-2.map",
        "empty-32-32.map",
        "warehouse-10-20-10-2-1.map"
    };
    std::vector<std::string> validation_maps = {
        "warehouse-10-20-10-2-2.map",
        "random-64-64-10.map"
    };
    size_t training_samples = 1280;
    size_t validation_samples = 640;

    // Feature scaler parameters (StandardScaler fit on training set)
    std::array<double, SearchProgressFeatures::NUM_FEATURES> scaler_means = {
        0.09453125,
        4.9796875,
        0.0537109375,
        32.76171875,
        1.316953125e-05,
        12.78671875,
        4.6109375,
        159.215625,
        201.26796875,
        0.05174478515625001,
        0.012603971875,
        0.015996875,
        0.99980731328125
    };

    std::array<double, SearchProgressFeatures::NUM_FEATURES> scaler_scales = {
        0.29256639036881443,
        2.5408339974000174,
        0.1820051575653039,
        171.0635808429015,
        6.306341185985162e-05,
        120.89919863199422,
        2.533871616932032,
        151.23163754108919,
        168.15965044117755,
        0.17642362296520905,
        0.01145930801824532,
        0.016665833019515557,
        0.00014233058288227608
    };

    // Target A (Accepted Improvement) Ridge Model
    double ridge_A_intercept = 0.3828125000001055;
    std::array<double, SearchProgressFeatures::NUM_FEATURES> ridge_A_coefs = {
        0.14065177696364026,
        0.188976535266014,
        0.3304537413413374,
        1.5642540295858152,
        -1.6900437861323154,
        -0.060672566303537276,
        -0.11225811612188491,
        -0.1549400547400831,
        0.7252775737948643,
        -0.3935959703722969,
        0.3807191531778678,
        -0.08561825999665927,
        0.278993506317197
    };

    // Target B (Active Search Time min(T_rem, Delta)) Ridge Model
    double ridge_B_intercept = 0.00030500468749999985;
    std::array<double, SearchProgressFeatures::NUM_FEATURES> ridge_B_coefs = {
        -0.00020863538509212001,
        -2.4866871211786167e-05,
        0.00022641612439506509,
        -0.0014974786492066205,
        0.0020511837042833884,
        -8.899869164904831e-05,
        1.8133915208733682e-05,
        0.00016403563851192558,
        0.0003107809951343135,
        -0.000412030892982951,
        7.878643356069647e-05,
        -0.00018930862560929528,
        -3.5646401120860436e-07
    };

    // Baseline Constant (Global Training Means)
    double baseline_constant_mean_A = 0.3828125;
    double baseline_constant_mean_B = 0.0003050046875;

    // Baseline Tabular ((action_type, slice_duration_ms) -> (mean_A, mean_B))
    // Key: 1000 * action_type + round(slice_s * 1000)
    // Fresh (action 0): 2, 5, 10, 20, 50
    // Resumed (action 1): 1002, 1005, 1010, 1020, 1050
    std::map<int, std::pair<double, double>> tabular_table = {
        {2,    {0.1434426, 0.00082086}},
        {5,    {0.3110236, 0.00037696}},
        {10,   {0.8031496, 4.9488e-05}},
        {20,   {0.1943128, 3.5308e-06}},
        {50,   {0.1734694, 3.1531e-06}},
        {1002, {0.0,       0.00083333}},
        {1005, {1.3846154, 0.00038727}},
        {1010, {2.4736842, 0.00053468}},
        {1020, {0.4444444, 0.00148852}},
        {1050, {0.08,      6.4000e-07}}
    };
};

class SearchProgressEstimator {
private:
    EstimatorModelData model_data_;
    bool is_valid_ = true;

public:
    SearchProgressEstimator() = default;

    const EstimatorModelData& getModelData() const { return model_data_; }
    bool isValid() const { return is_valid_; }

    // Fast inference method for C++ planner
    EstimationResult predict(
        const SearchProgressFeatures& features,
        EstimatorMode mode = EstimatorMode::RIDGE_REGRESSION) const
    {
        return predict(features.toVector(), features.requested_slice_duration_s, mode);
    }

    // Direct vector interface
    EstimationResult predict(
        const std::vector<double>& feat_vec,
        double requested_slice_s,
        EstimatorMode mode = EstimatorMode::RIDGE_REGRESSION) const
    {
        EstimationResult res;
        res.mode_used = mode;

        // 1. Sanity check input features: count & finiteness
        if (feat_vec.size() != SearchProgressFeatures::NUM_FEATURES) {
            res.used_fallback = true;
            res.expected_accepted_improvement = model_data_.baseline_constant_mean_A;
            res.expected_active_search_time_s = std::min(requested_slice_s, model_data_.baseline_constant_mean_B);
            return res;
        }

        for (double val : feat_vec) {
            if (!std::isfinite(val)) {
                res.used_fallback = true;
                res.expected_accepted_improvement = model_data_.baseline_constant_mean_A;
                res.expected_active_search_time_s = std::min(requested_slice_s, model_data_.baseline_constant_mean_B);
                return res;
            }
        }

        // 2. Constant Baseline evaluation
        if (mode == EstimatorMode::CONSTANT_BASELINE) {
            res.expected_accepted_improvement = model_data_.baseline_constant_mean_A;
            res.expected_active_search_time_s = std::min(requested_slice_s, model_data_.baseline_constant_mean_B);
            return res;
        }

        // 3. Tabular Baseline evaluation
        if (mode == EstimatorMode::TABULAR_BASELINE) {
            int action_flag = (feat_vec[0] > 0.5) ? 1 : 0;
            int slice_ms = (int)std::round(requested_slice_s * 1000.0);
            int key = action_flag * 1000 + slice_ms;

            auto it = model_data_.tabular_table.find(key);
            if (it != model_data_.tabular_table.end()) {
                res.expected_accepted_improvement = it->second.first;
                res.expected_active_search_time_s = std::min(requested_slice_s, it->second.second);
            } else {
                // If unknown slice size, fallback to nearest or constant
                res.used_fallback = true;
                res.expected_accepted_improvement = model_data_.baseline_constant_mean_A;
                res.expected_active_search_time_s = std::min(requested_slice_s, model_data_.baseline_constant_mean_B);
            }
            return res;
        }

        // 4. Elapsed-Time-Only Ablation (E5)
        // Masks internal search-progress features (indices 2 through 10) with training scaler means,
        // relying purely on action_type (0), neighborhood_size (1), requested_slice_s (11), and budget_remaining_ratio (12).
        if (mode == EstimatorMode::ELAPSED_TIME_ONLY) {
            double dot_A = 0.0;
            double dot_B = 0.0;
            for (size_t i = 0; i < SearchProgressFeatures::NUM_FEATURES; ++i) {
                double val = (i >= 2 && i <= 10) ? model_data_.scaler_means[i] : feat_vec[i];
                double scale = (model_data_.scaler_scales[i] > 1e-12) ? model_data_.scaler_scales[i] : 1.0;
                double norm_x = (val - model_data_.scaler_means[i]) / scale;
                dot_A += model_data_.ridge_A_coefs[i] * norm_x;
                dot_B += model_data_.ridge_B_coefs[i] * norm_x;
            }
            double raw_A = model_data_.ridge_A_intercept + dot_A;
            double raw_B = model_data_.ridge_B_intercept + dot_B;
            res.expected_accepted_improvement = std::max(0.0, raw_A);
            res.expected_active_search_time_s = std::max(0.0, std::min(requested_slice_s, raw_B));
            res.used_fallback = false;
            return res;
        }

        // 5. Full Progress-Feature Ridge Regression Model
        // Normalize features: (x - mean) / scale
        double dot_A = 0.0;
        double dot_B = 0.0;
        for (size_t i = 0; i < SearchProgressFeatures::NUM_FEATURES; ++i) {
            double scale = (model_data_.scaler_scales[i] > 1e-12) ? model_data_.scaler_scales[i] : 1.0;
            double norm_x = (feat_vec[i] - model_data_.scaler_means[i]) / scale;
            dot_A += model_data_.ridge_A_coefs[i] * norm_x;
            dot_B += model_data_.ridge_B_coefs[i] * norm_x;
        }

        double raw_A = model_data_.ridge_A_intercept + dot_A;
        double raw_B = model_data_.ridge_B_intercept + dot_B;

        // Physical clamping
        res.expected_accepted_improvement = std::max(0.0, raw_A);
        res.expected_active_search_time_s = std::max(0.0, std::min(requested_slice_s, raw_B));
        res.used_fallback = false;

        return res;
    }

    // Benchmark inference latency in Release build (microseconds per evaluation)
    double benchmarkLatency(size_t iterations = 100000) const {
        SearchProgressFeatures dummy;
        dummy.action_type = 0.0;
        dummy.neighborhood_size = 4.0;
        dummy.completed_prefix_ratio = 0.0;
        dummy.cumulative_expansions = 150.0;
        dummy.cumulative_active_time_s = 0.001;
        dummy.repeated_expansions = 10.0;
        dummy.remaining_agents_count = 4.0;
        dummy.remaining_agents_lowerbound_cost = 80.0;
        dummy.current_neighborhood_cost = 95.0;
        dummy.prefix_cost_ratio = 0.0;
        dummy.local_congestion_density = 0.015;
        dummy.requested_slice_duration_s = 0.010;
        dummy.remaining_global_budget_ratio = 0.85;

        auto start = std::chrono::high_resolution_clock::now();
        volatile double sum_A = 0.0;
        volatile double sum_B = 0.0;

        for (size_t i = 0; i < iterations; ++i) {
            auto res = predict(dummy, EstimatorMode::RIDGE_REGRESSION);
            sum_A += res.expected_accepted_improvement;
            sum_B += res.expected_active_search_time_s;
        }

        auto end = std::chrono::high_resolution_clock::now();
        double elapsed_us = std::chrono::duration<double, std::micro>(end - start).count();
        return elapsed_us / (double)iterations;
    }
};

} // namespace rbl
