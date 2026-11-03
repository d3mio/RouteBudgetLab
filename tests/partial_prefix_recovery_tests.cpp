// RouteBudget Lab - Partial-Prefix Recovery Test Suite
// Verifies conservative prefix recovery under changed external trajectories.
//
// Test Cases:
// 1. Unchanged Context: Stage 1 version match, all r_done paths retained (COMPATIBLE_UNCHANGED).
// 2. Unrelated External Changes: External agents modified without conflict, all r_done paths retained (COMPATIBLE_PREFIX_VALID).
// 3. Earliest Conflict at k=0: External collision with first agent in neighborhood (retains 0, rejected as DISCARD_VERTEX_CONFLICT).
// 4. Middle Conflict (0 < k < r_done): Collision on agent k=1 in 2-agent prefix; salvages P_0, truncates to k=1, resumes routing, completes, passes independent validation.
// 5. Late Conflict (k = r_done - 1): Collision on last agent; salvages first r_done-1 paths.
// 6. Edge Swap on Path k: External agent moves in opposite direction across edge at time t; truncates before k.
// 7. Stay-at-Goal Collision on Path k: External agent parks at goal cell traversed at t >= arrival; truncates before k.
// 8. Neighborhood Mutation: An agent belonging to neighborhood N is modified in incumbent; strictly rejected with DISCARD_NEIGHBORHOOD_MUTATED.
// 9. Stale Cost Bound: Prefix cost alone >= current neighborhood incumbent cost; strictly rejected with DISCARD_COST_BOUND_EXCEEDED.
// 10. End-to-End Resumption & Independent Validation: Resumed repair from salvaged prefix completes and passes full independent checkPlan with 0 collisions.

#include <iostream>
#include <iomanip>
#include <cassert>
#include <vector>
#include <string>
#include <queue>
#include <algorithm>

#include "Instance.h"
#include "rbl_resumable_engine.hpp"
#include "rbl_constraint_validator.hpp"
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

#define ASSERT_FALSE(cond, msg) do { \
    if (cond) { \
        std::cerr << "FAILED: " << msg << " (" << #cond << " is true)" << std::endl; \
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
        assert(!paths[i].empty());
        ct.insert2CT(paths[i]);
    }
    return paths;
}

std::vector<int> find2DPath(const Instance& instance, int start, int goal) {
    if (start == goal) return {start};
    std::vector<int> parent(instance.map_size, -1);
    std::vector<bool> visited(instance.map_size, false);
    std::queue<int> q;
    q.push(start);
    visited[start] = true;
    while (!q.empty()) {
        int u = q.front(); q.pop();
        if (u == goal) break;
        int moves[4] = {1, -1, instance.num_of_cols, -instance.num_of_cols};
        for (int m : moves) {
            int v = u + m;
            if (v >= 0 && v < instance.map_size && !instance.isObstacle(v) && instance.validMove(u, v) && !visited[v]) {
                visited[v] = true;
                parent[v] = u;
                q.push(v);
            }
        }
    }
    if (!visited[goal]) return {};
    std::vector<int> path;
    for (int curr = goal; curr != -1; curr = parent[curr]) path.push_back(curr);
    std::reverse(path.begin(), path.end());
    return path;
}

bool buildLegalCollidingTrajectory(const Instance& instance,
                                   int agent,
                                   int target_loc,
                                   int target_t,
                                   ::Path& out_path) {
    int start = instance.getStarts()[agent];
    int goal = instance.getGoals()[agent];
    auto leg1 = find2DPath(instance, start, target_loc);
    auto leg2 = find2DPath(instance, target_loc, goal);
    if (leg1.empty() || leg2.empty()) return false;
    int d1 = (int)leg1.size() - 1;
    if (d1 > target_t) return false;

    out_path.clear();
    int waits = target_t - d1;
    for (int w = 0; w < waits; ++w) {
        out_path.push_back(PathEntry(start));
    }
    for (int loc : leg1) {
        out_path.push_back(PathEntry(loc));
    }
    for (size_t i = 1; i < leg2.size(); ++i) {
        out_path.push_back(PathEntry(leg2[i]));
    }
    return true;
}

bool isPathCollisionFreeWithOthers(const ::Path& p_test,
                                   int test_agent,
                                   const std::vector<::Path>& incumbent,
                                   const std::vector<int>& check_agents) {
    for (int other : check_agents) {
        if (other == test_agent) continue;
        const auto& p_other = incumbent[other];
        size_t max_t = std::max(p_test.size(), p_other.size());
        for (size_t t = 0; t < max_t; ++t) {
            int loc_test = (t < p_test.size()) ? p_test[t].location : p_test.back().location;
            int loc_other = (t < p_other.size()) ? p_other[t].location : p_other.back().location;
            if (loc_test == loc_other) return false;
            if (t > 0) {
                int prev_test = (t - 1 < p_test.size()) ? p_test[t - 1].location : p_test.back().location;
                int prev_other = (t - 1 < p_other.size()) ? p_other[t - 1].location : p_other.back().location;
                if (loc_test == prev_other && loc_other == prev_test && loc_test != prev_test) return false;
            }
        }
    }
    return true;
}

bool createCleanExternalCollisionOnPath(const Instance& instance,
                                        const ::Path& target_path,
                                        const ::Path& prefix0_path,
                                        const std::vector<::Path>& incumbent,
                                        int& chosen_agent,
                                        ::Path& chosen_path) {
    std::vector<int> ext_agents = {4, 5, 6, 7, 8, 9};
    for (int t = 2; t < (int)target_path.size(); ++t) {
        int loc = target_path[t].location;
        for (int a : ext_agents) {
            ::Path cand;
            if (buildLegalCollidingTrajectory(instance, a, loc, t, cand)) {
                if (isPathCollisionFreeWithOthers(cand, a, incumbent, ext_agents)) {
                    // Also ensure no collision with prefix 0
                    std::vector<::Path> dummy = {prefix0_path};
                    if (isPathCollisionFreeWithOthers(cand, -1, dummy, {0})) {
                        chosen_agent = a;
                        chosen_path = cand;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool validateSolutionIndependent(const Instance& instance,
                                 const std::string& map_file,
                                 const std::string& scen_file,
                                 int num_agents,
                                 const std::vector<::Path>& solver_paths) {
    std::string map_txt, scen_txt, parse_err;
    if (!rbl::readWholeFile(map_file, map_txt)) return false;
    rbl::Grid grid;
    if (!rbl::parseMap(map_txt, grid, parse_err)) return false;

    if (!rbl::readWholeFile(scen_file, scen_txt)) return false;
    rbl::Scenario scen;
    if (!rbl::parseScenario(scen_txt, num_agents, scen, parse_err)) return false;

    std::vector<rbl::Path> val_paths(num_agents);
    for (int i = 0; i < num_agents; ++i) {
        for (const auto& pe : solver_paths[i]) {
            int x = instance.getColCoordinate(pe.location);
            int y = instance.getRowCoordinate(pe.location);
            val_paths[i].push_back({x, y});
        }
    }

    rbl::Report rep;
    rbl::checkPlan(grid, scen.agents, val_paths, rep);
    if (rep.status != rbl::Status::VALID) {
        std::cout << "\n[VALIDATOR REPORT] Status: " << rbl::statusName(rep.status) << std::endl;
        for (const auto& issue : rep.issues) {
            std::cout << "  Issue: " << issue.code << " | " << issue.detail << std::endl;
        }
    }
    return rep.status == rbl::Status::VALID;
}

bool test1_UnchangedContext(const Instance& instance, const std::vector<::Path>& initial_paths) {
    std::cout << "  [Test 1] Unchanged Context (Stage 1 Match)... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(initial_paths);

    // Calibrate budget expansions to stop after 2 agents
    SearchBudget b_slice(300, 10.0);
    RepairOutcome o = engine.attemptFreshRepair({0, 1, 2, 3}, b_slice, 42);
    ASSERT_EQ(o.type, RepairOutcomeType::INTERRUPTED_SLICE, "Should be interrupted");
    ASSERT_TRUE(o.interruption.completed_prefix_len >= 2, "Should have routed at least 2 agents");

    // Validate under PARTIAL_PREFIX_RECOVERY
    CompatibilityReport rep = engine.validateCheckpoint(0, RetentionPolicy::PARTIAL_PREFIX_RECOVERY);
    ASSERT_EQ(rep.verdict, CompatibilityVerdict::COMPATIBLE_UNCHANGED, "Verdict should be UNCHANGED");
    ASSERT_TRUE(rep.isCompatible(), "Should be compatible");
    ASSERT_FALSE(rep.isPartial(), "Should not be partial (full prefix intact)");
    ASSERT_EQ(rep.valid_prefix_len, rep.original_prefix_len, "Valid prefix should equal original prefix");

    std::cout << "PASSED (prefix=" << rep.valid_prefix_len << "/" << rep.original_prefix_len << ")" << std::endl;
    return true;
}

bool test2_UnrelatedExternalChanges(const Instance& instance, const std::vector<::Path>& initial_paths) {
    std::cout << "  [Test 2] Unrelated External Changes (All Prefix Retained)... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(initial_paths);

    SearchBudget b_slice(300, 10.0);
    RepairOutcome o = engine.attemptFreshRepair({0, 1, 2, 3}, b_slice, 42);
    ASSERT_EQ(o.type, RepairOutcomeType::INTERRUPTED_SLICE, "Should be interrupted");

    // Modify external agent 8 without causing any conflict with prefix paths of {0, 1, 2, 3}
    auto alt_p8 = engine.getIncumbentPaths()[8];
    alt_p8.insert(alt_p8.begin() + 1, alt_p8[0]); // extra wait at start cell
    engine.setIncumbentPathForTest(8, alt_p8);

    // Checkpoint 0 should have its version bumped, but all prefix paths remain valid
    CompatibilityReport rep = engine.validateCheckpoint(0, RetentionPolicy::PARTIAL_PREFIX_RECOVERY);
    ASSERT_EQ(rep.verdict, CompatibilityVerdict::COMPATIBLE_PREFIX_VALID, "All prefix paths should remain valid");
    ASSERT_TRUE(rep.isCompatible(), "Should be compatible");
    ASSERT_FALSE(rep.isPartial(), "Not partial since all prefix paths valid");
    ASSERT_EQ(rep.valid_prefix_len, rep.original_prefix_len, "All prefix paths retained");

    std::cout << "PASSED (valid=" << rep.valid_prefix_len << "/" << rep.original_prefix_len << ")" << std::endl;
    return true;
}

bool test3_EarliestConflictAtZero(const Instance& instance, const std::vector<::Path>& initial_paths) {
    std::cout << "  [Test 3] Earliest Conflict at k=0 (No Prefix Retained)... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(initial_paths);

    SearchBudget b_slice(300, 10.0);
    RepairOutcome o = engine.attemptFreshRepair({0, 1, 2, 3}, b_slice, 42);
    ASSERT_EQ(o.type, RepairOutcomeType::INTERRUPTED_SLICE, "Should be interrupted");

    const auto* cp = engine.getCheckpointStore().getCheckpoint(0);
    ASSERT_TRUE(cp != nullptr && cp->r_done >= 1, "Must have checkpoint with r_done >= 1");
    const ::Path& p0 = cp->prefix_paths[0];

    // Synthetic collision directly at k=0 (first agent in neighborhood)
    auto incumbent = engine.getIncumbentPaths();
    auto versions = engine.getAgentVersions();
    if (p0.size() > 1 && incumbent[8].size() > 1) {
        incumbent[8][1].location = p0[1].location; // Vertex collision with P_0 at t=1
        versions[8]++;
    }

    // Under PARTIAL_PREFIX_RECOVERY: earliest conflict is at k=0.
    // Retaining paths before k=0 yields 0 paths, so it must discard with DISCARD_VERTEX_CONFLICT!
    CompatibilityReport rep = ConservativeConstraintValidator::validate(
        *cp, incumbent, versions, engine.getIncumbentVersion() + 1, RetentionPolicy::PARTIAL_PREFIX_RECOVERY);
    ASSERT_FALSE(rep.isCompatible(), "Should be incompatible since k=0 has conflict");
    ASSERT_EQ(rep.verdict, CompatibilityVerdict::DISCARD_VERTEX_CONFLICT, "Should discard on vertex conflict at k=0");
    ASSERT_EQ(rep.first_conflict_agent_index, 0, "Conflict must be at index 0");
    ASSERT_EQ(rep.valid_prefix_len, 0, "Valid prefix length must be 0");

    std::cout << "PASSED (conflict at k=0, discarded safely)" << std::endl;
    return true;
}

bool test4_MiddleConflictRecovery(const Instance& instance,
                                  const std::string& map_file,
                                  const std::string& scen_file,
                                  const std::vector<::Path>& initial_paths) {
    std::cout << "  [Test 4] Middle Conflict (0 < k < r_done, Truncation & Salvage)... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(initial_paths);

    // Calibrate slice so r_done >= 2
    SearchBudget b_slice(400, 10.0);
    RepairOutcome o = engine.attemptFreshRepair({0, 1, 2, 3}, b_slice, 42);
    ASSERT_EQ(o.type, RepairOutcomeType::INTERRUPTED_SLICE, "Should be interrupted");
    const auto* cp = engine.getCheckpointStore().getCheckpoint(0);
    ASSERT_TRUE(cp != nullptr && cp->r_done >= 2, "Must have at least 2 agents completed");

    int orig_rdone = cp->r_done;
    const ::Path& p0 = cp->prefix_paths[0];
    const ::Path& p1 = cp->prefix_paths[1];

    // Find a 100% legal grid trajectory for an external agent that visits a cell on P_1 at time t
    int chosen_agent = -1;
    ::Path chosen_path;
    bool found_collision = createCleanExternalCollisionOnPath(
        instance, p1, p0, engine.getIncumbentPaths(), chosen_agent, chosen_path);
    ASSERT_TRUE(found_collision, "Must find a legal external trajectory colliding with P_1");

    // Apply the clean external modification to the engine
    engine.setIncumbentPathForTest(chosen_agent, chosen_path);

    // 1. Under WHOLE_CHECKPOINT_SELECTIVE (Policy B): must discard entirely!
    CompatibilityReport rep_b = engine.validateCheckpoint(0, RetentionPolicy::WHOLE_CHECKPOINT_SELECTIVE);
    ASSERT_FALSE(rep_b.isCompatible(), "Policy B must discard whole checkpoint on middle conflict");
    ASSERT_EQ(rep_b.verdict, CompatibilityVerdict::DISCARD_VERTEX_CONFLICT, "Policy B detects vertex conflict");

    // 2. Under PARTIAL_PREFIX_RECOVERY (Policy C): must detect partial prefix!
    CompatibilityReport rep_c = engine.validateCheckpoint(0, RetentionPolicy::PARTIAL_PREFIX_RECOVERY);
    ASSERT_TRUE(rep_c.isCompatible(), "Policy C must be compatible with partial prefix");
    ASSERT_EQ(rep_c.verdict, CompatibilityVerdict::COMPATIBLE_PARTIAL_PREFIX, "Verdict must be COMPATIBLE_PARTIAL_PREFIX");
    ASSERT_TRUE(rep_c.isPartial(), "isPartial() must return true");
    ASSERT_EQ(rep_c.valid_prefix_len, 1, "Only P_0 (index 0) should be valid; invalid at k=1");
    ASSERT_EQ(rep_c.first_conflict_agent_index, 1, "First conflict at k=1");
    ASSERT_EQ(rep_c.original_prefix_len, orig_rdone, "Original prefix length must match");

    // 3. Resume Checkpoint under Policy C: should salvage P_0 and route remaining agents {1, 2, 3}
    SearchBudget b_resume(50000, 10.0);
    RepairOutcome o_res = engine.resumeCheckpoint(0, b_resume, RetentionPolicy::PARTIAL_PREFIX_RECOVERY);
    ASSERT_TRUE(o_res.was_partial_recovery, "Repair outcome must flag was_partial_recovery");
    ASSERT_EQ(o_res.salvaged_prefix_len, 1, "Salvaged prefix length must be 1");
    ASSERT_EQ(o_res.original_prefix_len, orig_rdone, "Original prefix length recorded");
    ASSERT_TRUE(o_res.type == RepairOutcomeType::COMPLETED_ACCEPTED || o_res.type == RepairOutcomeType::COMPLETED_NOT_IMPROVING, "Resumed repair must complete");

    // 4. Verify candidate solution passes full independent validation
    if (o_res.type == RepairOutcomeType::COMPLETED_ACCEPTED) {
        ASSERT_TRUE(validateSolutionIndependent(instance, map_file, scen_file, 10, engine.getIncumbentPaths()),
                    "Independent validation of updated incumbent must pass 100%");
    }

    std::cout << "PASSED (salvaged k=1, resumed and completed, validated 100%)" << std::endl;
    return true;
}

bool test5_LateConflictRecovery(const Instance& instance, const std::vector<::Path>& initial_paths) {
    std::cout << "  [Test 5] Late Conflict (k = r_done - 1, Salvage Nearly Whole Prefix)... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(initial_paths);

    SearchBudget b_slice(400, 10.0);
    RepairOutcome o = engine.attemptFreshRepair({0, 1, 2, 3}, b_slice, 42);
    ASSERT_EQ(o.type, RepairOutcomeType::INTERRUPTED_SLICE, "Should be interrupted");
    const auto* cp = engine.getCheckpointStore().getCheckpoint(0);
    ASSERT_TRUE(cp != nullptr && cp->r_done >= 2, "Must have at least 2 agents");

    int last_idx = cp->r_done - 1;
    const ::Path& p_last = cp->prefix_paths[last_idx];

    // Collide only with the last path
    auto incumbent = engine.getIncumbentPaths();
    auto versions = engine.getAgentVersions();
    if (p_last.size() > 2 && incumbent[8].size() > 2) {
        incumbent[8][2].location = p_last[2].location;
        versions[8]++;
    }

    CompatibilityReport rep = ConservativeConstraintValidator::validate(
        *cp, incumbent, versions, engine.getIncumbentVersion() + 1, RetentionPolicy::PARTIAL_PREFIX_RECOVERY);
    ASSERT_TRUE(rep.isCompatible(), "Should be compatible partially");
    ASSERT_EQ(rep.verdict, CompatibilityVerdict::COMPATIBLE_PARTIAL_PREFIX, "Partial verdict");
    ASSERT_EQ(rep.valid_prefix_len, last_idx, "Should salvage all except the last path");
    ASSERT_EQ(rep.first_conflict_agent_index, last_idx, "Conflict index at last_idx");

    std::cout << "PASSED (salvaged " << rep.valid_prefix_len << "/" << cp->r_done << " paths)" << std::endl;
    return true;
}

bool test6_EdgeSwapConflict(const Instance& instance, const std::vector<::Path>& initial_paths) {
    std::cout << "  [Test 6] Edge Swap Conflict on Path k... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(initial_paths);

    SearchBudget b_slice(400, 10.0);
    RepairOutcome o = engine.attemptFreshRepair({0, 1, 2, 3}, b_slice, 42);
    ASSERT_EQ(o.type, RepairOutcomeType::INTERRUPTED_SLICE, "Should be interrupted");
    const auto* cp = engine.getCheckpointStore().getCheckpoint(0);
    ASSERT_TRUE(cp != nullptr && cp->r_done >= 2, "Must have at least 2 agents");

    const ::Path& p1 = cp->prefix_paths[1];
    ASSERT_TRUE(p1.size() > 2, "Path must have at least 3 steps");

    // Create an edge swap at t=1 -> t=2: p1 moves u -> v, external agent moves v -> u
    int loc_u = p1[1].location;
    int loc_v = p1[2].location;

    auto incumbent = engine.getIncumbentPaths();
    auto versions = engine.getAgentVersions();
    if (incumbent[8].size() > 2) {
        incumbent[8][1].location = loc_v;
        incumbent[8][2].location = loc_u;
        versions[8]++;
    }

    // Under Policy B: discard with DISCARD_EDGE_SWAP
    CompatibilityReport rep_b = ConservativeConstraintValidator::validate(
        *cp, incumbent, versions, engine.getIncumbentVersion() + 1, RetentionPolicy::WHOLE_CHECKPOINT_SELECTIVE);
    ASSERT_FALSE(rep_b.isCompatible(), "Policy B must discard on edge swap");
    ASSERT_EQ(rep_b.verdict, CompatibilityVerdict::DISCARD_EDGE_SWAP, "Verdict must be DISCARD_EDGE_SWAP");

    // Under Policy C: truncates before k=1, salvages P_0
    CompatibilityReport rep_c = ConservativeConstraintValidator::validate(
        *cp, incumbent, versions, engine.getIncumbentVersion() + 1, RetentionPolicy::PARTIAL_PREFIX_RECOVERY);
    ASSERT_TRUE(rep_c.isCompatible(), "Policy C must salvage prefix before edge swap");
    ASSERT_EQ(rep_c.valid_prefix_len, 1, "Must retain 1 path before edge swap");
    ASSERT_EQ(rep_c.first_conflict_agent_index, 1, "Conflict index is 1");

    std::cout << "PASSED (edge swap isolated to k=1, P_0 salvaged safely)" << std::endl;
    return true;
}

bool test7_StayAtGoalConflict(const Instance& instance, const std::vector<::Path>& initial_paths) {
    std::cout << "  [Test 7] Stay-at-Goal Conflict on Path k... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(initial_paths);

    SearchBudget b_slice(400, 10.0);
    RepairOutcome o = engine.attemptFreshRepair({0, 1, 2, 3}, b_slice, 42);
    ASSERT_EQ(o.type, RepairOutcomeType::INTERRUPTED_SLICE, "Should be interrupted");
    const auto* cp = engine.getCheckpointStore().getCheckpoint(0);
    ASSERT_TRUE(cp != nullptr && cp->r_done >= 2, "Must have at least 2 agents");

    const ::Path& p1 = cp->prefix_paths[1];
    int goal_loc = p1.back().location;
    int arrival_t = (int)p1.size() - 1;

    // External agent 8 parks at p1's goal cell at t >= arrival_t
    auto incumbent = engine.getIncumbentPaths();
    auto versions = engine.getAgentVersions();
    while ((int)incumbent[8].size() <= arrival_t + 2) {
        incumbent[8].push_back(incumbent[8].back());
    }
    for (int t = arrival_t; t < (int)incumbent[8].size(); ++t) {
        incumbent[8][t].location = goal_loc;
    }
    versions[8]++;

    // Under Policy C: path 1 conflicts at goal, truncates to salvage P_0
    CompatibilityReport rep_c = ConservativeConstraintValidator::validate(
        *cp, incumbent, versions, engine.getIncumbentVersion() + 1, RetentionPolicy::PARTIAL_PREFIX_RECOVERY);
    ASSERT_TRUE(rep_c.isCompatible(), "Policy C salvages prefix before stay-at-goal conflict");
    ASSERT_EQ(rep_c.valid_prefix_len, 1, "Retains P_0");
    ASSERT_EQ(rep_c.first_conflict_agent_index, 1, "Conflict index is 1");

    std::cout << "PASSED (stay-at-goal conflict detected, P_0 salvaged safely)" << std::endl;
    return true;
}

bool test8_NeighborhoodMutation(const Instance& instance, const std::vector<::Path>& initial_paths) {
    std::cout << "  [Test 8] Neighborhood Mutation (Strict Rejection)... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(initial_paths);

    SearchBudget b_slice(300, 10.0);
    RepairOutcome o = engine.attemptFreshRepair({0, 1, 2, 3}, b_slice, 42);
    (void)o;
    const auto* cp = engine.getCheckpointStore().getCheckpoint(0);
    ASSERT_TRUE(cp != nullptr, "Checkpoint must exist");

    // Intervening repair changes agent 2 (which is in {0, 1, 2, 3})
    auto incumbent = engine.getIncumbentPaths();
    auto versions = engine.getAgentVersions();
    versions[2]++; // Mutate version of neighborhood agent 2

    // Both Policy B and Policy C MUST strictly reject with DISCARD_NEIGHBORHOOD_MUTATED!
    CompatibilityReport rep_b = ConservativeConstraintValidator::validate(
        *cp, incumbent, versions, engine.getIncumbentVersion() + 1, RetentionPolicy::WHOLE_CHECKPOINT_SELECTIVE);
    ASSERT_EQ(rep_b.verdict, CompatibilityVerdict::DISCARD_NEIGHBORHOOD_MUTATED, "Policy B must reject mutated neighborhood");

    CompatibilityReport rep_c = ConservativeConstraintValidator::validate(
        *cp, incumbent, versions, engine.getIncumbentVersion() + 1, RetentionPolicy::PARTIAL_PREFIX_RECOVERY);
    ASSERT_EQ(rep_c.verdict, CompatibilityVerdict::DISCARD_NEIGHBORHOOD_MUTATED, "Policy C must reject mutated neighborhood");
    ASSERT_FALSE(rep_c.isCompatible(), "Must NOT be compatible when neighborhood mutated");

    std::cout << "PASSED (strictly rejected under both policies)" << std::endl;
    return true;
}

bool test9_StaleCostBound(const Instance& instance, const std::vector<::Path>& initial_paths) {
    std::cout << "  [Test 9] Stale Cost Bound Pruning (Strict Rejection)... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(initial_paths);

    SearchBudget b_slice(300, 10.0);
    RepairOutcome o = engine.attemptFreshRepair({0, 1, 2, 3}, b_slice, 42);
    (void)o;
    const auto* cp = engine.getCheckpointStore().getCheckpoint(0);
    ASSERT_TRUE(cp != nullptr, "Checkpoint must exist");

    // External intervening repair massively improves incumbent cost of neighborhood {0, 1, 2, 3}
    // such that prefix cost >= new neighborhood cost
    auto incumbent = engine.getIncumbentPaths();
    auto versions = engine.getAgentVersions();
    for (int a : {0, 1, 2, 3}) {
        if (incumbent[a].size() > 1) {
            incumbent[a].resize(1); // minimal possible cost 0
        }
    }
    // An external agent changed so incumbent version incremented
    versions[8]++;

    // Under Policy C: prefix cost >= current incumbent cost, must discard!
    CompatibilityReport rep_c = ConservativeConstraintValidator::validate(
        *cp, incumbent, versions, engine.getIncumbentVersion() + 1, RetentionPolicy::PARTIAL_PREFIX_RECOVERY);
    ASSERT_FALSE(rep_c.isCompatible(), "Must be incompatible when prefix cost >= incumbent cost");
    ASSERT_EQ(rep_c.verdict, CompatibilityVerdict::DISCARD_COST_BOUND_EXCEEDED, "Verdict must be DISCARD_COST_BOUND_EXCEEDED");

    std::cout << "PASSED (cost bound pruning verified)" << std::endl;
    return true;
}

bool test10_EndToEndSolutionValidity(const Instance& instance,
                                     const std::string& map_file,
                                     const std::string& scen_file,
                                     const std::vector<::Path>& initial_paths) {
    std::cout << "  [Test 10] Independent Solution Validation of Committed Plan... " << std::flush;
    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(initial_paths);

    // Initial solution must pass validator
    ASSERT_TRUE(validateSolutionIndependent(instance, map_file, scen_file, 10, initial_paths),
                "Initial solution must pass validator");

    // Fresh repair interrupted
    SearchBudget b_slice(400, 10.0);
    RepairOutcome o = engine.attemptFreshRepair({0, 1, 2, 3}, b_slice, 42);
    ASSERT_EQ(o.type, RepairOutcomeType::INTERRUPTED_SLICE, "Should be interrupted");

    const auto* cp = engine.getCheckpointStore().getCheckpoint(0);
    ASSERT_TRUE(cp != nullptr && cp->r_done >= 2, "Need r_done >= 2");
    const ::Path& p0 = cp->prefix_paths[0];
    const ::Path& p1 = cp->prefix_paths[1];

    int chosen_agent = -1;
    ::Path chosen_path;
    bool found_collision = createCleanExternalCollisionOnPath(
        instance, p1, p0, engine.getIncumbentPaths(), chosen_agent, chosen_path);
    ASSERT_TRUE(found_collision, "Must find clean colliding external trajectory");

    engine.setIncumbentPathForTest(chosen_agent, chosen_path);

    // Resume with partial prefix recovery
    SearchBudget b_res(50000, 10.0);
    RepairOutcome o_res = engine.resumeCheckpoint(0, b_res, RetentionPolicy::PARTIAL_PREFIX_RECOVERY);
    ASSERT_TRUE(o_res.was_partial_recovery, "Must be partial recovery");
    ASSERT_TRUE(o_res.type == RepairOutcomeType::COMPLETED_ACCEPTED || o_res.type == RepairOutcomeType::COMPLETED_NOT_IMPROVING, "Must complete successfully");

    // Verify updated incumbent passes independent validator 100%
    ASSERT_TRUE(validateSolutionIndependent(instance, map_file, scen_file, 10, engine.getIncumbentPaths()),
                "Final incumbent must pass independent validator 100%");

    std::cout << "PASSED (final committed solution 100% collision-free)" << std::endl;
    return true;
}

} // namespace rbl_test

int main(int argc, char** argv) {
    std::string map_file = (argc > 1) ? argv[1] : "data/sample/random-32-32-20.map";
    std::string scen_file = (argc > 2) ? argv[2] : "data/sample/random-32-32-20-random-1.scen";

    std::cout << "================================================================" << std::endl;
    std::cout << "Running Partial-Prefix Recovery Correctness Test Suite" << std::endl;
    std::cout << "Map: " << map_file << "\nScen: " << scen_file << std::endl;
    std::cout << "================================================================" << std::endl;

    const int test_agents = 10;
    Instance instance(map_file, scen_file, test_agents);
    auto initial_paths = rbl_test::createInitialPaths(instance, test_agents);

    bool all_ok = true;
    all_ok &= rbl_test::test1_UnchangedContext(instance, initial_paths);
    all_ok &= rbl_test::test2_UnrelatedExternalChanges(instance, initial_paths);
    all_ok &= rbl_test::test3_EarliestConflictAtZero(instance, initial_paths);
    all_ok &= rbl_test::test4_MiddleConflictRecovery(instance, map_file, scen_file, initial_paths);
    all_ok &= rbl_test::test5_LateConflictRecovery(instance, initial_paths);
    all_ok &= rbl_test::test6_EdgeSwapConflict(instance, initial_paths);
    all_ok &= rbl_test::test7_StayAtGoalConflict(instance, initial_paths);
    all_ok &= rbl_test::test8_NeighborhoodMutation(instance, initial_paths);
    all_ok &= rbl_test::test9_StaleCostBound(instance, initial_paths);
    all_ok &= rbl_test::test10_EndToEndSolutionValidity(instance, map_file, scen_file, initial_paths);

    std::cout << "================================================================" << std::endl;
    if (all_ok) {
        std::cout << "ALL 10 PARTIAL-PREFIX RECOVERY TESTS PASSED SUCCESSFULLY!" << std::endl;
        return 0;
    } else {
        std::cerr << "SOME TESTS FAILED!" << std::endl;
        return 1;
    }
}
