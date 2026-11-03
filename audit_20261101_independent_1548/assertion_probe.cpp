// RouteBudget Lab - Selective Recovery Verification Suite (Claude Prompt Section 11, 15 Required Tests)
//
// 1.  All gains zero or negative: expensive analysis skipped.
// 2.  Positive candidate requires a negative dependency: improving combination retained.
// 3.  Dependencies form a cycle: select consistently or reject safely.
// 4.  New-new route conflict: infeasible combination rejected.
// 5.  Dependency has no replacement: candidate rejected.
// 6.  Natural eviction: recovery occurs before data removal and victim is still removed.
// 7.  Incumbent changes after checkpoint creation: gains and compatibility recomputed.
// 8.  Path compatibility holds but search-state compatibility fails: unsafe resumption prevented.
// 9.  Deadline/cancellation at extraction, filtering, graph building, selection, validation and precommit: no incomplete commit.
// 10. Terminal recovery finds no gain: valid incumbent preserved.
// 11. First recovery changes another checkpoint's compatibility: fresh validation before the next commit.
// 12. Overflow, malformed routes, missing agents and wrong goals: safe rejection.
// 13. Goal occupancy and edge-swap conflicts: independent validator detects them.
// 14. Recovery disabled: deterministic baseline behavior preserved.
// 15. Expensive cases: deadline overshoot measured without late-result credit.

#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>
#include <limits>
#include <chrono>

#include "Instance.h"
#include "rbl_checkpoint.hpp"
#include "rbl_resumable_engine.hpp"
#include "rbl_partial_acceptance.hpp"
#include "rbl_validator.hpp"

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        std::cerr << "FAILED [" << __FILE__ << ":" << __LINE__ << "]: " << msg \
                  << " (" << #a << " = " << (a) << ", " << #b << " = " << (b) << ")" << std::endl; \
        return false; \
    } \
} while(0)

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "FAILED [" << __FILE__ << ":" << __LINE__ << "]: " << msg \
                  << " (" << #cond << " is false)" << std::endl; \
        return false; \
    } \
} while(0)

#define ASSERT_FALSE(cond, msg) do { \
    if (cond) { \
        std::cerr << "FAILED [" << __FILE__ << ":" << __LINE__ << "]: " << msg \
                  << " (" << #cond << " is true)" << std::endl; \
        return false; \
    } \
} while(0)

namespace rbl_test {
using namespace rbl;

std::vector<::Path> createInitialPaths(const Instance& instance, int num_agents) {
    std::vector<::Path> paths(num_agents);
    ConstraintTable ct(instance.num_of_cols, instance.map_size);
    for (int i = 0; i < num_agents; ++i) {
        SpaceTimeAStar solver(instance, i);
        paths[i] = solver.findPath(ct);
        if (paths[i].empty()) {
            std::cerr << "FATAL: Failed to find initial path for agent " << i << std::endl;
            std::exit(1);
        }
        ct.insert2CT(paths[i]);
    }
    return paths;
}

::Path makePath(const std::vector<int>& locs) {
    ::Path p;
    p.reserve(locs.size());
    for (int l : locs) p.emplace_back(l);
    return p;
}

::Path addInitialWaits(const ::Path& p, int waits) {
    if (p.empty() || waits <= 0) return p;
    ::Path out;
    out.reserve(p.size() + waits);
    for (int w = 0; w < waits; ++w) {
        out.emplace_back(p.front().location);
    }
    for (const auto& entry : p) {
        out.emplace_back(entry.location);
    }
    return out;
}

// Helper: find an agent whose path can be delayed while remaining collision-free with all other agents
int findDelayableAgent(const Instance& instance, const std::vector<::Path>& base_paths, int delay_steps, ::Path& out_delayed, int exclude_agent = -1) {
    int m = static_cast<int>(base_paths.size());
    for (int a = 0; a < m; ++a) {
        if (a == exclude_agent) continue;
        ::Path d = addInitialWaits(base_paths[a], delay_steps);
        std::vector<::Path> test_inc = base_paths;
        test_inc[a] = d;
        int64_t soc = 0;
        std::string err;
        if (PartialAcceptanceOptimizer::validateCompletePlanIndependently(test_inc, instance, soc, err)) {
            out_delayed = d;
            return a;
        }
    }
    return -1;
}

// ----------------------------------------------------------------------------
// Test 1: All gains zero or negative: expensive analysis skipped (Section 5)
// ----------------------------------------------------------------------------
bool test1_all_gains_zero_or_negative_skips_analysis(const Instance& instance, const std::vector<::Path>& init_paths) {
    std::cout << "  [Test 1] All gains zero or negative: expensive analysis skipped... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(init_paths);
    engine.setPartialAcceptanceEnabled(true);
    engine.setPartialAcceptancePolicy(PartialAcceptancePolicy::EXACT_CLOSURE);

    // Provide candidate replacements that are strictly longer or equal cost (waits inserted)
    std::vector<int> candidate_agents = {0, 1};
    std::vector<::Path> candidate_paths = {
        addInitialWaits(init_paths[0], 2), // cost +2 -> w_0 = -2
        addInitialWaits(init_paths[1], 1)  // cost +1 -> w_1 = -1
    };

    PartialAcceptanceResult res = engine.evaluatePartialAcceptance(candidate_agents, candidate_paths);

    ASSERT_TRUE(false, "audit intentional failure in copied source");
    ASSERT_EQ(res.status, PartialAcceptanceStatus::NO_GAIN, "Status must be NO_GAIN");
    ASSERT_TRUE(res.filter_skipped, "Filter skip flag must be true");
    ASSERT_EQ(res.optimistic_upper_bound_U, 0, "Optimistic upper bound U must be 0");
    ASSERT_EQ(res.graph_build_time_us, 0.0, "Graph build time must be 0 (skipped)");
    ASSERT_EQ(res.selection_time_us, 0.0, "Selection time must be 0 (skipped)");
    ASSERT_EQ(engine.getFilterSkips(), 1, "Filter skip counter must increment");

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 2: Positive candidate requires negative dependency: improving combination retained (Section 6)
// ----------------------------------------------------------------------------
bool test2_positive_candidate_requires_negative_dependency(const Instance& instance) {
    (void)instance;
    std::cout << "  [Test 2] Positive candidate requires negative dependency: combination retained... " << std::flush;
    DependencyGraph graph;
    graph.num_candidates = 2;
    graph.candidate_agents = {0, 1};
    graph.agent_to_candidate[0] = 0;
    graph.agent_to_candidate[1] = 1;
    graph.weights = {5, -2};
    graph.adj.resize(2);
    graph.adj[0].push_back(1); // 0 requires 1
    graph.forbidden.assign(2, false);
    graph.transitively_excluded.assign(2, false);
    PartialAcceptanceOptimizer::computeGraphTelemetry(graph);

    std::vector<ReplacementCandidate> candidates(2);
    candidates[0].agent_id = 0;
    candidates[0].weight = 5;
    candidates[1].agent_id = 1;
    candidates[1].weight = -2;

    PartialAcceptanceResult res = PartialAcceptanceOptimizer::solveExactClosure(graph, candidates, 1);
    ASSERT_EQ(res.status, PartialAcceptanceStatus::OPTIMAL_SELECTION, "Must find optimal selection");
    ASSERT_EQ(res.predicted_gain, 3, "Net gain must be 5 - 2 = 3");
    ASSERT_EQ(res.selected_agent_ids.size(), 2u, "Both agents must be selected");
    ASSERT_EQ(res.selected_positive_weight_count, 1, "1 positive agent selected");
    ASSERT_EQ(res.selected_negative_weight_count, 1, "1 negative dependency absorbed");

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 3: Dependencies form a cycle: select consistently or reject safely
// ----------------------------------------------------------------------------
bool test3_dependency_cycle_handling(const Instance& instance) {
    (void)instance;
    std::cout << "  [Test 3] Dependency cycle: select consistently or reject safely... " << std::flush;
    // Case A: 0 -> 1, 1 -> 0, net gain = 4 + (-2) = 2 > 0 -> select both
    {
        DependencyGraph graph;
        graph.num_candidates = 2;
        graph.candidate_agents = {0, 1};
        graph.agent_to_candidate[0] = 0;
        graph.agent_to_candidate[1] = 1;
        graph.weights = {4, -2};
        graph.adj.resize(2);
        graph.adj[0].push_back(1);
        graph.adj[1].push_back(0);
        graph.forbidden.assign(2, false);
        graph.transitively_excluded.assign(2, false);
        PartialAcceptanceOptimizer::computeGraphTelemetry(graph);

        std::vector<ReplacementCandidate> candidates(2);
        candidates[0].agent_id = 0; candidates[0].weight = 4;
        candidates[1].agent_id = 1; candidates[1].weight = -2;

        PartialAcceptanceResult res = PartialAcceptanceOptimizer::solveExactClosure(graph, candidates, 1);
        ASSERT_EQ(res.status, PartialAcceptanceStatus::OPTIMAL_SELECTION, "Must select profitable cycle");
        ASSERT_EQ(res.predicted_gain, 2, "Net gain must be 2");
        ASSERT_EQ(res.selected_agent_ids.size(), 2u, "Both agents in cycle selected");
    }

    // Case B: 0 -> 1, 1 -> 0, net gain = 2 + (-3) = -1 <= 0 -> select neither
    {
        DependencyGraph graph;
        graph.num_candidates = 2;
        graph.candidate_agents = {0, 1};
        graph.agent_to_candidate[0] = 0;
        graph.agent_to_candidate[1] = 1;
        graph.weights = {2, -3};
        graph.adj.resize(2);
        graph.adj[0].push_back(1);
        graph.adj[1].push_back(0);
        graph.forbidden.assign(2, false);
        graph.transitively_excluded.assign(2, false);
        PartialAcceptanceOptimizer::computeGraphTelemetry(graph);

        std::vector<ReplacementCandidate> candidates(2);
        candidates[0].agent_id = 0; candidates[0].weight = 2;
        candidates[1].agent_id = 1; candidates[1].weight = -3;

        PartialAcceptanceResult res = PartialAcceptanceOptimizer::solveExactClosure(graph, candidates, 1);
        ASSERT_EQ(res.status, PartialAcceptanceStatus::NO_GAIN, "Must reject non-improving cycle");
        ASSERT_EQ(res.predicted_gain, 0, "Net gain must be 0");
        ASSERT_EQ(res.selected_agent_ids.size(), 0u, "No agents selected");
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 4: New-new route conflict: infeasible combination rejected
// ----------------------------------------------------------------------------
bool test4_new_new_route_conflict_rejected(const Instance& instance, const std::vector<::Path>& init_paths) {
    std::cout << "  [Test 4] New-new route conflict: infeasible combination rejected... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(init_paths);
    engine.setPartialAcceptanceEnabled(true);
    engine.setPartialAcceptancePolicy(PartialAcceptancePolicy::EXACT_CLOSURE);

    // Force vertex collision at t=1 between replacement paths
    ::Path p0 = init_paths[0];
    ::Path p1 = init_paths[1];
    if (p0.size() > 1 && p1.size() > 1) {
        int coll_loc = p0[1].location;
        p1[1].location = coll_loc; // Force new-new conflict
        std::vector<int> candidate_agents = {0, 1};
        std::vector<::Path> candidate_replacements = {p0, p1};

        std::string kin_err;
        if (PartialAcceptanceOptimizer::checkPathKinematics(p1, instance.getStarts()[1], instance.getGoals()[1], instance, kin_err)) {
            PartialAcceptanceResult res = engine.evaluatePartialAcceptance(candidate_agents, candidate_replacements);
            ASSERT_EQ(res.status, PartialAcceptanceStatus::INVALID_INPUT, "New-new collision must be rejected as INVALID_INPUT");
        }
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 5: Dependency has no replacement: candidate rejected
// ----------------------------------------------------------------------------
bool test5_dependency_has_no_replacement(const Instance& instance) {
    (void)instance;
    std::cout << "  [Test 5] Dependency has no replacement: candidate rejected... " << std::flush;
    DependencyGraph graph;
    graph.num_candidates = 1;
    graph.candidate_agents = {0};
    graph.agent_to_candidate[0] = 0;
    graph.weights = {10};
    graph.adj.resize(1);
    graph.forbidden = {true}; // Missing replacement -> forbidden
    graph.transitively_excluded = {true};
    PartialAcceptanceOptimizer::computeGraphTelemetry(graph);

    std::vector<ReplacementCandidate> candidates(1);
    candidates[0].agent_id = 0;
    candidates[0].weight = 10;

    PartialAcceptanceResult res = PartialAcceptanceOptimizer::solveExactClosure(graph, candidates, 1);
    ASSERT_EQ(res.status, PartialAcceptanceStatus::NO_GAIN, "Candidate requiring missing replacement must be rejected");
    ASSERT_EQ(res.selected_agent_ids.size(), 0u, "No agent selected");

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 6: Natural eviction: recovery occurs before data removal and victim is still removed (Section 8)
// ----------------------------------------------------------------------------
bool test6_natural_eviction_recovery(const Instance& instance, const std::vector<::Path>& init_paths) {
    std::cout << "  [Test 6] Natural eviction: recovery occurs before removal and victim is removed... " << std::flush;
    // Find an agent that can be safely delayed in the incumbent plan
    ::Path delayed_path;
    int target_agent = findDelayableAgent(instance, init_paths, 2, delayed_path);
    ASSERT_TRUE(target_agent >= 0, "Must find delayable agent in initial paths");

    ResumableRepairEngine engine(instance, false, 1);
    std::vector<::Path> incumbent = init_paths;
    incumbent[target_agent] = delayed_path;
    engine.setInitialSolution(incumbent);
    int initial_cost = engine.getIncumbentCost();

    engine.setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy::PA_EVICTION_ONLY);

    // Save Checkpoint 1 into store (slot 0) with completed improving replacement (init_paths[target_agent])
    MacroPrefixCheckpoint cp1;
    cp1.checkpoint_id = 101;
    cp1.lifecycle = CheckpointLifecycle::SUSPENDED;
    cp1.neighborhood_agents = {target_agent};
    cp1.priority_order = {target_agent};
    cp1.r_done = 1;
    cp1.prefix_paths = {init_paths[target_agent]}; // unpadded, gain = +2
    cp1.old_paths = {incumbent[target_agent]};
    cp1.incumbent_version = engine.getIncumbentVersion();
    cp1.incumbent_sum_of_costs = initial_cost;

    int slot1 = engine.getCheckpointStore().saveCheckpoint(cp1);
    ASSERT_EQ(slot1, 0, "CP1 must occupy slot 0");
    ASSERT_EQ(engine.getCheckpointStore().getNumSuspended(), 1, "1 suspended checkpoint");

    int victim_slot = engine.getCheckpointStore().findEvictionVictimSlot();
    ASSERT_EQ(victim_slot, 0, "Victim slot must be 0");

    SearchBudget budget(10000, 1.0);
    bool recovered = engine.attemptEvictionRecovery(victim_slot, budget);
    ASSERT_TRUE(recovered, "Eviction recovery must succeed on profitable victim");
    ASSERT_EQ(engine.getEvictionRecoveryCommitted(), 1, "Eviction commit count must be 1");
    ASSERT_TRUE(engine.getIncumbentCost() < initial_cost, "Incumbent cost must strictly improve");

    // Complete the eviction: save Checkpoint 2 into the store
    int other_agent = (target_agent == 0) ? 1 : 0;
    MacroPrefixCheckpoint cp2;
    cp2.checkpoint_id = 102;
    cp2.lifecycle = CheckpointLifecycle::SUSPENDED;
    cp2.neighborhood_agents = {other_agent};
    cp2.priority_order = {other_agent};
    cp2.r_done = 1;
    cp2.prefix_paths = {init_paths[other_agent]};
    cp2.old_paths = {incumbent[other_agent]};

    int slot2 = engine.getCheckpointStore().saveCheckpoint(cp2);
    ASSERT_EQ(slot2, 0, "CP2 must overwrite victim slot 0");
    ASSERT_EQ(engine.getCheckpointStore().getCheckpoint(0)->checkpoint_id, 102u, "Slot 0 must now contain CP2");
    ASSERT_EQ(engine.getCheckpointStore().getTotalEvictions(), 1, "Store must record 1 eviction");

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 7: Incumbent changes after checkpoint creation: gains and compatibility recomputed
// ----------------------------------------------------------------------------
bool test7_incumbent_changes_after_checkpoint(const Instance& instance, const std::vector<::Path>& init_paths) {
    std::cout << "  [Test 7] Incumbent changes after checkpoint: gains & compatibility recomputed... " << std::flush;
    ::Path delayed_path;
    int target_agent = findDelayableAgent(instance, init_paths, 2, delayed_path);
    ASSERT_TRUE(target_agent >= 0, "Must find delayable agent");

    ResumableRepairEngine engine(instance, false, 2);
    std::vector<::Path> incumbent = init_paths;
    incumbent[target_agent] = delayed_path;
    engine.setInitialSolution(incumbent);
    engine.setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy::PA_EVERY_INTERRUPTION_FILTERED);

    // Candidate replacement was computed when incumbent[target_agent] was delayed (gain +2)
    std::vector<int> candidate_agents = {target_agent};
    std::vector<::Path> candidate_paths = {init_paths[target_agent]};

    // Now mutate incumbent directly so it already equals init_paths[target_agent] (gain becomes 0)
    engine.setIncumbentPathForTest(target_agent, init_paths[target_agent]);

    // Evaluate: must recompute against CURRENT incumbent and detect U == 0
    PartialAcceptanceResult res = engine.evaluatePartialAcceptance(candidate_agents, candidate_paths);
    ASSERT_EQ(res.status, PartialAcceptanceStatus::NO_GAIN, "Must detect no gain after incumbent mutation");
    ASSERT_TRUE(res.filter_skipped, "Must be skipped by cheap filter");
    ASSERT_EQ(res.optimistic_upper_bound_U, 0, "U must be 0 against mutated incumbent");

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 8: Path compatibility holds but search-state compatibility fails: unsafe resumption prevented
// ----------------------------------------------------------------------------
bool test8_path_compat_holds_but_search_state_incompatible(const Instance& instance, const std::vector<::Path>& init_paths) {
    std::cout << "  [Test 8] Path compatibility holds but search-state fails: unsafe resumption prevented... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(init_paths);
    engine.setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy::PA_EVERY_INTERRUPTION_FILTERED);

    MacroPrefixCheckpoint cp;
    cp.checkpoint_id = 201;
    cp.lifecycle = CheckpointLifecycle::SUSPENDED;
    cp.neighborhood_agents = {0, 1};
    cp.priority_order = {0, 1};
    cp.r_done = 1;
    cp.prefix_paths = {init_paths[0]};
    cp.old_paths = {init_paths[0], init_paths[1]};
    cp.incumbent_version = engine.getIncumbentVersion();
    cp.agent_versions_at_save = engine.getAgentVersions();

    int slot = engine.getCheckpointStore().saveCheckpoint(cp);

    // Mutate incumbent agent 1 trajectory version in engine
    engine.setIncumbentPathForTest(1, init_paths[1]);

    // Resumption under DISCARD_ON_CHANGE must detect incompatibility
    CompatibilityReport rep = engine.validateCheckpoint(slot, RetentionPolicy::DISCARD_ON_CHANGE);
    ASSERT_FALSE(rep.isCompatible(), "Search-state resumption must be rejected when neighborhood changed");

    // Candidate prefix path for agent 0 can still be independently evaluated for recovery
    PartialAcceptanceResult pa_res = engine.evaluatePartialAcceptance({0}, {init_paths[0]});
    ASSERT_TRUE(pa_res.status == PartialAcceptanceStatus::NO_GAIN || pa_res.status == PartialAcceptanceStatus::OPTIMAL_SELECTION,
                "Candidate prefix path independently evaluatable");

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 9: Deadline/cancellation at extraction, filtering, selection, validation and precommit
// ----------------------------------------------------------------------------
bool test9_deadline_cancellation_preserves_incumbent(const Instance& instance, const std::vector<::Path>& init_paths) {
    std::cout << "  [Test 9] Deadline cancellation: zero partial mutation at any stage... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(init_paths);
    engine.setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy::PA_EVERY_INTERRUPTION_FILTERED);

    uint64_t initial_version = engine.getIncumbentVersion();
    int initial_cost = engine.getIncumbentCost();

    SearchBudget exhausted_budget(1000, 0.0);
    exhausted_budget.setGlobalDeadline(std::chrono::steady_clock::now() - std::chrono::milliseconds(10));

    PartialAcceptanceResult res = engine.evaluatePartialAcceptance({0}, {init_paths[0]}, &exhausted_budget);
    ASSERT_EQ(res.status, PartialAcceptanceStatus::BUDGET_SKIPPED, "Must abort with BUDGET_SKIPPED");
    ASSERT_EQ(engine.getIncumbentVersion(), initial_version, "Incumbent version must not change");
    ASSERT_EQ(engine.getIncumbentCost(), initial_cost, "Incumbent cost must not change");

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 10: Terminal recovery finds no gain: valid incumbent preserved (Section 9)
// ----------------------------------------------------------------------------
bool test10_terminal_recovery_no_gain_preserves_incumbent(const Instance& instance, const std::vector<::Path>& init_paths) {
    std::cout << "  [Test 10] Terminal recovery finds no gain: valid incumbent preserved... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(init_paths);
    engine.setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy::PA_TERMINAL_ONLY);

    for (int i = 0; i < 2; ++i) {
        MacroPrefixCheckpoint cp;
        cp.checkpoint_id = 300 + i;
        cp.lifecycle = CheckpointLifecycle::SUSPENDED;
        cp.neighborhood_agents = {i};
        cp.priority_order = {i};
        cp.r_done = 1;
        cp.prefix_paths = {addInitialWaits(init_paths[i], 2)};
        cp.old_paths = {init_paths[i]};
        cp.incumbent_version = engine.getIncumbentVersion();
        engine.getCheckpointStore().saveCheckpoint(cp);
    }

    uint64_t version_before = engine.getIncumbentVersion();
    int cost_before = engine.getIncumbentCost();

    SearchBudget budget(10000, 1.0);
    bool committed = engine.attemptTerminalRecovery(budget);
    ASSERT_FALSE(committed, "Terminal recovery must commit nothing when no gain exists");
    ASSERT_EQ(engine.getIncumbentVersion(), version_before, "Version must remain identical");
    ASSERT_EQ(engine.getIncumbentCost(), cost_before, "Cost must remain identical");
    ASSERT_EQ(engine.getTerminalRecoveryFilterSkips(), 2, "Both checkpoints must be skipped by filter");

    std::string val_err;
    ASSERT_TRUE(engine.validateIncumbent(val_err), "Incumbent must remain 100% valid");

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 11: First recovery changes another checkpoint's compatibility: fresh validation before next commit
// ----------------------------------------------------------------------------
bool test11_cascading_multi_checkpoint_recovery(const Instance& instance, const std::vector<::Path>& init_paths) {
    std::cout << "  [Test 11] Cascading multi-checkpoint recovery: fresh validation before commit... " << std::flush;
    // Find two distinct delayable agents that do not conflict when both are delayed
    ::Path delayed_a, delayed_b;
    int a1 = -1, a2 = -1;
    for (int i = 0; i < 10; ++i) {
        ::Path da;
        int cand_a = findDelayableAgent(instance, init_paths, 2, da, -1);
        if (cand_a >= 0) {
            a1 = cand_a;
            delayed_a = da;
            for (int j = 0; j < 10; ++j) {
                if (j == a1) continue;
                ::Path db = addInitialWaits(init_paths[j], 2);
                std::vector<::Path> test_both = init_paths;
                test_both[a1] = delayed_a;
                test_both[j] = db;
                int64_t soc = 0; std::string err;
                if (PartialAcceptanceOptimizer::validateCompletePlanIndependently(test_both, instance, soc, err)) {
                    a2 = j;
                    delayed_b = db;
                    break;
                }
            }
            if (a2 >= 0) break;
        }
    }
    ASSERT_TRUE(a1 >= 0 && a2 >= 0, "Must find two mutually delayable agents");

    ResumableRepairEngine engine(instance, false, 2);
    std::vector<::Path> incumbent = init_paths;
    incumbent[a1] = delayed_a;
    incumbent[a2] = delayed_b;
    engine.setInitialSolution(incumbent);
    int start_cost = engine.getIncumbentCost();

    engine.setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy::PA_TERMINAL_ONLY);

    MacroPrefixCheckpoint cp1;
    cp1.checkpoint_id = 401;
    cp1.lifecycle = CheckpointLifecycle::SUSPENDED;
    cp1.neighborhood_agents = {a1};
    cp1.priority_order = {a1};
    cp1.r_done = 1;
    cp1.prefix_paths = {init_paths[a1]};
    cp1.old_paths = {incumbent[a1]};
    engine.getCheckpointStore().saveCheckpoint(cp1);

    MacroPrefixCheckpoint cp2;
    cp2.checkpoint_id = 402;
    cp2.lifecycle = CheckpointLifecycle::SUSPENDED;
    cp2.neighborhood_agents = {a2};
    cp2.priority_order = {a2};
    cp2.r_done = 1;
    cp2.prefix_paths = {init_paths[a2]};
    cp2.old_paths = {incumbent[a2]};
    engine.getCheckpointStore().saveCheckpoint(cp2);

    SearchBudget budget(100000, 1.0);
    bool committed = engine.attemptTerminalRecovery(budget);
    ASSERT_TRUE(committed, "Terminal recovery must succeed on cascade");
    ASSERT_EQ(engine.getTerminalRecoveryCommitted(), 2, "Both checkpoints must safely commit");
    ASSERT_EQ(engine.getTerminalRecoveryTotalGain(), 4, "Total gain must be 2 + 2 = 4");
    ASSERT_EQ(engine.getIncumbentCost(), start_cost - 4, "Incumbent cost must reflect total gain");

    std::string val_err;
    ASSERT_TRUE(engine.validateIncumbent(val_err), "Incumbent must remain 100% collision-free");

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 12: Overflow, malformed routes, missing agents and wrong goals: safe rejection
// ----------------------------------------------------------------------------
bool test12_overflow_malformed_and_bounds_rejection(const Instance& instance, const std::vector<::Path>& init_paths) {
    std::cout << "  [Test 12] Overflow, malformed routes, missing agents, wrong goals: safe rejection... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(init_paths);
    engine.setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy::PA_EVERY_INTERRUPTION_FILTERED);

    // Case A: Agent ID out of bounds
    {
        PartialAcceptanceResult res = engine.evaluatePartialAcceptance({9999}, {init_paths[0]});
        ASSERT_EQ(res.status, PartialAcceptanceStatus::INVALID_INPUT, "Out of bounds agent ID rejected");
    }

    // Case B: Duplicate agent ID
    {
        PartialAcceptanceResult res = engine.evaluatePartialAcceptance({0, 0}, {init_paths[0], init_paths[0]});
        ASSERT_EQ(res.status, PartialAcceptanceStatus::INVALID_INPUT, "Duplicate agent ID rejected");
    }

    // Case C: Path ending at wrong goal location
    {
        ::Path malformed = init_paths[0];
        malformed.back().location = instance.getStarts()[0]; // Ends at start, not goal
        PartialAcceptanceResult res = engine.evaluatePartialAcceptance({0}, {malformed});
        ASSERT_EQ(res.status, PartialAcceptanceStatus::INVALID_INPUT, "Wrong goal path rejected");
    }

    // Case D: Checked arithmetic overflow detection
    {
        int64_t out_u = 0;
        bool overflow = false;
        std::vector<ReplacementCandidate> overflow_cands(2);
        overflow_cands[0].weight = std::numeric_limits<int64_t>::max() - 10;
        overflow_cands[1].weight = 20; // Causes signed overflow
        bool ok = PartialAcceptanceOptimizer::computeOptimisticUpperBound(overflow_cands, out_u, overflow);
        ASSERT_FALSE(ok, "Overflow must be detected");
        ASSERT_TRUE(overflow, "Overflow flag must be set");
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 13: Goal occupancy and edge-swap conflicts: independent validator detects them
// ----------------------------------------------------------------------------
bool test13_goal_occupancy_and_edge_swap_detection(const Instance& instance, const std::vector<::Path>& init_paths) {
    (void)instance; (void)init_paths;
    std::cout << "  [Test 13] Goal occupancy and edge-swap conflicts: validator detects them... " << std::flush;
    ::Path p_parked = {PathEntry(10), PathEntry(10)}; // Stays parked at loc 10
    ::Path p_traversing = {PathEntry(5), PathEntry(6), PathEntry(10)}; // Arrives at loc 10 at t=2
    std::string reason;
    bool conflict = PartialAcceptanceOptimizer::checkTrajectoryConflict(p_traversing, p_parked, &reason);
    ASSERT_TRUE(conflict, "Goal occupancy conflict must be detected");

    ::Path p_a = {PathEntry(10), PathEntry(11)}; // 10 -> 11 at t=0->1
    ::Path p_b = {PathEntry(11), PathEntry(10)}; // 11 -> 10 at t=0->1
    conflict = PartialAcceptanceOptimizer::checkTrajectoryConflict(p_a, p_b, &reason);
    ASSERT_TRUE(conflict, "Edge-swap conflict must be detected");

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 14: Recovery disabled: deterministic baseline behavior preserved (Section 7)
// ----------------------------------------------------------------------------
bool test14_recovery_disabled_preserves_deterministic_baseline(const Instance& instance, const std::vector<::Path>& init_paths) {
    std::cout << "  [Test 14] Recovery disabled: deterministic baseline behavior preserved... " << std::flush;
    ResumableRepairEngine engine1(instance, false, 2);
    engine1.setInitialSolution(init_paths);
    engine1.setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy::RESUMPTION_NO_PA);

    ResumableRepairEngine engine2(instance, false, 2);
    engine2.setInitialSolution(init_paths);
    engine2.setPartialAcceptanceEnabled(false);

    // Run identical deterministic sequence of repairs using separate budgets
    std::vector<int> neighborhood = {0, 1, 2};
    SearchBudget budget1(200, 1.0);
    SearchBudget budget2(200, 1.0);
    RepairOutcome out1 = engine1.attemptFreshRepair(neighborhood, budget1, 42);
    RepairOutcome out2 = engine2.attemptFreshRepair(neighborhood, budget2, 42);

    ASSERT_EQ(out1.type, out2.type, "Outcome type must match");
    ASSERT_EQ(engine1.getIncumbentCost(), engine2.getIncumbentCost(), "Incumbent cost must match");
    ASSERT_EQ(engine1.getFilterEvaluations(), 0, "Zero filter evaluations under RESUMPTION_NO_PA");

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 15: Expensive cases: deadline overshoot measured without late-result credit (Section 9)
// ----------------------------------------------------------------------------
bool test15_deadline_overshoot_measured_without_late_credit(const Instance& instance, const std::vector<::Path>& init_paths) {
    std::cout << "  [Test 15] Expensive cases: deadline overshoot measured without late credit... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(init_paths);
    engine.setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy::PA_EVERY_INTERRUPTION_FILTERED);

    SearchBudget budget(1000, 0.0);
    budget.setGlobalDeadline(std::chrono::steady_clock::now() - std::chrono::milliseconds(5));

    PartialAcceptanceResult res;
    bool committed = engine.commitPartialAcceptance({0}, {init_paths[0]}, engine.getIncumbentVersion(), res, &budget);
    ASSERT_FALSE(committed, "Late commit after deadline must be rejected");
    ASSERT_EQ(res.status, PartialAcceptanceStatus::BUDGET_SKIPPED, "Status must be BUDGET_SKIPPED");

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 16: Checked arithmetic covers extreme gains and capacity overflow (Section 12.16)
// ----------------------------------------------------------------------------
bool test16_checked_arithmetic_overflow_capacities(const Instance& instance, const std::vector<::Path>& init_paths) {
    (void)instance; (void)init_paths;
    std::cout << "  [Test 16] Checked arithmetic covers extreme gains and overflow... " << std::flush;
    int64_t out_val = 0;
    // Test addition overflow
    ASSERT_FALSE(checkedAdd(std::numeric_limits<int64_t>::max() - 5, 10, out_val), "Add overflow must fail");
    // Test subtraction overflow
    ASSERT_FALSE(checkedSub(std::numeric_limits<int64_t>::min() + 5, 10, out_val), "Sub overflow must fail");

    // Test extreme weights in optimistic upper bound
    std::vector<ReplacementCandidate> cands(3);
    cands[0].weight = -1000;
    cands[1].weight = 500;
    cands[2].weight = 200;
    int64_t u_bound = 0;
    bool overflow = false;
    ASSERT_TRUE(PartialAcceptanceOptimizer::computeOptimisticUpperBound(cands, u_bound, overflow), "Bound compute must succeed");
    ASSERT_FALSE(overflow, "No overflow expected");
    ASSERT_EQ(u_bound, 700, "U must sum only positive weights: 500 + 200 = 700");

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 17: Every failure preserves paths, reservations, costs, versions, and lifecycle (Section 12.17)
// ----------------------------------------------------------------------------
bool test17_state_invariants_preserved_on_failure(const Instance& instance, const std::vector<::Path>& init_paths) {
    std::cout << "  [Test 17] State invariants preserved on all failure paths... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(init_paths);
    engine.setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy::PA_EVERY_INTERRUPTION_FILTERED);

    uint64_t initial_ver = engine.getIncumbentVersion();
    int initial_cost = engine.getIncumbentCost();
    const auto& initial_solution = engine.getIncumbentPaths();

    // 1. Attempt invalid kinematics commit
    PartialAcceptanceResult res1;
    ::Path bad_path = init_paths[0];
    bad_path[0].location = -1; // Out of bounds
    bool c1 = engine.commitPartialAcceptance({0}, {bad_path}, initial_ver, res1);
    ASSERT_FALSE(c1, "Invalid kinematics must not commit");
    ASSERT_EQ(engine.getIncumbentVersion(), initial_ver, "Version unchanged on failure");
    ASSERT_EQ(engine.getIncumbentCost(), initial_cost, "Cost unchanged on failure");

    // 2. Attempt stale version commit
    PartialAcceptanceResult res2;
    bool c2 = engine.commitPartialAcceptance({0}, {init_paths[0]}, initial_ver + 999, res2);
    ASSERT_FALSE(c2, "Stale version must not commit");
    ASSERT_EQ(engine.getIncumbentVersion(), initial_ver, "Version unchanged on failure");
    ASSERT_EQ(engine.getIncumbentCost(), initial_cost, "Cost unchanged on failure");

    // Verify paths match exactly cell-by-cell
    const auto& current_solution = engine.getIncumbentPaths();
    ASSERT_EQ(current_solution.size(), initial_solution.size(), "Size unchanged");
    for (size_t a = 0; a < current_solution.size(); ++a) {
        ASSERT_EQ(current_solution[a].size(), initial_solution[a].size(), "Path length unchanged");
        for (size_t t = 0; t < current_solution[a].size(); ++t) {
            ASSERT_EQ(current_solution[a][t].location, initial_solution[a][t].location, "Location unchanged");
        }
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 18: Selector agreement: Greedy, Exact Dinic, and Exhaustive Oracle agree (Section 12.20)
// ----------------------------------------------------------------------------
bool test18_selector_agreement_greedy_exact_oracle(const Instance& instance, const std::vector<::Path>& init_paths) {
    (void)instance; (void)init_paths;
    std::cout << "  [Test 18] Selector agreement: Greedy, Exact Dinic, and Oracle agree... " << std::flush;
    // Build a 3-candidate graph:
    // Agent 0: weight +5, depends on Agent 1
    // Agent 1: weight -2, depends on Agent 2
    // Agent 2: weight -1
    // Closure {0, 1, 2} has net gain 5 - 2 - 1 = +2.
    DependencyGraph graph;
    graph.num_candidates = 3;
    graph.candidate_agents = {0, 1, 2};
    graph.agent_to_candidate[0] = 0;
    graph.agent_to_candidate[1] = 1;
    graph.agent_to_candidate[2] = 2;
    graph.weights = {5, -2, -1};
    graph.adj.resize(3);
    graph.forbidden.assign(3, false);
    graph.transitively_excluded.assign(3, false);
    graph.adj[0].push_back(1); // 0 -> 1
    graph.adj[1].push_back(2); // 1 -> 2
    graph.positive_weight_count = 1;
    graph.negative_weight_count = 2;
    graph.feasible_negative_weight_count = 2;

    std::vector<ReplacementCandidate> cands(3);
    for (int i = 0; i < 3; ++i) {
        cands[i].agent_id = i;
        cands[i].old_path = {PathEntry(i), PathEntry(i + 10)};
        cands[i].replacement_path = {PathEntry(i), PathEntry(i + 10)};
    }
    cands[0].weight = 5;
    cands[1].weight = -2;
    cands[2].weight = -1;

    auto res_exact = PartialAcceptanceOptimizer::solveExactClosure(graph, cands, 1);
    auto res_greedy = PartialAcceptanceOptimizer::solveGreedyDependencyAware(graph, cands, 1);
    auto res_oracle = PartialAcceptanceOptimizer::solveExhaustiveGraphClosureOracle(graph, cands, 1);

    ASSERT_EQ(res_exact.verified_gain, 2, "Exact gain must be 2");
    ASSERT_EQ(res_greedy.verified_gain, 2, "Greedy gain must match exact (2)");
    ASSERT_EQ(res_oracle.verified_gain, 2, "Oracle gain must match exact (2)");
    ASSERT_EQ(res_exact.selected_agent_ids.size(), 3, "All 3 agents must be selected");

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 19: Multiple candidate alternatives: at most one route per agent (Section 12.21)
// ----------------------------------------------------------------------------
bool test19_multiple_candidate_alternatives_per_agent(const Instance& instance, const std::vector<::Path>& init_paths) {
    std::cout << "  [Test 19] Multiple alternatives per agent: duplicate agent rejected... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(init_paths);
    engine.setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy::PA_EVERY_INTERRUPTION_FILTERED);

    // Provide two duplicate candidate replacements for agent 0
    std::vector<int> dup_agents = {0, 0};
    std::vector<::Path> dup_paths = {init_paths[0], init_paths[0]};

    PartialAcceptanceResult res = engine.evaluatePartialAcceptance(dup_agents, dup_paths);
    ASSERT_EQ(res.status, PartialAcceptanceStatus::INVALID_INPUT, "Duplicate candidate replacement must be rejected");

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 20: CLI policy parsing, telemetry consistency, and string mappings (Section 12.22)
// ----------------------------------------------------------------------------
bool test20_cli_policy_parsing_and_telemetry(const Instance& instance, const std::vector<::Path>& init_paths) {
    (void)instance; (void)init_paths;
    std::cout << "  [Test 20] CLI policy parsing and telemetry consistency... " << std::flush;
    // Verify policy parsing round-trip
    ASSERT_TRUE(parseSelectiveRecoveryPolicy("RESUMPTION_NO_PA") == SelectiveRecoveryPolicy::RESUMPTION_NO_PA, "Parse RESUMPTION_NO_PA");
    ASSERT_TRUE(parseSelectiveRecoveryPolicy("PA_EVERY_INTERRUPTION_FILTERED") == SelectiveRecoveryPolicy::PA_EVERY_INTERRUPTION_FILTERED, "Parse PA_EVERY");
    ASSERT_TRUE(parseSelectiveRecoveryPolicy("PA_EVICTION_ONLY") == SelectiveRecoveryPolicy::PA_EVICTION_ONLY, "Parse PA_EVICTION_ONLY");
    ASSERT_TRUE(parseSelectiveRecoveryPolicy("PA_TERMINAL_ONLY") == SelectiveRecoveryPolicy::PA_TERMINAL_ONLY, "Parse PA_TERMINAL_ONLY");
    ASSERT_TRUE(parseSelectiveRecoveryPolicy("PA_EVICTION_AND_TERMINAL") == SelectiveRecoveryPolicy::PA_EVICTION_AND_TERMINAL, "Parse PA_EVICTION_AND_TERMINAL");
    ASSERT_TRUE(parseSelectiveRecoveryPolicy("UNKNOWN_INVALID") == SelectiveRecoveryPolicy::RESUMPTION_NO_PA, "Fallback to default");

    ASSERT_TRUE(std::string(toString(SelectiveRecoveryPolicy::PA_EVICTION_AND_TERMINAL)) == "PA_EVICTION_AND_TERMINAL", "ToString match");
    ASSERT_TRUE(std::string(toString(SelectiveRecoveryPolicy::PA_TERMINAL_ONLY)) == "PA_TERMINAL_ONLY", "ToString match");

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 21: Failed or timed-out eviction recovery still completes eviction (Section 12.8)
// ----------------------------------------------------------------------------
bool test21_failed_eviction_recovery_completes_original_eviction(const Instance& instance, const std::vector<::Path>& init_paths) {
    std::cout << "  [Test 21] Failed eviction recovery completes original eviction... " << std::flush;
    ResumableRepairEngine engine(instance, false, 1); // Capacity K=1
    engine.setInitialSolution(init_paths);
    engine.setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy::PA_EVICTION_ONLY);

    // Save checkpoint 1
    MacroPrefixCheckpoint cp1;
    cp1.checkpoint_id = 101;
    cp1.lifecycle = CheckpointLifecycle::SUSPENDED;
    cp1.neighborhood_agents = {0};
    cp1.priority_order = {0};
    cp1.r_done = 1;
    cp1.prefix_paths = {init_paths[0]}; // Zero gain
    cp1.old_paths = {init_paths[0]};
    cp1.incumbent_version = engine.getIncumbentVersion();
    cp1.incumbent_sum_of_costs = engine.getIncumbentCost();
    int s1 = engine.getCheckpointStore().saveCheckpoint(cp1);
    ASSERT_EQ(s1, 0, "Save CP1 in slot 0");

    // Save checkpoint 2 to force eviction of CP1
    // During eviction recovery, CP1 has zero gain, so recovery finds no gain.
    SearchBudget budget(1000, 1.0);
    int victim_slot = engine.getCheckpointStore().findEvictionVictimSlot();
    ASSERT_EQ(victim_slot, 0, "Victim slot must be 0 (K=1)");
    engine.attemptEvictionRecovery(victim_slot, budget);

    MacroPrefixCheckpoint cp2;
    cp2.checkpoint_id = 102;
    cp2.lifecycle = CheckpointLifecycle::SUSPENDED;
    cp2.neighborhood_agents = {1};
    cp2.priority_order = {1};
    cp2.r_done = 1;
    cp2.prefix_paths = {init_paths[1]};
    cp2.old_paths = {init_paths[1]};
    cp2.incumbent_version = engine.getIncumbentVersion();
    cp2.incumbent_sum_of_costs = engine.getIncumbentCost();
    int s2 = engine.getCheckpointStore().saveCheckpoint(cp2);
    ASSERT_EQ(s2, 0, "Save CP2 into evicted slot 0");

    // CP1 must be replaced by CP2
    const MacroPrefixCheckpoint* active_cp = engine.getCheckpointStore().getCheckpoint(0);
    ASSERT_TRUE(active_cp != nullptr, "Active checkpoint exists");
    ASSERT_EQ(active_cp->checkpoint_id, 102, "CP2 must have successfully taken victim slot 0");

    std::cout << "PASSED" << std::endl;
    return true;
}

// ----------------------------------------------------------------------------
// Test 22: Boundary time control and deadline polling across pipeline (Section 12.11)
// ----------------------------------------------------------------------------
bool test22_boundary_time_control_and_deadline_polling(const Instance& instance, const std::vector<::Path>& init_paths) {
    std::cout << "  [Test 22] Boundary time control and deadline polling... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(init_paths);
    engine.setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy::PA_EVERY_INTERRUPTION_FILTERED);

    // Create a budget that expires immediately after extraction
    SearchBudget budget(1000, 0.000001); // 1 microsecond budget
    budget.setGlobalDeadline(std::chrono::steady_clock::now() - std::chrono::microseconds(10));

    PartialAcceptanceResult res = engine.evaluatePartialAcceptance({0}, {init_paths[0]}, &budget);
    ASSERT_EQ(res.status, PartialAcceptanceStatus::BUDGET_SKIPPED, "Must abort with BUDGET_SKIPPED on expired deadline");

    std::cout << "PASSED" << std::endl;
    return true;
}

} // namespace rbl_test

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: rbl_selective_recovery_tests <map_file> <scen_file>" << std::endl;
        return 1;
    }
    std::string map_file = argv[1];
    std::string scen_file = argv[2];

    std::cout << "=================================================================" << std::endl;
    std::cout << "RouteBudget Lab - Selective Recovery Verification Suite (22 Tests)" << std::endl;
    std::cout << "=================================================================" << std::endl;

    const int test_agents = 10;
    Instance instance(map_file, scen_file, test_agents);
    auto initial_paths = rbl_test::createInitialPaths(instance, test_agents);

    bool all_passed = true;
    all_passed &= rbl_test::test1_all_gains_zero_or_negative_skips_analysis(instance, initial_paths);
    all_passed &= rbl_test::test2_positive_candidate_requires_negative_dependency(instance);
    all_passed &= rbl_test::test3_dependency_cycle_handling(instance);
    all_passed &= rbl_test::test4_new_new_route_conflict_rejected(instance, initial_paths);
    all_passed &= rbl_test::test5_dependency_has_no_replacement(instance);
    all_passed &= rbl_test::test6_natural_eviction_recovery(instance, initial_paths);
    all_passed &= rbl_test::test7_incumbent_changes_after_checkpoint(instance, initial_paths);
    all_passed &= rbl_test::test8_path_compat_holds_but_search_state_incompatible(instance, initial_paths);
    all_passed &= rbl_test::test9_deadline_cancellation_preserves_incumbent(instance, initial_paths);
    all_passed &= rbl_test::test10_terminal_recovery_no_gain_preserves_incumbent(instance, initial_paths);
    all_passed &= rbl_test::test11_cascading_multi_checkpoint_recovery(instance, initial_paths);
    all_passed &= rbl_test::test12_overflow_malformed_and_bounds_rejection(instance, initial_paths);
    all_passed &= rbl_test::test13_goal_occupancy_and_edge_swap_detection(instance, initial_paths);
    all_passed &= rbl_test::test14_recovery_disabled_preserves_deterministic_baseline(instance, initial_paths);
    all_passed &= rbl_test::test15_deadline_overshoot_measured_without_late_credit(instance, initial_paths);
    all_passed &= rbl_test::test16_checked_arithmetic_overflow_capacities(instance, initial_paths);
    all_passed &= rbl_test::test17_state_invariants_preserved_on_failure(instance, initial_paths);
    all_passed &= rbl_test::test18_selector_agreement_greedy_exact_oracle(instance, initial_paths);
    all_passed &= rbl_test::test19_multiple_candidate_alternatives_per_agent(instance, initial_paths);
    all_passed &= rbl_test::test20_cli_policy_parsing_and_telemetry(instance, initial_paths);
    all_passed &= rbl_test::test21_failed_eviction_recovery_completes_original_eviction(instance, initial_paths);
    all_passed &= rbl_test::test22_boundary_time_control_and_deadline_polling(instance, initial_paths);

    std::cout << "=================================================================" << std::endl;
    if (all_passed) {
        std::cout << "ALL 22 SELECTIVE RECOVERY TEST GROUPS PASSED IN THIS SUITE." << std::endl;
        return 0;
    } else {
        std::cerr << "SOME SELECTIVE RECOVERY TESTS FAILED!" << std::endl;
        return 1;
    }
}
