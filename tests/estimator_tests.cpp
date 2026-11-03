#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <iomanip>

#include "rbl_feature_extractor.hpp"
#include "rbl_estimator.hpp"
#include "Instance.h"

// Simple assertion macro with detailed diagnostic output
#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << (msg) << " (" #cond ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while(0)

#define ASSERT_NEAR(val, expected, tol, msg) \
    do { \
        if (std::abs((val) - (expected)) > (tol)) { \
            std::cerr << "FAIL: " << (msg) << " Expected: " << (expected) \
                      << ", Actual: " << (val) << ", Diff: " << std::abs((val) - (expected)) \
                      << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while(0)

void testFeatureContract() {
    std::cout << "[Test 1] Search Progress Feature Contract..." << std::endl;

    rbl::SearchProgressFeatures f;
    ASSERT_TRUE(f.toVector().size() == 13, "Feature vector must contain exactly 13 features");
    ASSERT_TRUE(rbl::SearchProgressFeatures::featureNames().size() == 13, "Feature names count must be 13");

    // Verify open-list feature is omitted
    for (const auto& name : rbl::SearchProgressFeatures::featureNames()) {
        ASSERT_TRUE(name.find("open_list") == std::string::npos, "Live open-list size must NOT be in feature contract");
    }

    std::cout << "  -> PASS: 13 features verified, live open-list omitted as required." << std::endl;
}

void testPythonCppParity() {
    std::cout << "[Test 2] Python vs C++ Prediction Parity on Verification Cases..." << std::endl;

    rbl::SearchProgressEstimator estimator;

    // Fixed verification cases exported by Python training script
    // Case 0: Fresh, k=8, delta=0.010
    std::vector<double> feat0 = {0.0, 8.0, 0.0, 0.0, 0.0, 0.0, 8.0, 263.0, 271.0, 0.0, 0.003123, 0.01, 1.0};
    double py_A_0 = 0.7774911098113515;
    double py_B_0 = 0.0005328976066357116;

    auto res0 = estimator.predict(feat0, 0.01, rbl::EstimatorMode::RIDGE_REGRESSION);
    ASSERT_NEAR(res0.expected_accepted_improvement, py_A_0, 1e-5, "Case 0 Target A Parity");
    ASSERT_NEAR(res0.expected_active_search_time_s, py_B_0, 1e-6, "Case 0 Target B Parity");

    // Case 1: Fresh, k=4, delta=0.020 (clipped at 0.0 for Target A)
    std::vector<double> feat1 = {0.0, 4.0, 0.0, 0.0, 0.0, 0.0, 4.0, 160.0, 162.0, 0.0, 0.006494, 0.02, 0.999745};
    double py_A_1 = 0.0;
    double py_B_1 = 0.00014047654802609553;

    auto res1 = estimator.predict(feat1, 0.02, rbl::EstimatorMode::RIDGE_REGRESSION);
    ASSERT_NEAR(res1.expected_accepted_improvement, py_A_1, 1e-5, "Case 1 Target A Parity");
    ASSERT_NEAR(res1.expected_active_search_time_s, py_B_1, 1e-6, "Case 1 Target B Parity");

    // Case 2: Resumed, k=8, r_done/k=0.5, delta=0.002
    std::vector<double> feat2 = {1.0, 8.0, 0.5, 201.0, 0.000131, 28.0, 4.0, 171.0, 341.0, 0.492669, 0.004472, 0.002, 0.999872};
    double py_A_2 = 0.0;
    double py_B_2 = 0.001873311222351081;

    auto res2 = estimator.predict(feat2, 0.002, rbl::EstimatorMode::RIDGE_REGRESSION);
    ASSERT_NEAR(res2.expected_accepted_improvement, py_A_2, 1e-5, "Case 2 Target A Parity");
    ASSERT_NEAR(res2.expected_active_search_time_s, py_B_2, 1e-6, "Case 2 Target B Parity");

    // Case 3: Resumed, k=8, r_done/k=0.625, delta=0.005
    std::vector<double> feat3 = {1.0, 8.0, 0.625, 201.0, 9.5e-05, 23.0, 3.0, 171.0, 344.0, 0.502907, 0.005958, 0.005, 0.999856};
    double py_A_3 = 1.0198141106825331;
    double py_B_3 = 0.0008122235593553714;

    auto res3 = estimator.predict(feat3, 0.005, rbl::EstimatorMode::RIDGE_REGRESSION);
    ASSERT_NEAR(res3.expected_accepted_improvement, py_A_3, 1e-5, "Case 3 Target A Parity");
    ASSERT_NEAR(res3.expected_active_search_time_s, py_B_3, 1e-6, "Case 3 Target B Parity");

    std::cout << "  -> PASS: All 4 verification cases match Python within numerical precision (< 1e-5)." << std::endl;
}

void testPhysicalBoundsAndFallbacks() {
    std::cout << "[Test 3] Physical Bounds and Robust Fallbacks..." << std::endl;

    rbl::SearchProgressEstimator estimator;

    // Test non-finite input (NaN)
    std::vector<double> nan_feat(13, 0.0);
    nan_feat[3] = std::numeric_limits<double>::quiet_NaN();

    auto res_nan = estimator.predict(nan_feat, 0.010, rbl::EstimatorMode::RIDGE_REGRESSION);
    ASSERT_TRUE(res_nan.used_fallback, "NaN feature must trigger safe fallback");
    ASSERT_TRUE(res_nan.expected_accepted_improvement >= 0.0, "Fallback improvement must be non-negative");
    ASSERT_TRUE(res_nan.expected_active_search_time_s <= 0.010, "Fallback search time must be <= Delta");

    // Test non-finite input (Inf)
    std::vector<double> inf_feat(13, 0.0);
    inf_feat[7] = std::numeric_limits<double>::infinity();

    auto res_inf = estimator.predict(inf_feat, 0.010, rbl::EstimatorMode::RIDGE_REGRESSION);
    ASSERT_TRUE(res_inf.used_fallback, "Inf feature must trigger safe fallback");

    // Test vector size mismatch
    std::vector<double> short_feat(5, 1.0);
    auto res_short = estimator.predict(short_feat, 0.010, rbl::EstimatorMode::RIDGE_REGRESSION);
    ASSERT_TRUE(res_short.used_fallback, "Feature vector size mismatch must trigger fallback");

    // Test Tabular mode and unknown slice fallback
    std::vector<double> valid_feat(13, 0.0);
    valid_feat[0] = 0.0; // fresh
    auto res_tab_known = estimator.predict(valid_feat, 0.010, rbl::EstimatorMode::TABULAR_BASELINE);
    ASSERT_TRUE(!res_tab_known.used_fallback, "Known slice duration in tabular mode should not fallback");
    ASSERT_NEAR(res_tab_known.expected_accepted_improvement, 0.8031496, 1e-4, "Tabular mean_A lookup");

    auto res_tab_unknown = estimator.predict(valid_feat, 0.033, rbl::EstimatorMode::TABULAR_BASELINE);
    ASSERT_TRUE(res_tab_unknown.used_fallback, "Unknown slice duration in tabular mode must trigger safe fallback");

    std::cout << "  -> PASS: All non-finite inputs, size mismatches, and unknown tabular keys handled safely." << std::endl;
}

void testInferenceLatency() {
    std::cout << "[Test 4] Inference Latency Benchmark (100,000 evaluations)..." << std::endl;

    rbl::SearchProgressEstimator estimator;
    double avg_us = estimator.benchmarkLatency(100000);

    std::cout << "  Average Inference Latency: " << std::fixed << std::setprecision(4)
              << avg_us << " microseconds per prediction ("
              << (avg_us / 1000.0) << " ms)" << std::endl;

    // Verify it is well under the 0.1 ms (100 us) target
    ASSERT_TRUE(avg_us < 100.0, "Inference latency must be well under 100 us (0.1 ms)");
    std::cout << "  -> PASS: Latency is " << avg_us << " us, meeting sub-millisecond real-time requirements." << std::endl;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "RouteBudget Lab: Estimator & Feature Extractor Unit Tests (Phase 5)" << std::endl;
    std::cout << "================================================================" << std::endl;

    testFeatureContract();
    testPythonCppParity();
    testPhysicalBoundsAndFallbacks();
    testInferenceLatency();

    std::cout << "================================================================" << std::endl;
    std::cout << "ALL PHASE 5 ESTIMATOR TESTS PASSED!" << std::endl;
    std::cout << "================================================================" << std::endl;
    return 0;
}
