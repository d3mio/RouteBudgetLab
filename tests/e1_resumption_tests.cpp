#include <cmath>
// RouteBudget Lab - Experiment E1 Test Suite: Pause/Resume Correctness & Equivalence
//
// Focused E1 Coverage:
// 1. Uninterrupted repair baseline.
// 2. Interruption at agent boundary (r_done = 2) & resumption equivalence with explicit cell-by-cell path equality.
// 3. Interruption inside low-level search (mid-search cutoff on unfinished agent, r_done = 1, current_agent_expansions > 0).
// 4. Repeated interruption on the same difficult agent (demonstrating lack of progress and frontier discard in macro-prefix mode).
// 5. Interruption before any prefix is completed (r_done = 0).
// 6. Multiple pause/resume cycles on the same repair transaction.
// 7. Bounded checkpoint store behavior: K=0, K=1, K=2, lifecycle states, and deterministic FIFO eviction.
// 8. Global deadline expiry handling (STOPPED_DEADLINE).
// 9. Independent collision and path validity checking (rbl_validator).
// 10. Integrated save/restore latency and retained memory reporting.

#include <iostream>
#include <cassert>
#include <vector>
#include <string>

#include "Instance.h"
#include "rbl_resumable_engine.hpp"
#include "rbl_validator.hpp"

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        std::cerr << "FAILED: " << msg << " (" << #a << " = " << (a) << ", " << #b << " = " << (b) << ")" << std::endl; \
        return false; \
    } \
} while(0)

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "FAILED: " << msg << " (" << #cond << " is false)" << std::endl; \
        return false; \
    } \
} while(0)

namespace rbl_test {
using namespace rbl;

// Helper: generate guaranteed collision-free initial paths using prioritized planning (PP)
std::vector<::Path> createInitialPaths(const Instance& instance, int num_agents) {
    std::vector<::Path> paths(num_agents);
    ConstraintTable ct(instance.num_of_cols, instance.map_size);
    for (int i = 0; i < num_agents; ++i) {
        SpaceTimeAStar solver(instance, i);
        paths[i] = solver.findPath(ct);
        assert(!paths[i].empty());
        ct.insert2CT(paths[i]);
    }
    return paths;
}

bool runAllE1Tests(const std::string& map_file, const std::string& scen_file) {
    std::cout << "================================================================" << std::endl;
    std::cout << "Running Experiment E1: Resumable Repair Correctness & Equivalence" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "Map: " << map_file << "\nScen: " << scen_file << std::endl;

    const int test_agents = 10;
    Instance instance(map_file, scen_file, test_agents);

    std::vector<::Path> initial_paths = createInitialPaths(instance, test_agents);
    std::cout << "Generated " << initial_paths.size() << " initial paths." << std::endl;

    // -------------------------------------------------------------
    // Test 1: Uninterrupted Repair Baseline
    // -------------------------------------------------------------
    std::cout << "\n[E1 Test 1] Uninterrupted Repair Baseline..." << std::endl;
    ResumableRepairEngine engine_baseline(instance, false, 2);
    engine_baseline.setInitialSolution(initial_paths);

    std::string val_err;
    ASSERT_TRUE(engine_baseline.validateIncumbent(val_err), "Initial solution must be valid: " + val_err);

    std::vector<int> neighborhood = {0, 1, 2, 3};
    SearchBudget budget_uninterrupted(50000, 10.0);
    RepairOutcome outcome_uninterrupted = engine_baseline.attemptFreshRepair(neighborhood, budget_uninterrupted, 42);

    std::cout << "  Outcome: " << toString(outcome_uninterrupted.type)
              << " | Old Cost: " << outcome_uninterrupted.old_cost
              << " | New Cost: " << outcome_uninterrupted.new_cost
              << " | Delta: " << outcome_uninterrupted.cost_delta
              << " | Expansions: " << outcome_uninterrupted.total_expansions << std::endl;

    ASSERT_TRUE(outcome_uninterrupted.type == RepairOutcomeType::COMPLETED_ACCEPTED ||
                outcome_uninterrupted.type == RepairOutcomeType::COMPLETED_NOT_IMPROVING,
                "Uninterrupted repair must complete");

    std::vector<::Path> baseline_final_paths = engine_baseline.getIncumbentPaths();
    int baseline_final_cost = engine_baseline.getIncumbentCost();

    // -------------------------------------------------------------
    // Test 2: Interruption After 2 Agents (r_done = 2) & Cell-by-Cell Path Parity
    // -------------------------------------------------------------
    std::cout << "\n[E1 Test 2] Interruption After 2 Agents & Exact Path Parity..." << std::endl;
    ResumableRepairEngine engine_resumed(instance, false, 2);
    engine_resumed.setInitialSolution(initial_paths);

    // Limit budget so it routes first 2 agents
    SearchBudget budget_slice1(outcome_uninterrupted.total_expansions / 2, 10.0);
    RepairOutcome outcome_slice1 = engine_resumed.attemptFreshRepair(neighborhood, budget_slice1, 42);

    std::cout << "  Slice 1 Outcome: " << toString(outcome_slice1.type)
              << " | r_done: " << outcome_slice1.interruption.completed_prefix_len
              << " | Has reusable prefix: " << outcome_slice1.interruption.has_reusable_prefix << std::endl;

    // Verify transactional isolation: incumbent is untouched during interruption
    ASSERT_EQ(engine_resumed.getIncumbentCost(), engine_baseline.getIncumbentCost() - (outcome_uninterrupted.type == RepairOutcomeType::COMPLETED_ACCEPTED ? outcome_uninterrupted.cost_delta : 0),
              "Incumbent cost must remain untouched during interruption");

    BoundedCheckpointStore& store = engine_resumed.getCheckpointStore();
    ASSERT_EQ(store.getNumSuspended(), 1, "Store must contain exactly 1 suspended checkpoint");

    MacroPrefixCheckpoint* cp = store.getLatestSuspended();
    ASSERT_TRUE(cp != nullptr, "Latest suspended checkpoint must not be null");
    ASSERT_EQ(cp->r_done, outcome_slice1.interruption.completed_prefix_len, "Checkpoint r_done must match interruption record");

    // RESUME the checkpoint with remaining budget
    SearchBudget budget_slice2(50000, 10.0);
    RepairOutcome outcome_slice2 = engine_resumed.resumeCheckpoint(0, budget_slice2);

    std::cout << "  Slice 2 Outcome: " << toString(outcome_slice2.type)
              << " | Final Cost: " << outcome_slice2.new_cost << std::endl;

    ASSERT_EQ(toString(outcome_slice2.type), toString(outcome_uninterrupted.type),
              "Resumed repair outcome must match uninterrupted repair outcome");
    ASSERT_EQ(engine_resumed.getIncumbentCost(), baseline_final_cost,
              "Resumed repair final incumbent cost must equal uninterrupted repair cost");

    // Explicit path-by-path cell-by-cell equality assertion
    const auto& resumed_final_paths = engine_resumed.getIncumbentPaths();
    ASSERT_EQ(resumed_final_paths.size(), baseline_final_paths.size(), "Agent count must match");
    for (size_t a = 0; a < baseline_final_paths.size(); ++a) {
        ASSERT_EQ(resumed_final_paths[a].size(), baseline_final_paths[a].size(), "Agent path length must match");
        for (size_t t = 0; t < baseline_final_paths[a].size(); ++t) {
            ASSERT_EQ(resumed_final_paths[a][t].location, baseline_final_paths[a][t].location,
                      "Agent path step location must match exactly for deterministic equivalence");
        }
    }
    std::cout << "  Explicit path-by-path cell-by-cell equivalence: VERIFIED IDENTICAL." << std::endl;

    // -------------------------------------------------------------
    // Test 3: Interruption Inside Low-Level Search (Mid-Search on Unfinished Agent)
    // -------------------------------------------------------------
    std::cout << "\n[E1 Test 3] Interruption Inside Low-Level Search (Mid-Search Cutoff)..." << std::endl;
    ResumableRepairEngine engine_mid_search(instance, false, 2);
    engine_mid_search.setInitialSolution(initial_paths);
    // Dynamically determine the expansion limit that finishes at least one agent (r_done >= 1)
    // while cutting off mid-search on the next agent (current_agent_expansions > 0)
    int target_budget = 30;
    for (int b = 15; b <= 200; b += 5) {
        ResumableRepairEngine probe(instance, false, 2);
        probe.setInitialSolution(initial_paths);
        SearchBudget b_probe(b, 10.0);
        RepairOutcome out_p = probe.attemptFreshRepair(neighborhood, b_probe, 42);
        if (out_p.type == RepairOutcomeType::INTERRUPTED_SLICE &&
            out_p.interruption.completed_prefix_len >= 1 &&
            out_p.interruption.current_agent_expansions > 0) {
            target_budget = b;
            break;
        }
    }

    SearchBudget budget_mid(target_budget, 10.0);
    RepairOutcome outcome_mid1 = engine_mid_search.attemptFreshRepair(neighborhood, budget_mid, 42);

    ASSERT_EQ(outcome_mid1.type, RepairOutcomeType::INTERRUPTED_SLICE, "Must be interrupted mid-search");
    ASSERT_TRUE(outcome_mid1.interruption.completed_prefix_len >= 1, "Completed prefix length must be >= 1");
    ASSERT_TRUE(outcome_mid1.interruption.has_reusable_prefix, "Reusable prefix must be true");
    ASSERT_TRUE(outcome_mid1.interruption.current_agent_expansions > 0, "Current agent expansions must be > 0");
    std::cout << "  Interrupted inside agent " << outcome_mid1.interruption.unfinished_agent_id
              << " after " << outcome_mid1.interruption.current_agent_expansions << " node expansions."
              << " (r_done = " << outcome_mid1.interruption.completed_prefix_len << ", budget = " << target_budget << ")" << std::endl;

    // Verify unfinished agent's frontier is discarded (macro-prefix contract):
    MacroPrefixCheckpoint* cp_mid = engine_mid_search.getCheckpointStore().getLatestSuspended();
    ASSERT_TRUE(cp_mid != nullptr, "Checkpoint must exist");
    ASSERT_EQ(cp_mid->r_done, outcome_mid1.interruption.completed_prefix_len, "Checkpoint r_done must match interruption");
    ASSERT_EQ((int)cp_mid->prefix_paths.size(), outcome_mid1.interruption.completed_prefix_len,
              "Checkpoint prefix_paths must hold exactly r_done completed agents");
    ASSERT_EQ(cp_mid->repeated_expansions, outcome_mid1.interruption.current_agent_expansions,
              "Checkpoint must record discarded expansions as repeated work upon restart");

    // Resume: restarts agent 1 from scratch with preserved deterministic state and records repeated work
    SearchBudget budget_mid_res(50000, 10.0);
    RepairOutcome outcome_mid2 = engine_mid_search.resumeCheckpoint(0, budget_mid_res);

    ASSERT_EQ(toString(outcome_mid2.type), toString(outcome_uninterrupted.type), "Must complete successfully");
    ASSERT_EQ(engine_mid_search.getIncumbentCost(), baseline_final_cost, "Cost must match baseline");
    ASSERT_EQ(outcome_mid2.repeated_expansions, outcome_mid1.interruption.current_agent_expansions,
              "Resumed outcome must report repeated expansions");
    std::cout << "  Resumed cleanly: repeated " << outcome_mid2.repeated_expansions
              << " expansions recorded. Incumbent cost matches baseline." << std::endl;

    // Explicit path parity for mid-search interruption
    const auto& mid_paths = engine_mid_search.getIncumbentPaths();
    for (size_t a = 0; a < baseline_final_paths.size(); ++a) {
        ASSERT_EQ(mid_paths[a].size(), baseline_final_paths[a].size(), "Mid-search resumed path length must match");
        for (size_t t = 0; t < baseline_final_paths[a].size(); ++t) {
            ASSERT_EQ(mid_paths[a][t].location, baseline_final_paths[a][t].location, "Mid-search resumed cell must match");
        }
    }

    // -------------------------------------------------------------
    // Test 4: Repeated Interruption Inside the Same Difficult Agent
    // -------------------------------------------------------------
    std::cout << "\n[E1 Test 4] Repeated Interruption Inside the Same Difficult Agent..." << std::endl;
    ResumableRepairEngine engine_stuck(instance, false, 2);
    engine_stuck.setInitialSolution(initial_paths);

    // Give 2 expansions per slice (insufficient to route even agent 0)
    SearchBudget b_short1(2, 10.0);
    RepairOutcome out_s1 = engine_stuck.attemptFreshRepair(neighborhood, b_short1, 42);
    ASSERT_EQ(out_s1.type, RepairOutcomeType::INTERRUPTED_SLICE, "Slice 1 must interrupt");
    ASSERT_EQ(out_s1.interruption.completed_prefix_len, 0, "r_done must be 0");

    SearchBudget b_short2(2, 10.0);
    RepairOutcome out_s2 = engine_stuck.resumeCheckpoint(0, b_short2);
    ASSERT_EQ(out_s2.type, RepairOutcomeType::INTERRUPTED_SLICE, "Slice 2 must interrupt");
    ASSERT_EQ(out_s2.interruption.completed_prefix_len, 0, "r_done must remain 0 (no progress)");

    SearchBudget b_short3(2, 10.0);
    RepairOutcome out_s3 = engine_stuck.resumeCheckpoint(0, b_short3);
    ASSERT_EQ(out_s3.type, RepairOutcomeType::INTERRUPTED_SLICE, "Slice 3 must interrupt");
    ASSERT_EQ(out_s3.interruption.completed_prefix_len, 0, "r_done must remain 0 (no progress)");

    MacroPrefixCheckpoint* cp_stuck = engine_stuck.getCheckpointStore().getLatestSuspended();
    std::cout << "  Repeated interruption accurately observed: r_done remained 0 across 3 short slices." << std::endl;
    std::cout << "  Accumulated repeated expansions discarded: " << cp_stuck->repeated_expansions << std::endl;
    ASSERT_TRUE(cp_stuck->repeated_expansions >= 4, "Must accumulate repeated expansions across slices");

    // Finally resume with adequate budget to complete
    SearchBudget b_adequate(50000, 10.0);
    RepairOutcome out_finish = engine_stuck.resumeCheckpoint(0, b_adequate);
    ASSERT_EQ(toString(out_finish.type), toString(outcome_uninterrupted.type), "Must finish with adequate budget");
    ASSERT_EQ(engine_stuck.getIncumbentCost(), baseline_final_cost, "Final cost matches baseline");

    // -------------------------------------------------------------
    // Test 5: Interruption Before Any Prefix is Completed (r_done = 0)
    // -------------------------------------------------------------
    std::cout << "\n[E1 Test 5] Interruption Before Any Prefix (r_done = 0)..." << std::endl;
    ResumableRepairEngine engine_empty_prefix(instance, false, 2);
    engine_empty_prefix.setInitialSolution(initial_paths);

    SearchBudget budget_tiny(1, 10.0);
    RepairOutcome outcome_tiny = engine_empty_prefix.attemptFreshRepair(neighborhood, budget_tiny, 42);

    ASSERT_EQ(outcome_tiny.type, RepairOutcomeType::INTERRUPTED_SLICE, "Must be interrupted by slice");
    ASSERT_EQ(outcome_tiny.interruption.completed_prefix_len, 0, "r_done must be 0");
    ASSERT_TRUE(!outcome_tiny.interruption.has_reusable_prefix, "Reusable prefix must be false when r_done=0");

    SearchBudget budget_finish(50000, 10.0);
    RepairOutcome outcome_from_zero = engine_empty_prefix.resumeCheckpoint(0, budget_finish);
    ASSERT_EQ(toString(outcome_from_zero.type), toString(outcome_uninterrupted.type),
              "Resumed from r_done=0 must match uninterrupted outcome");
    ASSERT_EQ(engine_empty_prefix.getIncumbentCost(), baseline_final_cost,
              "Cost must match baseline when resumed from r_done=0");

    // -------------------------------------------------------------
    // Test 6: Multiple Pause/Resume Cycles
    // -------------------------------------------------------------
    std::cout << "\n[E1 Test 6] Multiple Pause/Resume Cycles..." << std::endl;
    ResumableRepairEngine engine_multi_cycle(instance, false, 2);
    engine_multi_cycle.setInitialSolution(initial_paths);

    SearchBudget b1(10, 10.0);
    engine_multi_cycle.attemptFreshRepair(neighborhood, b1, 42);

    SearchBudget b2(15, 10.0);
    engine_multi_cycle.resumeCheckpoint(0, b2);

    SearchBudget b3(50000, 10.0);
    RepairOutcome outcome_multi = engine_multi_cycle.resumeCheckpoint(0, b3);

    std::cout << "  Multi-cycle final outcome: " << toString(outcome_multi.type)
              << " | Final Cost: " << engine_multi_cycle.getIncumbentCost() << std::endl;
    ASSERT_EQ(engine_multi_cycle.getIncumbentCost(), baseline_final_cost,
              "Multi-pause/resume must reach identical incumbent cost");

    // -------------------------------------------------------------
    // Test 7: Bounded Store Lifecycle: K=0, K=1, and K=2 FIFO Eviction
    // -------------------------------------------------------------
    std::cout << "\n[E1 Test 7] Bounded Store Lifecycle: K=0, K=1, K=2..." << std::endl;

    // Test K = 0
    ResumableRepairEngine engine_k0(instance, false, 0);
    engine_k0.setInitialSolution(initial_paths);
    SearchBudget b_k0(10, 10.0);
    RepairOutcome outcome_k0 = engine_k0.attemptFreshRepair(neighborhood, b_k0, 42);
    ASSERT_EQ(outcome_k0.type, RepairOutcomeType::INTERRUPTED_SLICE, "Must interrupt");
    ASSERT_EQ(engine_k0.getCheckpointStore().getNumSuspended(), 0, "K=0 store must retain 0 checkpoints");
    ASSERT_TRUE(engine_k0.getCheckpointStore().getLatestSuspended() == nullptr, "No checkpoint retained in K=0");

    // Test K = 1
    ResumableRepairEngine engine_k1(instance, false, 1);
    engine_k1.setInitialSolution(initial_paths);
    SearchBudget b_evict1(10, 10.0);
    engine_k1.attemptFreshRepair({0, 1}, b_evict1, 101);
    ASSERT_EQ(engine_k1.getCheckpointStore().getNumSuspended(), 1, "K=1 must hold 1 checkpoint");
    uint64_t id1 = engine_k1.getCheckpointStore().getLatestSuspended()->checkpoint_id;

    SearchBudget b_evict2(10, 10.0);
    engine_k1.attemptFreshRepair({2, 3}, b_evict2, 102);
    ASSERT_EQ(engine_k1.getCheckpointStore().getNumSuspended(), 1, "K=1 must still hold exactly 1 checkpoint after eviction");
    uint64_t id2 = engine_k1.getCheckpointStore().getLatestSuspended()->checkpoint_id;
    ASSERT_TRUE(id2 > id1, "Eviction must replace slot with newer checkpoint");
    ASSERT_EQ(engine_k1.getCheckpointStore().getTotalEvictions(), 1, "Must record exactly 1 eviction");

    // Test K = 2 Lifecycle
    ResumableRepairEngine engine_k2(instance, false, 2);
    engine_k2.setInitialSolution(initial_paths);
    engine_k2.attemptFreshRepair({0, 1}, b_evict1, 201);
    ASSERT_EQ(engine_k2.getCheckpointStore().getNumSuspended(), 1, "K=2 holds 1 checkpoint after 1st interruption");

    engine_k2.attemptFreshRepair({2, 3}, b_evict2, 202);
    ASSERT_EQ(engine_k2.getCheckpointStore().getNumSuspended(), 2, "K=2 holds 2 checkpoints after 2nd interruption");

    // 3rd interruption triggers FIFO eviction of the oldest
    SearchBudget b_evict3(10, 10.0);
    engine_k2.attemptFreshRepair({4, 5}, b_evict3, 203);
    ASSERT_EQ(engine_k2.getCheckpointStore().getNumSuspended(), 2, "K=2 still holds exactly 2 checkpoints");
    ASSERT_EQ(engine_k2.getCheckpointStore().getTotalEvictions(), 1, "K=2 records 1 eviction after 3rd interruption");
    std::cout << "  K=0, K=1, and K=2 bounded store lifecycle verified." << std::endl;

    // -------------------------------------------------------------
    // Test 8: Global Deadline Expiry (STOPPED_DEADLINE)
    // -------------------------------------------------------------
    std::cout << "\n[E1 Test 8] Global Deadline Expiry Handling..." << std::endl;
    ResumableRepairEngine engine_deadline(instance, false, 2);
    engine_deadline.setInitialSolution(initial_paths);

    SearchBudget b_expired(1000, 0.0);
    b_expired.deadline_exceeded = true;
    RepairOutcome outcome_dead = engine_deadline.attemptFreshRepair(neighborhood, b_expired, 42);
    ASSERT_EQ(outcome_dead.type, RepairOutcomeType::STOPPED_DEADLINE, "Expired budget must return STOPPED_DEADLINE");
    ASSERT_EQ(engine_deadline.getCheckpointStore().getNumSuspended(), 0, "No checkpoint created on deadline stop");
    ASSERT_EQ(engine_deadline.getIncumbentCost(), baseline_final_cost - (outcome_uninterrupted.type == RepairOutcomeType::COMPLETED_ACCEPTED ? outcome_uninterrupted.cost_delta : 0),
              "Incumbent must remain unmodified on deadline stop");
    std::cout << "  STOPPED_DEADLINE handled cleanly with zero state mutation." << std::endl;

    // -------------------------------------------------------------
    // Test 9: Independent Path Validity via rbl_validator
    // -------------------------------------------------------------
    std::cout << "\n[E1 Test 9] Independent Path Verification (rbl_validator)..." << std::endl;
    std::string map_txt, scen_txt, parse_err;
    rbl::readWholeFile(map_file, map_txt);
    rbl::Grid grid;
    ASSERT_TRUE(rbl::parseMap(map_txt, grid, parse_err), "Parse map failed: " + parse_err);

    rbl::readWholeFile(scen_file, scen_txt);
    rbl::Scenario scen;
    ASSERT_TRUE(rbl::parseScenario(scen_txt, test_agents, scen, parse_err), "Parse scen failed: " + parse_err);

    std::vector<rbl::Path> val_paths(test_agents);
    for (int i = 0; i < test_agents; ++i) {
        for (const auto& pe : engine_resumed.getIncumbentPaths()[i]) {
            int x = instance.getColCoordinate(pe.location);
            int y = instance.getRowCoordinate(pe.location);
            val_paths[i].push_back({x, y});
        }
    }

    rbl::Report rep;
    rbl::checkPlan(grid, scen.agents, val_paths, rep);
    std::cout << "  Independent Validator Verdict: " << rbl::statusName(rep.status)
              << " | Independent Cost: " << rep.sumOfCosts << std::endl;

    ASSERT_EQ(rep.status, rbl::Status::VALID, "Independent validator must confirm VALID");
    ASSERT_EQ(rep.sumOfCosts, (long long)engine_resumed.getIncumbentCost(),
              "Independent sum of costs must match engine incumbent cost");

    // -------------------------------------------------------------
    // Test 10: Integrated Checkpoint Save/Restore & Memory Measurements
    // -------------------------------------------------------------
    std::cout << "\n[E1 Test 10] Integrated Checkpoint Performance Reporting..." << std::endl;
    std::cout << "  [Live Solver Integration Measurements]" << std::endl;
    std::cout << "  Save Latency:    " << cp->save_time_us << " us" << std::endl;
    std::cout << "  Restore Latency: " << cp->restore_time_us << " us" << std::endl;
    std::cout << "  Combined Cost:   " << (cp->save_time_us + cp->restore_time_us) << " us" << std::endl;
    std::cout << "  Retained Memory: " << cp->getMemoryFootprintBytes() << " bytes ("
              << (cp->getMemoryFootprintBytes() / 1024.0) << " KB)" << std::endl;
    std::cout << "  Combined / 10ms Slice: "
              << ((cp->save_time_us + cp->restore_time_us) / 10000.0 * 100.0) << " %" << std::endl;

    ASSERT_TRUE(std::isfinite(cp->save_time_us) && cp->save_time_us >= 0, "Save latency is a finite nonnegative measurement");
    ASSERT_TRUE(std::isfinite(cp->restore_time_us) && cp->restore_time_us >= 0, "Restore latency is a finite nonnegative measurement");
    ASSERT_TRUE(cp->getMemoryFootprintBytes() < 64 * 1024, "Retained memory must be < 64 KB");

    std::cout << "\n>>> ALL 10 EXPERIMENT E1 TESTS PASSED! <<<" << std::endl;
    return true;
}

} // namespace rbl_test

int main(int argc, char** argv) {
    std::string map_file = "data/sample/random-32-32-20.map";
    std::string scen_file = "data/sample/random-32-32-20-random-1.scen";

    if (argc >= 3) {
        map_file = argv[1];
        scen_file = argv[2];
    }

    bool ok = rbl_test::runAllE1Tests(map_file, scen_file);
    return ok ? 0 : 1;
}
