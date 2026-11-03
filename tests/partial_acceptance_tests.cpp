// RouteBudget Lab - Maximum-Gain Partial Acceptance Verification Suite
// Verifies Section 9 of the Upgrade Guide (17 Required Tests + Audit Extensions):
//
// 1.  Empty replacement set: No mutation and zero gain.
// 2.  Independent improving alternatives: Select all beneficial independent alternatives.
// 3.  No positive feasible gain: Retain incumbent.
// 4.  A requires unavailable B: Forbid A.
// 5.  A requires B; B requires unavailable C: A and B cannot be selected (transitive exclusion).
// 6.  Positive A requires negative B: Select group only when total gain > 0; shared negative dependency where Exact beats Greedy.
// 7.  Zero-weight enabling replacement: Preserve when needed by a profitable group; terminal padding wait consistency.
// 8.  Dependency cycle: Select or reject cycle consistently.
// 9.  Vertex and edge-swap conflicts: Generate the correct constraints.
// 10. Persistent goal occupancy: Detect late conflicts after path termination.
// 11. Mutually conflicting or duplicate replacement paths: Reject input assumptions.
// 12. Wrong start/goal, obstacle move, or invalid incumbent: Reject input assumptions.
// 13. Stale incumbent version: Do not commit.
// 14. Deadline reached / interrupted flow / overflow boundaries: Retain incumbent and distinguish statuses.
// 15. Rejected batch update & transactional lifecycle: Zero partial mutation; checkpoint disposition verified.
// 16. Disabled feature: Deterministic fixed-work baseline preservation across fresh/interrupted/resumed sequences.
// 17. Independent path-mixture exhaustive oracle: Path-level deterministic & randomized fixtures (positive/zero/negative weights,
//     chains, cycles, forbidden nodes, ties, padding waits, nonnegative reference cross-check).

#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <vector>
#include <string>
#include <queue>
#include <algorithm>
#include <random>
#include <limits>
#include <unordered_set>

#include "Instance.h"
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

bool pathsEqual(const ::Path& p1, const ::Path& p2) {
    if (p1.size() != p2.size()) return false;
    for (size_t t = 0; t < p1.size(); ++t) {
        if (p1[t].location != p2[t].location) return false;
    }
    return true;
}

bool allPathsEqual(const std::vector<::Path>& ps1, const std::vector<::Path>& ps2) {
    if (ps1.size() != ps2.size()) return false;
    for (size_t i = 0; i < ps1.size(); ++i) {
        if (!pathsEqual(ps1[i], ps2[i])) return false;
    }
    return true;
}

::Path makePath(const std::vector<int>& locs) {
    ::Path p;
    p.reserve(locs.size());
    for (int l : locs) p.emplace_back(l);
    return p;
}

// Helper: build a valid alternate path by inserting a 2-step out-and-back or wait at start
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

::Path addTerminalGoalWaits(const ::Path& p, int waits) {
    if (p.empty() || waits <= 0) return p;
    ::Path out = p;
    for (int w = 0; w < waits; ++w) {
        out.emplace_back(p.back().location);
    }
    return out;
}

// Helper: convert linear location paths on Instance into rbl::validator Grid/AgentSpec/Path and run rbl::checkPlan
bool verifyWithIndependentValidator(const Instance& instance,
                                    const std::vector<::Path>& plan,
                                    long long& out_soc) {
    rbl::Grid g;
    g.width = instance.num_of_cols;
    g.height = instance.num_of_rows;
    g.passable.assign((size_t)g.width * g.height, 0);
    for (int loc = 0; loc < instance.map_size; ++loc) {
        g.passable[loc] = instance.isObstacle(loc) ? 0 : 1;
    }
    int m = instance.getDefaultNumberOfAgents();
    std::vector<rbl::AgentSpec> specs(m);
    const auto& starts = instance.getStarts();
    const auto& goals = instance.getGoals();
    for (int i = 0; i < m; ++i) {
        specs[i].start = {instance.getColCoordinate(starts[i]), instance.getRowCoordinate(starts[i])};
        specs[i].goal = {instance.getColCoordinate(goals[i]), instance.getRowCoordinate(goals[i])};
    }
    std::vector<rbl::Path> vpaths(m);
    for (int i = 0; i < m; ++i) {
        vpaths[i].reserve(plan[i].size());
        for (const auto& e : plan[i]) {
            vpaths[i].push_back({instance.getColCoordinate(e.location), instance.getRowCoordinate(e.location)});
        }
    }
    rbl::Report rep;
    rbl::checkPlan(g, specs, vpaths, rep);
    out_soc = rep.sumOfCosts;
    return rep.status == rbl::Status::VALID;
}

// -----------------------------------------------------------------------------
// Test 1: Empty replacement set
// -----------------------------------------------------------------------------
bool test1_empty_replacement_set(const Instance& instance, const std::vector<::Path>& base_paths) {
    std::cout << "  [Test 1] Empty replacement set: No mutation and zero gain... ";
    ResumableRepairEngine engine(instance);
    engine.setInitialSolution(base_paths);
    engine.setPartialAcceptanceEnabled(true);
    engine.setPartialAcceptancePolicy(PartialAcceptancePolicy::EXACT_CLOSURE);

    uint64_t v_before = engine.getIncumbentVersion();
    int c_before = engine.getIncumbentCost();

    PartialAcceptanceResult res = engine.evaluatePartialAcceptance({}, {});
    ASSERT_EQ(res.status, PartialAcceptanceStatus::NO_CANDIDATES, "Empty candidates should return NO_CANDIDATES");
    ASSERT_EQ(res.predicted_gain, 0, "Gain must be zero");
    ASSERT_EQ(engine.getIncumbentVersion(), v_before, "Version must be unchanged");
    ASSERT_EQ(engine.getIncumbentCost(), c_before, "Cost must be unchanged");

    PartialAcceptanceResult oracle_res = PartialAcceptanceOptimizer::solveExhaustiveOracle({}, base_paths, instance, v_before);
    ASSERT_EQ(oracle_res.status, PartialAcceptanceStatus::NO_CANDIDATES, "Exhaustive oracle on empty set must return NO_CANDIDATES");

    std::cout << "PASSED" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Independent improving alternatives
// -----------------------------------------------------------------------------
bool test2_independent_improving_alternatives(const Instance& instance, const std::vector<::Path>& base_paths) {
    std::cout << "  [Test 2] Independent improving alternatives: Select all beneficial independent alternatives... ";
    (void)base_paths;

    ReplacementCandidate cA, cB, cC;
    cA.agent_id = 0;
    cA.replacement_path = makePath({0, 1, 2});
    cA.weight = 5;

    cB.agent_id = 1;
    cB.replacement_path = makePath({10, 11, 12});
    cB.weight = 3;

    cC.agent_id = 2;
    cC.replacement_path = makePath({20, 21, 22});
    cC.weight = 4;

    std::vector<::Path> incumbent = {
        makePath({0, 0, 0, 0, 1, 2}),
        makePath({10, 10, 10, 11, 12}),
        makePath({20, 20, 20, 20, 21, 22})
    };

    DependencyGraph graph = PartialAcceptanceOptimizer::buildDependencyGraph({cA, cB, cC}, incumbent, instance);
    ASSERT_EQ(graph.dependency_arc_count, 0, "Independent candidates must have 0 dependencies");
    ASSERT_EQ(graph.forbidden_count, 0, "Independent candidates must have 0 forbidden nodes");
    ASSERT_EQ(graph.positive_weight_count, 3, "All 3 candidates have positive weights");

    PartialAcceptanceResult res = PartialAcceptanceOptimizer::solveExactClosure(graph, {cA, cB, cC}, 1);
    ASSERT_EQ(res.status, PartialAcceptanceStatus::OPTIMAL_SELECTION, "Must select all independent alternatives");
    ASSERT_EQ((int)res.selected_agent_ids.size(), 3, "All 3 agents must be selected");
    ASSERT_EQ(res.predicted_gain, 12, "Gain must equal sum 5 + 3 + 4 = 12");
    ASSERT_TRUE(res.used_nonnegative_fast_path, "Nonnegative condition must be flagged");

    PartialAcceptanceResult nn_res = PartialAcceptanceOptimizer::solveNonnegativeClosureReference(graph, {cA, cB, cC}, 1);
    ASSERT_EQ(nn_res.predicted_gain, 12, "Nonnegative reference must match exact gain");

    std::cout << "PASSED" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: No positive feasible gain
// -----------------------------------------------------------------------------
bool test3_no_positive_feasible_gain(const Instance& instance, const std::vector<::Path>& base_paths) {
    std::cout << "  [Test 3] No positive feasible gain: Retain incumbent... ";
    ResumableRepairEngine engine(instance);
    engine.setInitialSolution(base_paths);
    engine.setPartialAcceptanceEnabled(true);
    engine.setPartialAcceptancePolicy(PartialAcceptancePolicy::EXACT_CLOSURE);

    int a = 0;
    ::Path longer_p = base_paths[a];
    longer_p.insert(longer_p.begin() + 1, base_paths[a][0]); // extra wait step at start

    PartialAcceptanceResult res = engine.evaluatePartialAcceptance({a}, {longer_p});
    ASSERT_EQ(res.status, PartialAcceptanceStatus::NO_GAIN, "Status must be NO_GAIN");
    ASSERT_EQ(res.predicted_gain, 0, "Predicted gain must be 0");
    ASSERT_TRUE(res.selected_agent_ids.empty(), "No agents should be selected");

    std::cout << "PASSED" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: A requires unavailable B -> Forbid A
// -----------------------------------------------------------------------------
bool test4_a_requires_unavailable_b(const Instance& instance) {
    std::cout << "  [Test 4] A requires unavailable B: Forbid A... ";

    ReplacementCandidate c0;
    c0.agent_id = 0;
    c0.replacement_path = makePath({0, 1, 2});
    c0.old_path = makePath({0, 0, 0, 1, 2});
    c0.cost_old = 4;
    c0.cost_new = 2;
    c0.weight = 2;

    std::vector<::Path> incumbent = {
        makePath({0, 0, 0, 1, 2}), // P_0
        makePath({10, 1, 12})       // P_1 (conflicts with Q_0 at loc 1, t=1)
    };

    DependencyGraph graph = PartialAcceptanceOptimizer::buildDependencyGraph({c0}, incumbent, instance);
    ASSERT_EQ(graph.forbidden_count, 1, "Candidate 0 must be forbidden");
    ASSERT_TRUE(graph.forbidden[0], "Candidate 0 must be marked forbidden");

    PartialAcceptanceResult res = PartialAcceptanceOptimizer::solveExactClosure(graph, {c0}, 1);
    ASSERT_EQ(res.status, PartialAcceptanceStatus::NO_GAIN, "Forbidden candidate must yield NO_GAIN");
    ASSERT_TRUE(res.selected_agent_ids.empty(), "Forbidden candidate must not be selected");

    std::cout << "PASSED" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: A requires B; B requires unavailable C -> Neither can be selected
// -----------------------------------------------------------------------------
bool test5_transitive_unavailable(const Instance& instance) {
    std::cout << "  [Test 5] A requires B; B requires unavailable C: Neither can be selected... ";

    ReplacementCandidate c0, c1;
    c0.agent_id = 0;
    c0.replacement_path = makePath({0, 1, 2});
    c0.old_path = makePath({0, 0, 0, 0, 1, 2});
    c0.weight = 5;

    c1.agent_id = 1;
    c1.replacement_path = makePath({10, 20, 30});
    c1.old_path = makePath({10, 10, 20, 30});
    c1.weight = 3;

    std::vector<::Path> incumbent = {
        c0.old_path,
        makePath({10, 1, 30}),   // P_1 conflicts with Q_0 at t=1
        makePath({100, 20, 102}) // P_2 conflicts with Q_1 at t=1 (Agent 2 is unavailable)
    };

    DependencyGraph graph = PartialAcceptanceOptimizer::buildDependencyGraph({c0, c1}, incumbent, instance);
    ASSERT_TRUE(graph.forbidden[1], "Candidate 1 must be forbidden because C is unavailable");
    ASSERT_EQ(graph.transitively_excluded_count, 1, "Candidate 0 must be transitively excluded");
    ASSERT_TRUE(graph.transitively_excluded[0], "Candidate 0 marked transitively excluded");
    ASSERT_EQ((int)graph.adj[0].size(), 1, "Candidate 0 must have dependency arc to Candidate 1");
    ASSERT_EQ(graph.adj[0][0], 1, "0 -> 1 dependency");

    PartialAcceptanceResult res = PartialAcceptanceOptimizer::solveExactClosure(graph, {c0, c1}, 1);
    ASSERT_EQ(res.status, PartialAcceptanceStatus::NO_GAIN, "Must yield NO_GAIN due to transitive forbidden constraint");
    ASSERT_TRUE(res.selected_agent_ids.empty(), "Neither A nor B can be selected");

    PartialAcceptanceResult nn_res = PartialAcceptanceOptimizer::solveNonnegativeClosureReference(graph, {c0, c1}, 1);
    ASSERT_EQ(nn_res.status, PartialAcceptanceStatus::NO_GAIN, "Nonnegative reference also yields NO_GAIN");

    std::cout << "PASSED" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Positive A requires negative B (including shared-dependency case where Exact > Greedy)
// -----------------------------------------------------------------------------
bool test6_positive_requires_negative(const Instance& instance) {
    std::cout << "  [Test 6] Positive A requires negative B & shared negative dependency (Exact > Greedy)... ";

    // Case 6a: w_A = +8, w_B = -2, A -> B. Total gain = +6 > 0 -> Select {A, B}
    {
        ReplacementCandidate cA, cB;
        cA.agent_id = 0;
        cA.replacement_path = makePath({0, 1, 2});
        cA.weight = 8;

        cB.agent_id = 1;
        cB.replacement_path = makePath({10, 11, 12});
        cB.weight = -2;

        std::vector<::Path> incumbent = {
            makePath({0, 0, 0, 0, 0, 0, 0, 0, 1, 2}),
            makePath({10, 1, 12})
        };

        DependencyGraph graph = PartialAcceptanceOptimizer::buildDependencyGraph({cA, cB}, incumbent, instance);
        ASSERT_EQ(graph.feasible_negative_weight_count, 1, "One feasible negative-weight candidate");

        PartialAcceptanceResult res = PartialAcceptanceOptimizer::solveExactClosure(graph, {cA, cB}, 1);
        ASSERT_EQ(res.status, PartialAcceptanceStatus::OPTIMAL_SELECTION, "Must select profitable group");
        ASSERT_EQ((int)res.selected_agent_ids.size(), 2, "Must select both A and B");
        ASSERT_EQ(res.predicted_gain, 6, "Gain must be 8 - 2 = 6");
        ASSERT_EQ(res.selected_negative_weight_count, 1, "Selected 1 negative-weight node");
        ASSERT_FALSE(res.used_nonnegative_fast_path, "Mixed-sign case must not claim nonnegative fast path");

        // Nonnegative reference must reject mixed-sign input
        PartialAcceptanceResult nn_res = PartialAcceptanceOptimizer::solveNonnegativeClosureReference(graph, {cA, cB}, 1);
        ASSERT_EQ(nn_res.status, PartialAcceptanceStatus::INVALID_INPUT, "Nonnegative reference rejects selectable negative weight");
    }

    // Case 6b: w_A = +2, w_B = -8, A -> B. Total gain = -6 <= 0 -> Reject {A, B}
    {
        ReplacementCandidate cA, cB;
        cA.agent_id = 0;
        cA.replacement_path = makePath({0, 1, 2});
        cA.weight = 2;

        cB.agent_id = 1;
        cB.replacement_path = makePath({10, 11, 12});
        cB.weight = -8;

        std::vector<::Path> incumbent = {
            makePath({0, 0, 1, 2}),
            makePath({10, 1, 12})
        };

        DependencyGraph graph = PartialAcceptanceOptimizer::buildDependencyGraph({cA, cB}, incumbent, instance);
        PartialAcceptanceResult res = PartialAcceptanceOptimizer::solveExactClosure(graph, {cA, cB}, 1);
        ASSERT_EQ(res.status, PartialAcceptanceStatus::NO_GAIN, "Unprofitable group must yield NO_GAIN");
        ASSERT_TRUE(res.selected_agent_ids.empty(), "No candidates selected");
    }

    // Case 6c: Shared negative dependency where Exact Min-Cut strictly beats Greedy:
    // A1 (w=+4) -> B (w=-5) and A2 (w=+4) -> B (w=-5).
    // Individually: A1+B = -1 <= 0, A2+B = -1 <= 0 -> Greedy gets 0.
    // Jointly: A1+A2+B = +4 + 4 - 5 = +3 > 0 -> Exact Closure gets +3.
    {
        ReplacementCandidate cA1, cA2, cB;
        cA1.agent_id = 0;
        cA1.replacement_path = makePath({0, 1, 2});
        cA1.weight = 4;

        cA2.agent_id = 1;
        cA2.replacement_path = makePath({20, 21, 22});
        cA2.weight = 4;

        cB.agent_id = 2;
        cB.replacement_path = makePath({30, 31, 32});
        cB.weight = -5;

        // P_2 visits loc 1 at t=1 (conflicts with Q_0) and loc 21 at t=2 (conflicts with Q_1 at goal wait t=2)
        std::vector<::Path> incumbent = {
            makePath({0, 0, 0, 0, 0, 1, 2}),
            makePath({20, 20, 20, 20, 20, 21, 22}),
            makePath({30, 1, 22})
        };

        DependencyGraph graph = PartialAcceptanceOptimizer::buildDependencyGraph({cA1, cA2, cB}, incumbent, instance);
        ASSERT_EQ(graph.dependency_arc_count, 2, "Both A1 and A2 depend on B");

        PartialAcceptanceResult greedy_res = PartialAcceptanceOptimizer::solveGreedyDependencyAware(graph, {cA1, cA2, cB}, 1);
        PartialAcceptanceResult exact_res = PartialAcceptanceOptimizer::solveExactClosure(graph, {cA1, cA2, cB}, 1);

        ASSERT_EQ(greedy_res.status, PartialAcceptanceStatus::NO_GAIN, "Single-root greedy cannot see shared negative dependency");
        ASSERT_EQ(greedy_res.predicted_gain, 0, "Greedy gain is 0");
        ASSERT_EQ(exact_res.status, PartialAcceptanceStatus::OPTIMAL_SELECTION, "Exact min-cut finds joint positive closure");
        ASSERT_EQ(exact_res.predicted_gain, 3, "Exact gain is 4 + 4 - 5 = 3");
        ASSERT_EQ((int)exact_res.selected_agent_ids.size(), 3, "All 3 selected by exact min-cut");
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Zero-weight enabling replacement & terminal padding waits
// -----------------------------------------------------------------------------
bool test7_zero_weight_enabling_replacement(const Instance& instance, const std::vector<::Path>& base_paths) {
    std::cout << "  [Test 7] Zero-weight enabling replacement & terminal padding wait handling... ";

    ReplacementCandidate cA, cB;
    cA.agent_id = 0;
    cA.replacement_path = makePath({0, 1, 2});
    cA.weight = 5;

    cB.agent_id = 1;
    cB.replacement_path = makePath({10, 11, 12});
    cB.weight = 0; // neutral weight

    std::vector<::Path> incumbent = {
        makePath({0, 0, 0, 0, 0, 1, 2}),
        makePath({10, 1, 12}) // P_B conflicts with Q_A at loc 1
    };

    DependencyGraph graph = PartialAcceptanceOptimizer::buildDependencyGraph({cA, cB}, incumbent, instance);
    ASSERT_EQ((int)graph.adj[0].size(), 1, "A requires B");

    PartialAcceptanceResult res = PartialAcceptanceOptimizer::solveExactClosure(graph, {cA, cB}, 1);
    ASSERT_EQ(res.status, PartialAcceptanceStatus::OPTIMAL_SELECTION, "Must select enabling group");
    ASSERT_EQ((int)res.selected_agent_ids.size(), 2, "Must include zero-weight enabler B");
    ASSERT_EQ(res.predicted_gain, 5, "Gain must be 5 + 0 = 5");
    ASSERT_EQ(res.selected_zero_weight_count, 1, "Selected 1 zero-weight enabler");

    // Verify terminal padding waits at goal do not distort arrival cost or weight
    int goal0 = instance.getGoals()[0];
    int raw_arr = PartialAcceptanceOptimizer::computeArrivalCost(base_paths[0], goal0);
    ::Path padded_p0 = addTerminalGoalWaits(base_paths[0], 5);
    int padded_arr = PartialAcceptanceOptimizer::computeArrivalCost(padded_p0, goal0);
    ASSERT_EQ(raw_arr, padded_arr, "Terminal padding waits at goal must not increase computeArrivalCost");

    std::cout << "PASSED" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Dependency cycle
// -----------------------------------------------------------------------------
bool test8_dependency_cycle(const Instance& instance) {
    std::cout << "  [Test 8] Dependency cycle: Select or reject cycle consistently... ";

    // Case 8a: w_A = +5, w_B = -2. Total = +3 > 0 -> Select both {A, B}
    {
        ReplacementCandidate cA, cB;
        cA.agent_id = 0;
        cA.replacement_path = makePath({0, 1, 2});
        cA.weight = 5;

        cB.agent_id = 1;
        cB.replacement_path = makePath({10, 11, 12});
        cB.weight = -2;

        std::vector<::Path> incumbent = {
            makePath({0, 11, 2}), // P_A conflicts with Q_B at loc 11, t=1
            makePath({10, 1, 12}) // P_B conflicts with Q_A at loc 1, t=1
        };

        DependencyGraph graph = PartialAcceptanceOptimizer::buildDependencyGraph({cA, cB}, incumbent, instance);
        ASSERT_EQ((int)graph.adj[0].size(), 1, "A requires B");
        ASSERT_EQ((int)graph.adj[1].size(), 1, "B requires A");

        PartialAcceptanceResult res = PartialAcceptanceOptimizer::solveExactClosure(graph, {cA, cB}, 1);
        ASSERT_EQ(res.status, PartialAcceptanceStatus::OPTIMAL_SELECTION, "Profitable cycle must be selected");
        ASSERT_EQ((int)res.selected_agent_ids.size(), 2, "Both cycle members must be selected");
        ASSERT_EQ(res.predicted_gain, 3, "Gain must be 5 - 2 = 3");
    }

    // Case 8b: w_A = +2, w_B = -5. Total = -3 <= 0 -> Reject both
    {
        ReplacementCandidate cA, cB;
        cA.agent_id = 0;
        cA.replacement_path = makePath({0, 1, 2});
        cA.weight = 2;

        cB.agent_id = 1;
        cB.replacement_path = makePath({10, 11, 12});
        cB.weight = -5;

        std::vector<::Path> incumbent = {
            makePath({0, 11, 2}),
            makePath({10, 1, 12})
        };

        DependencyGraph graph = PartialAcceptanceOptimizer::buildDependencyGraph({cA, cB}, incumbent, instance);
        PartialAcceptanceResult res = PartialAcceptanceOptimizer::solveExactClosure(graph, {cA, cB}, 1);
        ASSERT_EQ(res.status, PartialAcceptanceStatus::NO_GAIN, "Unprofitable cycle must be rejected");
        ASSERT_TRUE(res.selected_agent_ids.empty(), "No cycle members selected");
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 9: Vertex and edge-swap conflicts
// -----------------------------------------------------------------------------
bool test9_vertex_and_edge_swap_conflicts(const Instance& instance) {
    std::cout << "  [Test 9] Vertex and edge-swap conflicts: Generate correct constraints... ";
    (void)instance;

    // 9a. Vertex conflict at t=2
    ::Path pA = makePath({0, 1, 5, 9});
    ::Path pB = makePath({10, 11, 5, 13});
    std::string reason;
    ASSERT_TRUE(PartialAcceptanceOptimizer::checkTrajectoryConflict(pA, pB, &reason), "Vertex conflict at loc 5, t=2 must be detected");
    ASSERT_TRUE(reason.find("Vertex conflict at t=2 at loc=5") != std::string::npos, "Reason should mention vertex at t=2");

    // 9b. Edge swap conflict between t=1 and t=2: A moves 3->4, B moves 4->3
    ::Path pC = makePath({0, 3, 4, 8});
    ::Path pD = makePath({10, 4, 3, 12});
    ASSERT_TRUE(PartialAcceptanceOptimizer::checkTrajectoryConflict(pC, pD, &reason), "Edge swap conflict at t=1->2 must be detected");
    ASSERT_TRUE(reason.find("Edge swap conflict at t=1->2") != std::string::npos, "Reason should mention edge swap");

    // 9c. Following each other without collision: A moves 3->4, B moves 2->3
    ::Path pE = makePath({0, 3, 4, 8});
    ::Path pF = makePath({1, 2, 3, 4});
    ASSERT_FALSE(PartialAcceptanceOptimizer::checkTrajectoryConflict(pE, pF), "Trailing movement is not a conflict");

    std::cout << "PASSED" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 10: Persistent goal occupancy
// -----------------------------------------------------------------------------
bool test10_persistent_goal_occupancy(const Instance& instance) {
    std::cout << "  [Test 10] Persistent goal occupancy: Detect late conflicts after path termination... ";
    (void)instance;

    ::Path pA = makePath({0, 1, 5});
    ::Path pB = makePath({10, 11, 12, 13, 5, 20});

    std::string reason;
    ASSERT_TRUE(PartialAcceptanceOptimizer::checkTrajectoryConflict(pA, pB, &reason), "Persistent goal conflict at t=4 must be detected");
    ASSERT_TRUE(reason.find("Vertex conflict at t=4 at loc=5") != std::string::npos, "Must detect conflict during goal wait");

    std::cout << "PASSED" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 11: Mutually conflicting or duplicate replacement paths
// -----------------------------------------------------------------------------
bool test11_mutually_conflicting_replacement_paths(const Instance& instance, const std::vector<::Path>& base_paths) {
    std::cout << "  [Test 11] Mutually conflicting or duplicate replacement paths: Reject input assumptions... ";

    ReplacementCandidate c0, c1;
    c0.agent_id = 0;
    c0.replacement_path = makePath({0, 5, 10});
    c1.agent_id = 1;
    c1.replacement_path = makePath({1, 5, 11}); // both at loc 5 at t=1

    std::string err;
    ASSERT_FALSE(PartialAcceptanceOptimizer::checkPairwiseCandidateCollisions({c0, c1}, err), "Must reject conflicting replacement candidates");
    ASSERT_TRUE(err.find("Pairwise candidate conflict") != std::string::npos, "Error must describe candidate conflict");

    // Duplicate replacement candidate for the same agent (Assumption 4 violation)
    ResumableRepairEngine engine(instance);
    engine.setInitialSolution(base_paths);
    engine.setPartialAcceptanceEnabled(true);
    engine.setPartialAcceptancePolicy(PartialAcceptancePolicy::EXACT_CLOSURE);

    PartialAcceptanceResult dup_res = engine.evaluatePartialAcceptance({0, 0}, {base_paths[0], base_paths[0]});
    ASSERT_EQ(dup_res.status, PartialAcceptanceStatus::INVALID_INPUT, "Duplicate candidate agent ID must return INVALID_INPUT");

    std::cout << "PASSED" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 12: Wrong start/goal, obstacle move, or invalid incumbent
// -----------------------------------------------------------------------------
bool test12_wrong_start_goal_or_obstacle(const Instance& instance, const std::vector<::Path>& base_paths) {
    std::cout << "  [Test 12] Wrong start/goal, obstacle move, or invalid incumbent: Reject input... ";
    ResumableRepairEngine engine(instance);
    engine.setInitialSolution(base_paths);
    engine.setPartialAcceptanceEnabled(true);
    engine.setPartialAcceptancePolicy(PartialAcceptancePolicy::EXACT_CLOSURE);

    int a = 0;
    int actual_start = instance.getStarts()[a];
    int actual_goal = instance.getGoals()[a];

    // 12a. Wrong start
    ::Path p_wrong_start = base_paths[a];
    p_wrong_start[0].location = (actual_start == 0) ? 1 : 0;
    PartialAcceptanceResult res1 = engine.evaluatePartialAcceptance({a}, {p_wrong_start});
    ASSERT_EQ(res1.status, PartialAcceptanceStatus::INVALID_INPUT, "Wrong start must return INVALID_INPUT");

    // 12b. Wrong goal
    ::Path p_wrong_goal = base_paths[a];
    p_wrong_goal.back().location = (actual_goal == 0) ? 1 : 0;
    PartialAcceptanceResult res2 = engine.evaluatePartialAcceptance({a}, {p_wrong_goal});
    ASSERT_EQ(res2.status, PartialAcceptanceStatus::INVALID_INPUT, "Wrong goal must return INVALID_INPUT");

    // 12c. Invalid jump move
    if (base_paths[a].size() >= 3) {
        ::Path p_jump = base_paths[a];
        p_jump[1].location = actual_goal; // non-adjacent teleport
        if (!instance.validMove(p_jump[0].location, p_jump[1].location) ||
            !instance.validMove(p_jump[1].location, p_jump[2].location)) {
            PartialAcceptanceResult res3 = engine.evaluatePartialAcceptance({a}, {p_jump});
            ASSERT_EQ(res3.status, PartialAcceptanceStatus::INVALID_INPUT, "Invalid grid jump must return INVALID_INPUT");
        }
    }

    // 12d. Invalid incumbent (Assumption 1 violation)
    ::Path colliding_inc = base_paths[0];
    if (colliding_inc.size() > 1 && base_paths[1].size() > 1) {
        colliding_inc[1].location = base_paths[1][1].location;
    }
    engine.setIncumbentPathForTest(0, colliding_inc); // Corrupt incumbent for test
    PartialAcceptanceResult res4 = engine.evaluatePartialAcceptance({1}, {base_paths[1]});
    ASSERT_EQ(res4.status, PartialAcceptanceStatus::INVALID_INPUT, "Invalid incumbent must return INVALID_INPUT");

    std::cout << "PASSED" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 13: Stale incumbent version
// -----------------------------------------------------------------------------
bool test13_stale_incumbent_version(const Instance& instance, const std::vector<::Path>& base_paths) {
    std::cout << "  [Test 13] Stale incumbent version: Do not commit... ";
    ResumableRepairEngine engine(instance);
    engine.setInitialSolution(base_paths);
    engine.setPartialAcceptanceEnabled(true);

    uint64_t current_v = engine.getIncumbentVersion();
    uint64_t stale_v = current_v + 10; // mismatch

    PartialAcceptanceResult res;
    bool committed = engine.commitPartialAcceptance({0}, {base_paths[0]}, stale_v, res);
    ASSERT_FALSE(committed, "Commit with stale version must fail");
    ASSERT_EQ(res.status, PartialAcceptanceStatus::VERSION_CHANGED, "Status must be VERSION_CHANGED");
    ASSERT_EQ(engine.getIncumbentVersion(), current_v, "Incumbent version must remain unchanged");

    std::cout << "PASSED" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 14: Deadline reached, interrupted flow calculation, and overflow boundaries
// -----------------------------------------------------------------------------
bool test14_deadline_interruption_and_overflow(const Instance& instance, const std::vector<::Path>& base_paths) {
    std::cout << "  [Test 14] Deadline reached, interrupted flow, and checked-arithmetic overflow boundaries... ";
    ResumableRepairEngine engine(instance);
    engine.setInitialSolution(base_paths);
    engine.setPartialAcceptanceEnabled(true);
    engine.setPartialAcceptancePolicy(PartialAcceptancePolicy::EXACT_CLOSURE);

    uint64_t v_before = engine.getIncumbentVersion();
    int c_before = engine.getIncumbentCost();

    // 14a. Global deadline exceeded prior to evaluation
    {
        SearchBudget budget;
        budget.deadline_exceeded = true;
        PartialAcceptanceResult res = engine.evaluatePartialAcceptance({0}, {base_paths[0]}, &budget);
        ASSERT_EQ(res.status, PartialAcceptanceStatus::BUDGET_SKIPPED, "Status must be BUDGET_SKIPPED");
        ASSERT_EQ(engine.getIncumbentVersion(), v_before, "Incumbent version must be unchanged");
        ASSERT_EQ(engine.getIncumbentCost(), c_before, "Incumbent cost must be unchanged");
    }

    // 14b. Slice budget expired while global deadline still has plenty of time:
    // Must NOT falsely trigger BUDGET_SKIPPED!
    {
        SearchBudget slice_budget(1000, 0.000001);
        slice_budget.setGlobalDeadline(std::chrono::steady_clock::now() + std::chrono::seconds(10));
        // Even if slice time is tiny/elapsed, global deadline is +10s away
        PartialAcceptanceResult res = engine.evaluatePartialAcceptance({0}, {base_paths[0]}, &slice_budget);
        ASSERT_EQ(res.status, PartialAcceptanceStatus::NO_GAIN, "Valid slice with remaining global deadline must evaluate (NO_GAIN on same path)");
    }

    // 14c. Interrupted flow calculation mid-Dinic must abort and return BUDGET_SKIPPED, never OPTIMAL_SELECTION
    {
        ReplacementCandidate c0, c1;
        c0.agent_id = 0; c0.replacement_path = makePath({0, 1}); c0.weight = 10;
        c1.agent_id = 1; c1.replacement_path = makePath({2, 3}); c1.weight = 10;
        DependencyGraph g;
        g.num_candidates = 2;
        g.candidate_agents = {0, 1};
        g.agent_to_candidate = {{0, 0}, {1, 1}};
        g.weights = {10, 10};
        g.adj = {{1}, {}};
        g.forbidden = {false, false};

        SearchBudget step_budget;
        step_budget.setMaxFlowSteps(1); // Abort inside Dinic BFS after 1 step
        PartialAcceptanceResult flow_res = PartialAcceptanceOptimizer::solveExactClosure(g, {c0, c1}, 1, &step_budget);
        ASSERT_EQ(flow_res.status, PartialAcceptanceStatus::BUDGET_SKIPPED, "Aborted Dinic flow must return BUDGET_SKIPPED");
        ASSERT_TRUE(flow_res.selected_agent_ids.empty(), "Aborted Dinic flow must not return partial/uncertified selection");

        SearchBudget greedy_budget;
        greedy_budget.setMaxFlowSteps(0); // Abort inside Greedy
        PartialAcceptanceResult gr_res = PartialAcceptanceOptimizer::solveGreedyDependencyAware(g, {c0, c1}, 1, &greedy_budget);
        ASSERT_EQ(gr_res.status, PartialAcceptanceStatus::BUDGET_SKIPPED, "Aborted greedy selection must return BUDGET_SKIPPED");
    }

    // 14d. Checked arithmetic overflow boundaries
    {
        int64_t out = 0;
        ASSERT_FALSE(checkedAbs(std::numeric_limits<int64_t>::min(), out), "checkedAbs(INT64_MIN) must detect overflow");
        ASSERT_FALSE(checkedAdd(std::numeric_limits<int64_t>::max(), 1, out), "checkedAdd(INT64_MAX, 1) must detect overflow");
        ASSERT_FALSE(checkedSub(std::numeric_limits<int64_t>::min(), 1, out), "checkedSub(INT64_MIN, 1) must detect overflow");

        ReplacementCandidate c0, c1;
        c0.agent_id = 0; c0.replacement_path = makePath({0, 1}); c0.weight = std::numeric_limits<int64_t>::max() / 2 + 10;
        c1.agent_id = 1; c1.replacement_path = makePath({2, 3}); c1.weight = std::numeric_limits<int64_t>::max() / 2 + 10;
        DependencyGraph g;
        g.num_candidates = 2;
        g.candidate_agents = {0, 1};
        g.agent_to_candidate = {{0, 0}, {1, 1}};
        g.weights = {c0.weight, c1.weight};
        g.adj = {{1}, {}};
        g.forbidden = {false, false};

        PartialAcceptanceResult ovf_res = PartialAcceptanceOptimizer::solveExactClosure(g, {c0, c1}, 1);
        ASSERT_EQ(ovf_res.status, PartialAcceptanceStatus::INVALID_INPUT, "Overflowing weights/capacities must return INVALID_INPUT");

        // Also test INT64_MIN weight
        g.weights = {std::numeric_limits<int64_t>::min(), 5};
        PartialAcceptanceResult min_res = PartialAcceptanceOptimizer::solveExactClosure(g, {c0, c1}, 1);
        ASSERT_EQ(min_res.status, PartialAcceptanceStatus::INVALID_INPUT, "INT64_MIN weight must return INVALID_INPUT");
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 15: Rejected batch update & transactional commitment lifecycle
// -----------------------------------------------------------------------------
bool test15_rejected_batch_update(const Instance& instance, const std::vector<::Path>& base_paths) {
    std::cout << "  [Test 15] Rejected batch update & transactional commitment lifecycle... ";
    ResumableRepairEngine engine(instance);
    engine.setInitialSolution(base_paths);
    engine.setPartialAcceptanceEnabled(true);
    engine.setPartialAcceptancePolicy(PartialAcceptancePolicy::EXACT_CLOSURE);

    uint64_t v_before = engine.getIncumbentVersion();
    int cost_before = engine.getIncumbentCost();
    auto paths_before = engine.getIncumbentPaths();
    auto versions_before = engine.getAgentVersions();

    // 15a. Unconditional check: create a colliding path by copying agent 1's location at t=1 (or t=0)
    ASSERT_TRUE(base_paths.size() >= 2 && !base_paths[0].empty() && !base_paths[1].empty(), "Fixture must have at least 2 non-empty agent paths");
    {
        ::Path colliding_p = base_paths[0];
        if (colliding_p.size() > 1 && base_paths[1].size() > 1) {
            colliding_p[1].location = base_paths[1][1].location;
        } else {
            colliding_p[0].location = base_paths[1][0].location;
        }
        PartialAcceptanceResult res;
        bool committed = engine.commitPartialAcceptance({0}, {colliding_p}, engine.getIncumbentVersion(), res);
        ASSERT_FALSE(committed, "Commit with collision/kinematic violation must fail");
        ASSERT_EQ(res.status, PartialAcceptanceStatus::VALIDATION_FAILED, "Status must be VALIDATION_FAILED");
        ASSERT_EQ(engine.getIncumbentVersion(), v_before, "Version must be unchanged");
        ASSERT_EQ(engine.getIncumbentCost(), cost_before, "Cost must be unchanged");
        ASSERT_TRUE(engine.getAgentVersions() == versions_before, "Agent versions must be unchanged");
        ASSERT_TRUE(allPathsEqual(engine.getIncumbentPaths(), paths_before), "Incumbent paths must be unchanged");
    }

    // 15b. Duplicate agent in commitPartialAcceptance must fail with INVALID_INPUT
    {
        PartialAcceptanceResult res;
        bool committed = engine.commitPartialAcceptance({0, 0}, {base_paths[0], base_paths[0]}, engine.getIncumbentVersion(), res);
        ASSERT_FALSE(committed, "Commit with duplicate agent ID must fail");
        ASSERT_EQ(res.status, PartialAcceptanceStatus::INVALID_INPUT, "Status must be INVALID_INPUT");
        ASSERT_EQ(engine.getIncumbentVersion(), v_before, "Version must be unchanged");
    }

    // 15c. Late commit after deadline expiry must fail with BUDGET_SKIPPED
    {
        SearchBudget expired_budget;
        expired_budget.deadline_exceeded = true;
        PartialAcceptanceResult res;
        bool committed = engine.commitPartialAcceptance({0}, {base_paths[0]}, engine.getIncumbentVersion(), res, &expired_budget);
        ASSERT_FALSE(committed, "Commit after deadline must be rejected");
        ASSERT_EQ(res.status, PartialAcceptanceStatus::BUDGET_SKIPPED, "Status must be BUDGET_SKIPPED");
        ASSERT_EQ(engine.getIncumbentVersion(), v_before, "Version must be unchanged");
    }

    // 15d. End-to-end interrupted repair transaction with suboptimal initial path:
    // Construct a suboptimal valid incumbent by adding initial waits at start (when non-colliding) or via commitPartialAcceptance
    // Verify that when an interrupted repair commits via partial acceptance:
    // - Originating checkpoint is discarded (not saved)
    // - Other existing checkpoints remain in store subject to normal compatibility validation
    {
        // Find an agent whose start cell is never visited by any other agent at t=0..3
        int target_agent = -1;
        for (int a = 0; a < (int)base_paths.size(); ++a) {
            ::Path delayed = addInitialWaits(base_paths[a], 2);
            std::vector<::Path> test_plan = base_paths;
            test_plan[a] = delayed;
            int64_t soc = 0;
            std::string err;
            if (PartialAcceptanceOptimizer::validateCompletePlanIndependently(test_plan, instance, soc, err)) {
                target_agent = a;
                break;
            }
        }
        ASSERT_TRUE(target_agent >= 0, "Must find at least one agent that can wait 2 steps at start without collision");

        std::vector<::Path> suboptimal_incumbent = base_paths;
        suboptimal_incumbent[target_agent] = addInitialWaits(base_paths[target_agent], 2);

        ResumableRepairEngine tx_engine(instance, false, 2);
        tx_engine.setInitialSolution(suboptimal_incumbent);

        // First, create an unrelated suspended checkpoint in slot 0 (with PA disabled)
        tx_engine.setPartialAcceptanceEnabled(false);
        int other_a1 = (target_agent + 1) % base_paths.size();
        int other_a2 = (target_agent + 2) % base_paths.size();
        SearchBudget b_other;
        b_other.setMaxAgents(1); // interrupt after 1 agent
        RepairOutcome out_other = tx_engine.attemptFreshRepair({other_a1, other_a2}, b_other, 0);
        ASSERT_EQ(out_other.type, RepairOutcomeType::INTERRUPTED_SLICE, "First repair should suspend into slot 0");
        ASSERT_EQ(tx_engine.getCheckpointStore().getNumSuspended(), 1, "1 checkpoint suspended in slot 0");

        // Now enable PA and run a repair on {target_agent, other_a1} interrupted after 1 agent (target_agent)
        tx_engine.setPartialAcceptanceEnabled(true);
        tx_engine.setPartialAcceptancePolicy(PartialAcceptancePolicy::EXACT_CLOSURE);
        int cost_pre_pa = tx_engine.getIncumbentCost();
        uint64_t ver_pre_pa = tx_engine.getIncumbentVersion();

        SearchBudget b_pa;
        b_pa.setMaxAgents(1); // completes target_agent, interrupts before other_a1
        RepairOutcome out_pa = tx_engine.attemptFreshRepair({target_agent, other_a1}, b_pa, 0);

        ASSERT_EQ(out_pa.type, RepairOutcomeType::COMPLETED_ACCEPTED, "Interrupted repair with improving prefix must commit via PA");
        ASSERT_TRUE(tx_engine.getIncumbentCost() < cost_pre_pa, "Incumbent cost must strictly decrease");
        ASSERT_EQ(tx_engine.getIncumbentVersion(), ver_pre_pa + 1, "Incumbent version must increment by 1");
        ASSERT_EQ(tx_engine.getPartialAcceptanceCommitted(), 1, "PA committed counter must be 1");
        // Originating checkpoint was discarded (not saved), while the earlier checkpoint in slot 0 remains suspended
        ASSERT_EQ(tx_engine.getCheckpointStore().getNumSuspended(), 1, "Originating checkpoint discarded; prior checkpoint remains in slot 0");
        CompatibilityReport compat = tx_engine.validateCheckpoint(0, RetentionPolicy::WHOLE_CHECKPOINT_SELECTIVE);
        ASSERT_TRUE(compat.isCompatible(), "Unrelated checkpoint in slot 0 remains compatible under selective validation");
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 16: Disabled feature baseline preservation across deterministic fixed-work sequences
// -----------------------------------------------------------------------------
bool test16_disabled_feature(const Instance& instance, const std::vector<::Path>& base_paths) {
    std::cout << "  [Test 16] Disabled feature: Deterministic fixed-work baseline preservation... ";

    // Compare two engines executing an identical multi-step fixed-work schedule (fresh repairs,
    // mid-search expansion cutoffs, agent-boundary cutoffs, and checkpoint resumptions)
    ResumableRepairEngine engine_default(instance, false, 2);
    ResumableRepairEngine engine_disabled(instance, false, 2);

    engine_default.setInitialSolution(base_paths);
    engine_disabled.setInitialSolution(base_paths);
    engine_disabled.setPartialAcceptanceEnabled(false);
    engine_disabled.setPartialAcceptancePolicy(PartialAcceptancePolicy::OFF);

    PartialAcceptanceResult direct_res = engine_disabled.evaluatePartialAcceptance({0}, {base_paths[0]});
    ASSERT_EQ(direct_res.status, PartialAcceptanceStatus::DISABLED, "Direct evaluation when disabled must return DISABLED");

    std::mt19937 rng1(1337), rng2(1337);
    std::vector<int> all_agents(base_paths.size());
    for (size_t i = 0; i < base_paths.size(); ++i) all_agents[i] = (int)i;

    for (int step = 0; step < 25; ++step) {
        std::vector<int> a1 = all_agents, a2 = all_agents;
        std::shuffle(a1.begin(), a1.end(), rng1);
        std::shuffle(a2.begin(), a2.end(), rng2);
        std::vector<int> nb1(a1.begin(), a1.begin() + 4);
        std::vector<int> nb2(a2.begin(), a2.begin() + 4);
        unsigned int s1 = rng1(), s2 = rng2();
        ASSERT_EQ(s1, s2, "RNG streams must stay synchronized");

        uint64_t exp_limit = (step % 3 == 0) ? 15 : ((step % 3 == 1) ? 40 : 500);
        SearchBudget b1(exp_limit, 1e9), b2(exp_limit, 1e9);
        if (step % 4 == 2) {
            b1.setMaxAgents(2);
            b2.setMaxAgents(2);
        }

        RepairOutcome o1, o2;
        if (step % 2 == 1 && engine_default.getCheckpointStore().getCheckpoint(0) != nullptr) {
            o1 = engine_default.resumeCheckpoint(0, b1, RetentionPolicy::PARTIAL_PREFIX_RECOVERY);
            o2 = engine_disabled.resumeCheckpoint(0, b2, RetentionPolicy::PARTIAL_PREFIX_RECOVERY);
        } else {
            o1 = engine_default.attemptFreshRepair(nb1, b1, s1);
            o2 = engine_disabled.attemptFreshRepair(nb2, b2, s2);
        }

        ASSERT_EQ(o1.type, o2.type, "Outcome type must match at step " + std::to_string(step));
        ASSERT_EQ(o1.old_cost, o2.old_cost, "Old cost must match");
        ASSERT_EQ(o1.new_cost, o2.new_cost, "New cost must match");
        ASSERT_EQ(o1.cost_delta, o2.cost_delta, "Cost delta must match");
        ASSERT_EQ(o1.total_expansions, o2.total_expansions, "Expansions must match");
        ASSERT_EQ(o1.repeated_expansions, o2.repeated_expansions, "Repeated expansions must match");
        ASSERT_EQ(o1.interruption.completed_prefix_len, o2.interruption.completed_prefix_len, "Completed prefix len must match");
        ASSERT_EQ(engine_default.getIncumbentCost(), engine_disabled.getIncumbentCost(), "Incumbent cost must match");
        ASSERT_EQ(engine_default.getIncumbentVersion(), engine_disabled.getIncumbentVersion(), "Incumbent version must match");
        ASSERT_TRUE(engine_default.getAgentVersions() == engine_disabled.getAgentVersions(), "Agent versions must match");
        ASSERT_TRUE(allPathsEqual(engine_default.getIncumbentPaths(), engine_disabled.getIncumbentPaths()), "Incumbent paths must match cell-by-cell");
    }

    ASSERT_EQ(engine_default.getTotalExpansions(), engine_disabled.getTotalExpansions(), "Cumulative expansions must match");
    ASSERT_EQ(engine_default.getRepeatedExpansions(), engine_disabled.getRepeatedExpansions(), "Cumulative repeated expansions must match");
    ASSERT_EQ(engine_default.getAcceptedRepairs(), engine_disabled.getAcceptedRepairs(), "Accepted repairs must match");
    ASSERT_EQ(engine_default.getNonImprovingRepairs(), engine_disabled.getNonImprovingRepairs(), "Non-improving repairs must match");
    ASSERT_EQ(engine_default.getInterruptedSlices(), engine_disabled.getInterruptedSlices(), "Interrupted slices must match");
    ASSERT_EQ(engine_disabled.getPartialAcceptanceAttempts(), 0, "Zero PA attempts when disabled");

    std::cout << "PASSED (25 fixed-work steps verified)" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 17: Independent Path-Mixture Exhaustive Oracle Comparison (Task 2)
// -----------------------------------------------------------------------------
bool test17_independent_exhaustive_oracle_comparison(const Instance& instance, const std::vector<::Path>& base_paths) {
    std::cout << "  [Test 17] Independent path-mixture exhaustive oracle: Path-level & randomized cases... ";

    int m = instance.getDefaultNumberOfAgents();
    const auto& goals = instance.getGoals();

    // Part A: Path-level randomized trials on the actual map & scenario instance
    // Construct valid path-level incumbent and candidate sets with positive, zero, and negative weights,
    // terminal padding waits, and cross-agent dependencies, and verify against the independent path-mixture oracle.
    std::mt19937 rng(20261031);
    int path_trials = 60;
    int tie_cases_observed = 0;
    int positive_gain_cases = 0;

    for (int trial = 0; trial < path_trials; ++trial) {
        int r = 1 + (trial % 6); // r in [1, 6]
        std::vector<int> pool(m);
        for (int i = 0; i < m; ++i) pool[i] = i;
        std::shuffle(pool.begin(), pool.end(), rng);
        std::vector<int> cand_agents(pool.begin(), pool.begin() + r);

        // Start from base_paths; for some agents, inject valid initial waits into incumbent or replacement
        // so we get a mix of positive, zero, and negative candidate weights on real collision-free paths.
        std::vector<::Path> trial_incumbent = base_paths;
        std::vector<::Path> trial_replacements(r);

        for (int idx = 0; idx < r; ++idx) {
            int a = cand_agents[idx];
            int mode = (trial + idx) % 4;
            ::Path delayed1 = addInitialWaits(base_paths[a], 1);
            ::Path delayed2 = addInitialWaits(base_paths[a], 2);

            if (mode == 0) {
                // Try making incumbent delayed by 2 and replacement original (w = +2)
                std::vector<::Path> test_inc = trial_incumbent;
                test_inc[a] = delayed2;
                int64_t soc = 0; std::string err;
                if (PartialAcceptanceOptimizer::validateCompletePlanIndependently(test_inc, instance, soc, err)) {
                    trial_incumbent[a] = delayed2;
                }
                trial_replacements[idx] = (idx % 2 == 0) ? addTerminalGoalWaits(base_paths[a], 3) : base_paths[a];
            } else if (mode == 1) {
                // Zero weight (with terminal padding waits on replacement to test padding consistency)
                trial_replacements[idx] = addTerminalGoalWaits(trial_incumbent[a], 2);
            } else if (mode == 2) {
                // Try making replacement delayed by 1 (negative weight w = -1) if pairwise compatible
                trial_replacements[idx] = delayed1;
            } else {
                // Try making incumbent delayed by 1 (w = +1)
                std::vector<::Path> test_inc = trial_incumbent;
                test_inc[a] = delayed1;
                int64_t soc = 0; std::string err;
                if (PartialAcceptanceOptimizer::validateCompletePlanIndependently(test_inc, instance, soc, err)) {
                    trial_incumbent[a] = delayed1;
                }
                trial_replacements[idx] = base_paths[a];
            }
        }

        // Build ReplacementCandidate vector
        std::vector<ReplacementCandidate> candidates(r);
        for (int idx = 0; idx < r; ++idx) {
            int a = cand_agents[idx];
            candidates[idx].agent_id = a;
            candidates[idx].replacement_path = trial_replacements[idx];
            candidates[idx].old_path = trial_incumbent[a];
            candidates[idx].cost_old = PartialAcceptanceOptimizer::computeArrivalCost(trial_incumbent[a], goals[a]);
            candidates[idx].cost_new = PartialAcceptanceOptimizer::computeArrivalCost(trial_replacements[idx], goals[a]);
            candidates[idx].weight = static_cast<int64_t>(candidates[idx].cost_old) - static_cast<int64_t>(candidates[idx].cost_new);
        }

        // If random delayed replacement caused a pairwise candidate collision, verify both Exact and Oracle reject it
        std::string pair_err;
        if (!PartialAcceptanceOptimizer::checkPairwiseCandidateCollisions(candidates, pair_err)) {
            PartialAcceptanceResult oracle_res = PartialAcceptanceOptimizer::solveExhaustiveOracle(candidates, trial_incumbent, instance, 1);
            ASSERT_EQ(oracle_res.status, PartialAcceptanceStatus::INVALID_INPUT, "Oracle must reject pairwise colliding candidates");
            continue;
        }

        // Solve via DependencyGraph + Dinic Exact Closure
        DependencyGraph graph = PartialAcceptanceOptimizer::buildDependencyGraph(candidates, trial_incumbent, instance);
        PartialAcceptanceResult exact_res = PartialAcceptanceOptimizer::solveExactClosure(graph, candidates, 1);

        // Solve via Independent Path-Mixture Exhaustive Oracle (does NOT use DependencyGraph)
        PartialAcceptanceResult oracle_res = PartialAcceptanceOptimizer::solveExhaustiveOracle(candidates, trial_incumbent, instance, 1);

        ASSERT_EQ(exact_res.status, oracle_res.status,
            "Exact status must match independent path-mixture oracle in path trial " + std::to_string(trial));
        ASSERT_EQ(exact_res.predicted_gain, oracle_res.predicted_gain,
            "Exact gain must match independent path-mixture oracle gain in path trial " + std::to_string(trial));

        // Cross-check nonnegative reference when applicable
        if (graph.feasible_negative_weight_count == 0) {
            PartialAcceptanceResult nn_res = PartialAcceptanceOptimizer::solveNonnegativeClosureReference(graph, candidates, 1);
            ASSERT_EQ(nn_res.predicted_gain, oracle_res.predicted_gain,
                "Nonnegative closure reference must match independent oracle when no selectable negative weights exist");
        }

        // Independently validate the exact selector's chosen path mixture using rbl::checkPlan (independent validator)
        if (exact_res.status == PartialAcceptanceStatus::OPTIMAL_SELECTION) {
            positive_gain_cases++;
            std::vector<::Path> exact_mixture = trial_incumbent;
            for (size_t k = 0; k < exact_res.selected_agent_ids.size(); ++k) {
                exact_mixture[exact_res.selected_agent_ids[k]] = exact_res.selected_paths[k];
            }
            long long inc_soc_v = 0, mix_soc_v = 0;
            ASSERT_TRUE(verifyWithIndependentValidator(instance, trial_incumbent, inc_soc_v),
                "Trial incumbent must pass rbl::checkPlan");
            ASSERT_TRUE(verifyWithIndependentValidator(instance, exact_mixture, mix_soc_v),
                "Exact selected mixture must pass independent rbl::checkPlan");
            ASSERT_EQ(inc_soc_v - mix_soc_v, exact_res.predicted_gain,
                "Independent validator SoC reduction must equal exact predicted_gain");
        }
    }

    // Part B: Stress test 100 randomized dependency topologies (chains, cycles, forbidden nodes, ties, mixed weights)
    int graph_trials = 100;
    for (int trial = 0; trial < graph_trials; ++trial) {
        int r = 2 + (trial % 8); // r in [2, 9]
        std::vector<ReplacementCandidate> candidates(r);
        std::uniform_int_distribution<int64_t> weight_dist(-8, 12);

        DependencyGraph graph;
        graph.num_candidates = r;
        graph.candidate_agents.resize(r);
        graph.weights.resize(r);
        graph.adj.assign(r, {});
        graph.forbidden.assign(r, false);

        for (int i = 0; i < r; ++i) {
            candidates[i].agent_id = i;
            // Inject deliberate multi-optimal ties (w=0 independent options and balanced +4/-4 components)
            if (trial % 5 == 0) {
                candidates[i].weight = (i == 0) ? 6 : ((i == 1) ? 0 : -2);
            } else {
                candidates[i].weight = weight_dist(rng);
            }
            candidates[i].replacement_path = makePath({i * 10, i * 10 + 1});
            candidates[i].old_path = makePath({i * 10, i * 10 + 2});
            graph.candidate_agents[i] = i;
            graph.agent_to_candidate[i] = i;
            graph.weights[i] = candidates[i].weight;
        }

        std::uniform_real_distribution<double> prob(0.0, 1.0);
        for (int i = 0; i < r; ++i) {
            if (trial % 5 != 0 && prob(rng) < 0.18) {
                graph.forbidden[i] = true;
            }
            for (int j = 0; j < r; ++j) {
                if (trial % 5 != 0 && i != j && prob(rng) < 0.25) {
                    graph.adj[i].push_back(j);
                }
            }
        }
        PartialAcceptanceOptimizer::computeGraphTelemetry(graph);

        PartialAcceptanceResult exact_res = PartialAcceptanceOptimizer::solveExactClosure(graph, candidates, 1);
        PartialAcceptanceResult oracle_res = PartialAcceptanceOptimizer::solveExhaustiveGraphClosureOracle(graph, candidates, 1);
        PartialAcceptanceResult greedy_res = PartialAcceptanceOptimizer::solveGreedyDependencyAware(graph, candidates, 1);

        ASSERT_EQ(exact_res.predicted_gain, oracle_res.predicted_gain,
            "Exact Dinic gain must match exhaustive closure oracle in trial " + std::to_string(trial));
        ASSERT_TRUE(exact_res.predicted_gain >= greedy_res.predicted_gain,
            "Exact Dinic gain must dominate or equal greedy gain in trial " + std::to_string(trial));

        // Count how many distinct closed subsets achieve the optimal positive gain (multi-optimal solutions)
        if (oracle_res.predicted_gain > 0) {
            int optimal_subset_count = 0;
            std::vector<int> maximal_optimal_agents;
            uint64_t num_masks = 1ULL << r;
            for (uint64_t mask = 1; mask < num_masks; ++mask) {
                std::vector<int> sub;
                int64_t g = 0;
                bool closed = true;
                for (int i = 0; i < r; ++i) {
                    if ((mask >> i) & 1ULL) {
                        if (graph.forbidden[i]) { closed = false; break; }
                        for (int dep : graph.adj[i]) {
                            if (((mask >> dep) & 1ULL) == 0) { closed = false; break; }
                        }
                        if (!closed) break;
                        sub.push_back(i);
                        g += candidates[i].weight;
                    }
                }
                if (closed && g == oracle_res.predicted_gain) {
                    optimal_subset_count++;
                    maximal_optimal_agents = sub;
                }
            }
            if (optimal_subset_count > 1 && exact_res.selected_agent_ids != maximal_optimal_agents) {
                tie_cases_observed++;
            }
        }

        if (graph.feasible_negative_weight_count == 0) {
            PartialAcceptanceResult nn_res = PartialAcceptanceOptimizer::solveNonnegativeClosureReference(graph, candidates, 1);
            ASSERT_EQ(nn_res.predicted_gain, exact_res.predicted_gain,
                "Nonnegative reference must match exact gain whenever feasible_negative_weight_count == 0");
        }
    }

    ASSERT_TRUE(positive_gain_cases > 0, "Must exercise positive-gain path mixtures");
    ASSERT_TRUE(tie_cases_observed > 0, "Must exercise tied-objective subset variations");
    std::cout << "PASSED (" << path_trials << " path-mixture trials + " << graph_trials
              << " graph trials verified, " << tie_cases_observed << " multi-optimal tied subset cases verified)" << std::endl;
    return true;
}

} // namespace rbl_test

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <map_file> <scenario_file>" << std::endl;
        return 1;
    }
    std::string map_file = argv[1];
    std::string scen_file = argv[2];

    std::cout << "=================================================================" << std::endl;
    std::cout << "RouteBudget Lab - Partial Acceptance Verification Suite (17 Tests)" << std::endl;
    std::cout << "=================================================================" << std::endl;

    const int test_agents = 10;
    Instance instance(map_file, scen_file, test_agents);
    auto initial_paths = rbl_test::createInitialPaths(instance, test_agents);

    bool all_passed = true;
    all_passed &= rbl_test::test1_empty_replacement_set(instance, initial_paths);
    all_passed &= rbl_test::test2_independent_improving_alternatives(instance, initial_paths);
    all_passed &= rbl_test::test3_no_positive_feasible_gain(instance, initial_paths);
    all_passed &= rbl_test::test4_a_requires_unavailable_b(instance);
    all_passed &= rbl_test::test5_transitive_unavailable(instance);
    all_passed &= rbl_test::test6_positive_requires_negative(instance);
    all_passed &= rbl_test::test7_zero_weight_enabling_replacement(instance, initial_paths);
    all_passed &= rbl_test::test8_dependency_cycle(instance);
    all_passed &= rbl_test::test9_vertex_and_edge_swap_conflicts(instance);
    all_passed &= rbl_test::test10_persistent_goal_occupancy(instance);
    all_passed &= rbl_test::test11_mutually_conflicting_replacement_paths(instance, initial_paths);
    all_passed &= rbl_test::test12_wrong_start_goal_or_obstacle(instance, initial_paths);
    all_passed &= rbl_test::test13_stale_incumbent_version(instance, initial_paths);
    all_passed &= rbl_test::test14_deadline_interruption_and_overflow(instance, initial_paths);
    all_passed &= rbl_test::test15_rejected_batch_update(instance, initial_paths);
    all_passed &= rbl_test::test16_disabled_feature(instance, initial_paths);
    all_passed &= rbl_test::test17_independent_exhaustive_oracle_comparison(instance, initial_paths);

    std::cout << "=================================================================" << std::endl;
    if (all_passed) {
        std::cout << "ALL 17 PARTIAL ACCEPTANCE TEST GROUPS PASSED IN THIS SUITE." << std::endl;
        return 0;
    } else {
        std::cerr << "SOME TESTS FAILED!" << std::endl;
        return 1;
    }
}
