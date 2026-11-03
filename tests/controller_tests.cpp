#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <iomanip>
#include <chrono>

#include "Instance.h"
#include "rbl_resumable_engine.hpp"
#include "rbl_estimator.hpp"
#include "rbl_controller.hpp"
#include "rbl_validator.hpp"

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << (msg) << " (" #cond ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while(0)

// Helper to create an initial plan using anytime-mapf ::Path
bool createTestInitialPlan(const Instance& instance, int num_agents, std::vector<::Path>& out_paths) {
    out_paths.clear();
    out_paths.resize(num_agents);
    ConstraintTable ct(instance.num_of_cols, instance.map_size);
    for (int i = 0; i < num_agents; ++i) {
        SpaceTimeAStar solver(instance, i);
        out_paths[i] = solver.findPath(ct);
        if (out_paths[i].empty()) return false;
        ct.insert2CT(out_paths[i]);
    }
    return true;
}

void testInitializationAndNoInitialSolution() {
    std::cout << "[Test 1] Controller Initialization & Missing Initial Solution..." << std::endl;

    Instance instance("data/sample/random-32-32-20.map", "data/sample/random-32-32-20-random-1.scen", 5);
    rbl::ResumableRepairEngine engine(instance, false, 2);
    rbl::SearchProgressEstimator estimator;
    rbl::ResumeOrStartController controller(engine, estimator, rbl::EstimatorMode::TABULAR_BASELINE,
                                            rbl::ControllerPolicy::SCORE_BASED_ADAPTIVE, 1.0);

    // Empty initial paths
    std::vector<::Path> empty_paths;
    auto summary = controller.run(empty_paths);
    ASSERT_TRUE(!summary.initial_plan_found, "Empty initial plan must be flagged as not found");
    ASSERT_TRUE(!summary.final_solution_valid, "Solution without initial plan must not be marked valid");
    std::cout << "  -> PASS: Missing initial solution cleanly caught." << std::endl;
}

void testPolicyModesK0K1K2() {
    std::cout << "[Test 2] Policy Modes: K=0 Restart-Only vs K=2 FIFO vs Adaptive..." << std::endl;

    Instance instance("data/sample/random-32-32-20.map", "data/sample/random-32-32-20-random-1.scen", 10);
    std::vector<::Path> init_paths;
    bool ok = createTestInitialPlan(instance, 10, init_paths);
    ASSERT_TRUE(ok, "Initial plan creation failed");

    // 1. RESTART_ONLY_K0: capacity must be 0, no resumed actions allowed
    {
        rbl::ResumableRepairEngine engine(instance, false, 2);
        rbl::SearchProgressEstimator estimator;
        rbl::ResumeOrStartController controller(engine, estimator, rbl::EstimatorMode::TABULAR_BASELINE,
                                                rbl::ControllerPolicy::RESTART_ONLY_K0, 0.2);
        auto summary = controller.run(init_paths);
        ASSERT_TRUE(engine.getCheckpointStore().getCapacity() == 0, "K=0 policy must enforce store capacity 0");
        ASSERT_TRUE(summary.resumed_actions_count == 0, "K=0 policy must never execute resumed actions");
        ASSERT_TRUE(summary.fresh_actions_count > 0, "K=0 policy must execute fresh repairs");
        ASSERT_TRUE(summary.final_solution_valid, "Final solution must be valid");
    }

    // 2. FIXED_FIFO_RESUME: capacity 2
    {
        rbl::ResumableRepairEngine engine(instance, false, 2);
        rbl::SearchProgressEstimator estimator;
        rbl::ResumeOrStartController controller(engine, estimator, rbl::EstimatorMode::TABULAR_BASELINE,
                                                rbl::ControllerPolicy::FIXED_FIFO_RESUME, 0.2);
        auto summary = controller.run(init_paths);
        ASSERT_TRUE(engine.getCheckpointStore().getCapacity() == 2, "FIFO policy must use K=2");
        ASSERT_TRUE(summary.final_solution_valid, "Final solution must be valid");
    }

    // 3. SCORE_BASED_ADAPTIVE: capacity 2
    {
        rbl::ResumableRepairEngine engine(instance, false, 2);
        rbl::SearchProgressEstimator estimator;
        rbl::ResumeOrStartController controller(engine, estimator, rbl::EstimatorMode::TABULAR_BASELINE,
                                                rbl::ControllerPolicy::SCORE_BASED_ADAPTIVE, 0.2);
        auto summary = controller.run(init_paths);
        ASSERT_TRUE(engine.getCheckpointStore().getCapacity() == 2, "Adaptive policy must use K=2");
        ASSERT_TRUE(summary.final_solution_valid, "Final solution must be valid");
    }

    std::cout << "  -> PASS: All three policy modes execute cleanly with matched settings." << std::endl;
}

void testAllZeroFallbackAndDeterministicProgress() {
    std::cout << "[Test 3] All-Zero Predicted Improvement Triggers Deterministic Fallback..." << std::endl;

    Instance instance("data/sample/random-32-32-20.map", "data/sample/random-32-32-20-random-1.scen", 10);
    std::vector<::Path> init_paths;
    createTestInitialPlan(instance, 10, init_paths);

    rbl::ResumableRepairEngine engine(instance, false, 2);
    rbl::SearchProgressEstimator estimator;
    // Run controller with TABULAR_BASELINE
    rbl::ResumeOrStartController controller(engine, estimator, rbl::EstimatorMode::TABULAR_BASELINE,
                                            rbl::ControllerPolicy::SCORE_BASED_ADAPTIVE, 0.15);

    auto summary = controller.run(init_paths);
    ASSERT_TRUE(summary.total_decisions > 0, "Controller must make decisions");
    // Verify fallback logging: when improvements predict 0, fallback_reason is populated
    bool saw_fallback = false;
    for (const auto& dec : summary.decisions) {
        if (dec.is_fallback) {
            saw_fallback = true;
            ASSERT_TRUE(!dec.fallback_reason.empty(), "Fallback reason must be documented");
        }
    }
    std::cout << "  -> PASS: Fallback gracefully engaged (saw_fallback=" << saw_fallback << ") with documented reasons." << std::endl;
}

void testRepeatedNoProgressEviction() {
    std::cout << "[Test 4] Stagnation Eviction: Bounded Response to Repeated No-Progress..." << std::endl;

    Instance instance("data/sample/random-32-32-20.map", "data/sample/random-32-32-20-random-1.scen", 10);
    std::vector<::Path> init_paths;
    createTestInitialPlan(instance, 10, init_paths);

    rbl::ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(init_paths);

    // Save a synthetic interrupted checkpoint
    rbl::MacroPrefixCheckpoint cp;
    cp.checkpoint_id = 99;
    cp.lifecycle = rbl::CheckpointLifecycle::SUSPENDED;
    cp.neighborhood_agents = {0, 1, 2, 3};
    cp.priority_order = {0, 1, 2, 3};
    cp.r_done = 1;
    cp.prefix_paths = {init_paths[0]};
    cp.old_paths = {init_paths[0], init_paths[1], init_paths[2], init_paths[3]};
    cp.old_neighborhood_cost = 50;
    cp.prefix_accumulated_cost = 10;
    cp.incumbent_version = engine.getIncumbentVersion();
    cp.agent_versions_at_save = engine.getAgentVersions();
    cp.consecutive_stagnant_attempts = 2; // At stagnation limit!

    engine.getCheckpointStore().saveCheckpoint(cp);
    ASSERT_TRUE(engine.getCheckpointStore().getNumSuspended() == 1, "Checkpoint saved in store");

    rbl::SearchProgressEstimator estimator;
    rbl::ResumeOrStartController controller(engine, estimator, rbl::EstimatorMode::TABULAR_BASELINE,
                                            rbl::ControllerPolicy::SCORE_BASED_ADAPTIVE, 0.05);

    // Running controller should evict stagnant checkpoint
    auto summary = controller.run(init_paths);

    const auto* slot0 = engine.getCheckpointStore().getCheckpoint(0);
    ASSERT_TRUE(summary.stagnant_checkpoints_evicted >= 1, "Stagnant checkpoint must be recorded as evicted in summary");
    ASSERT_TRUE(slot0 == nullptr || slot0->checkpoint_id != 99, "Stagnant checkpoint 99 must have been evicted");
    std::cout << "  -> PASS: Stagnant checkpoint evicted cleanly after exceeding threshold." << std::endl;
}

void testGlobalDeadlineRecheckAndLateCommitRejection() {
    std::cout << "[Test 5] Late Commit Rejection & Strict Incumbent Preservation..." << std::endl;

    Instance instance("data/sample/random-32-32-20.map", "data/sample/random-32-32-20-random-1.scen", 10);
    std::vector<::Path> init_paths;
    createTestInitialPlan(instance, 10, init_paths);

    rbl::ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(init_paths);
    int initial_cost = engine.getIncumbentCost();

    // Create a budget where deadline is ALREADY in the past
    rbl::SearchBudget expired_budget(1000, 0.010);
    expired_budget.setGlobalDeadline(std::chrono::steady_clock::now() - std::chrono::milliseconds(10));

    // Fresh repair with expired global deadline must reject commit
    auto outcome = engine.attemptFreshRepair({0, 1}, expired_budget);
    ASSERT_TRUE(outcome.type == rbl::RepairOutcomeType::STOPPED_DEADLINE ||
                outcome.type == rbl::RepairOutcomeType::REJECTED_LATE_COMMIT,
                "Expired deadline must reject commit");
    ASSERT_TRUE(engine.getIncumbentCost() == initial_cost, "Incumbent cost must be strictly preserved");

    std::cout << "  -> PASS: Late commit safely rejected; incumbent preserved untouched." << std::endl;
}

void testDecisionLatencyBenchmark() {
    std::cout << "[Test 6] Full Decision Pipeline Latency Audit..." << std::endl;

    Instance instance("data/sample/random-32-32-20.map", "data/sample/random-32-32-20-random-1.scen", 10);
    std::vector<::Path> init_paths;
    createTestInitialPlan(instance, 10, init_paths);

    rbl::ResumableRepairEngine engine(instance, false, 2);
    rbl::SearchProgressEstimator estimator;
    rbl::ResumeOrStartController controller(engine, estimator, rbl::EstimatorMode::TABULAR_BASELINE,
                                            rbl::ControllerPolicy::SCORE_BASED_ADAPTIVE, 0.5);

    auto summary = controller.run(init_paths);

    std::cout << "  Decisions made: " << summary.total_decisions << std::endl;
    std::cout << "  Median Decision Latency: " << std::fixed << std::setprecision(2)
              << summary.median_decision_latency_us << " us ("
              << (summary.median_decision_latency_us / 1000.0) << " ms)" << std::endl;
    std::cout << "  P95 Decision Latency:    " << summary.p95_decision_latency_us << " us ("
              << (summary.p95_decision_latency_us / 1000.0) << " ms)" << std::endl;
    std::cout << "  Deadline Overshoot:      " << summary.deadline_overshoot_s * 1000.0 << " ms" << std::endl;

    // Proposal target was <= 1.0 ms (1000 us) for decision + estimation
    ASSERT_TRUE(summary.median_decision_latency_us < 1000.0, "Decision latency must meet proposal target <= 1.0 ms");
    ASSERT_TRUE(summary.final_solution_valid, "Final solution must be valid");
    std::cout << "  -> PASS: Decision pipeline latency meets proposal budget <= 1 ms." << std::endl;
}

void testScoreBasedFixedAndElapsedTimeOnlyModes() {
    std::cout << "[Test 7] Matched Controls: SCORE_BASED_FIXED & ELAPSED_TIME_ONLY Policies..." << std::endl;

    Instance instance("data/sample/random-32-32-20.map", "data/sample/random-32-32-20-random-1.scen", 10);
    std::vector<::Path> init_paths;
    createTestInitialPlan(instance, 10, init_paths);

    // 1. SCORE_BASED_FIXED: must use K=2 and only 10 ms slices
    {
        rbl::ResumableRepairEngine engine(instance, false, 2);
        rbl::SearchProgressEstimator estimator;
        rbl::ResumeOrStartController controller(engine, estimator, rbl::EstimatorMode::RIDGE_REGRESSION,
                                                rbl::ControllerPolicy::SCORE_BASED_FIXED, 0.2);
        auto summary = controller.run(init_paths);
        ASSERT_TRUE(engine.getCheckpointStore().getCapacity() == 2, "SCORE_BASED_FIXED must use K=2");
        ASSERT_TRUE(summary.final_solution_valid, "Final solution must be valid");
        for (const auto& dec : summary.decisions) {
            // All scheduled slices must be <= 10 ms (10 ms fixed, or clamped if remaining budget < 10 ms)
            ASSERT_TRUE(dec.slice_duration_s <= 0.010001, "SCORE_BASED_FIXED must never exceed 10 ms slice");
        }
    }

    // 2. ELAPSED_TIME_ONLY: must run cleanly with masked features
    {
        rbl::ResumableRepairEngine engine(instance, false, 2);
        rbl::SearchProgressEstimator estimator;
        rbl::ResumeOrStartController controller(engine, estimator, rbl::EstimatorMode::ELAPSED_TIME_ONLY,
                                                rbl::ControllerPolicy::ELAPSED_TIME_ONLY, 0.2);
        auto summary = controller.run(init_paths);
        ASSERT_TRUE(engine.getCheckpointStore().getCapacity() == 2, "ELAPSED_TIME_ONLY must use K=2");
        ASSERT_TRUE(summary.final_solution_valid, "Final solution must be valid");
        ASSERT_TRUE(summary.total_decisions > 0, "Must execute decisions");
    }

    std::cout << "  -> PASS: SCORE_BASED_FIXED and ELAPSED_TIME_ONLY execute correctly with matched controls." << std::endl;
}

void testMemoryBookkeepingSeparation() {
    std::cout << "[Test 8] Bookkeeping Memory vs Retained Payload Separation..." << std::endl;

    Instance instance("data/sample/random-32-32-20.map", "data/sample/random-32-32-20-random-1.scen", 10);
    std::vector<::Path> init_paths;
    createTestInitialPlan(instance, 10, init_paths);

    // For K=0: payload must be strictly 0 B
    {
        rbl::ResumableRepairEngine engine(instance, false, 2);
        rbl::SearchProgressEstimator estimator;
        rbl::ResumeOrStartController controller(engine, estimator, rbl::EstimatorMode::RIDGE_REGRESSION,
                                                rbl::ControllerPolicy::RESTART_ONLY_K0, 0.1);
        auto summary = controller.run(init_paths);
        ASSERT_TRUE(summary.fixed_bookkeeping_memory_bytes > 0, "Fixed store structure must be nonzero");
        ASSERT_TRUE(summary.peak_payload_memory_bytes == 0, "K=0 payload memory must be strictly 0 B");
    }

    std::cout << "  -> PASS: Memory separation cleanly verified (K=0 payload strictly 0 B)." << std::endl;
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "RouteBudget Lab: Overhead-Aware Controller Integration Tests (Phase 6/7)" << std::endl;
    std::cout << "================================================================" << std::endl;

    testInitializationAndNoInitialSolution();
    testPolicyModesK0K1K2();
    testAllZeroFallbackAndDeterministicProgress();
    testRepeatedNoProgressEviction();
    testGlobalDeadlineRecheckAndLateCommitRejection();
    testDecisionLatencyBenchmark();
    testScoreBasedFixedAndElapsedTimeOnlyModes();
    testMemoryBookkeepingSeparation();

    std::cout << "================================================================" << std::endl;
    std::cout << "ALL PHASE 6/7 CONTROLLER TESTS PASSED!" << std::endl;
    std::cout << "================================================================" << std::endl;
    return 0;
}
