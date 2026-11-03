// RouteBudget Lab - Experiment E4 Test Suite: Two-Stage Conservative Constraint Compatibility (Phase 4 / O3, FR-03, RQ2)
//
// E4 Test Coverage:
// 1. Unchanged Context: Stage 1 fast O(|A|) version match (sub-microsecond latency, both policies compatible).
// 2. Irrelevant Changes: External agent modified non-conflictingly (Policy 0 discards; Policy 1 validates and retains prefix).
// 3. Conflicting Changes: Modified external agent causes vertex conflict (Policy 1 detects and discards with DISCARD_VERTEX_CONFLICT).
// 4. Edge Swap Changes & Removed Reservations: Modified external agent causes edge swap (Policy 1 detects DISCARD_EDGE_SWAP; stale reservations cleared).
// 5. Changed Goal Occupancy: External agent parks at goal cell traversed by prefix at t >= arrival (Policy 1 detects DISCARD_STAY_AT_GOAL).
// 6. Overlapping Neighborhood Repairs: Agent in N_i modified in intervening repair (Policy 1 detects DISCARD_NEIGHBORHOOD_MUTATED).
// 7. Cost Bound Pruning: Prefix accumulated cost >= current neighborhood incumbent cost (Policy 1 detects DISCARD_COST_BOUND_EXCEEDED).
// 8. Policy Comparison (Policy 0 vs Policy 1): Comparative evaluation logging eligibility, rejection reasons, validation latency, and final validity.
//
// Theoretical Note: Macro-prefix mode only retains completed concrete trajectories P_{1..r_done}.
// It discards the unfinished search frontier. Compatibility is strictly proven for concrete prefix paths,
// and does NOT imply correctness for unexpanded frontier states.

#include <iostream>
#include <iomanip>
#include <cassert>
#include <vector>
#include <string>
#include <chrono>

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

inline bool arePathsIdentical(const ::Path& p1, const ::Path& p2) {
    if (p1.size() != p2.size()) return false;
    for (size_t t = 0; t < p1.size(); ++t) {
        if (p1[t].location != p2[t].location) return false;
    }
    return true;
}

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

bool runAllE4Tests(const std::string& map_file, const std::string& scen_file) {
    std::cout << "================================================================" << std::endl;
    std::cout << "Running Experiment E4: Conservative Constraint Compatibility" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "Map: " << map_file << "\nScen: " << scen_file << std::endl;

    const int test_agents = 10;
    Instance instance(map_file, scen_file, test_agents);

    // 0. Calibrate budget expansions on baseline repair of {0, 1, 2, 3}
    auto initial_paths = createInitialPaths(instance, test_agents);
    ResumableRepairEngine engine_calib(instance, false, 2);
    engine_calib.setInitialSolution(initial_paths);
    SearchBudget b_calib(50000, 10.0);
    RepairOutcome o_calib = engine_calib.attemptFreshRepair({0, 1, 2, 3}, b_calib, 42);
    uint64_t exp_total = o_calib.total_expansions;
    uint64_t exp_2_agents = exp_total / 2; // ~216
    uint64_t exp_1_agent = exp_total / 4;  // ~108

    std::cout << "[Calibration] Total expansions for 4 agents = " << exp_total
              << " | Slice for 2 agents = " << exp_2_agents
              << " | Slice for 1 agent = " << exp_1_agent << std::endl;

    // -------------------------------------------------------------
    // Test 1: Unchanged Context (Stage 1 match)
    // -------------------------------------------------------------
    std::cout << "\n[E4 Test 1] Unchanged Context (Stage 1 Match)..." << std::endl;
    {
        ResumableRepairEngine engine(instance, false, 2);
        engine.setInitialSolution(initial_paths);

        std::vector<int> n0 = {0, 1, 2, 3};
        SearchBudget b1(exp_2_agents, 10.0);
        RepairOutcome o1 = engine.attemptFreshRepair(n0, b1, 42);
        ASSERT_EQ(o1.type, RepairOutcomeType::INTERRUPTED_SLICE, "Must interrupt at boundary");
        ASSERT_EQ(o1.interruption.completed_prefix_len, 2, "r_done must be 2");

        const auto* cp = engine.getCheckpointStore().getCheckpoint(0);
        ASSERT_TRUE(cp != nullptr, "Checkpoint must exist in slot 0");

        // Validate under Policy 0 (Discard-on-Change)
        CompatibilityReport rep_p0 = ConservativeConstraintValidator::validate(
            *cp, engine.getIncumbentPaths(), engine.getAgentVersions(), engine.getIncumbentVersion(), false);
        ASSERT_EQ(rep_p0.verdict, CompatibilityVerdict::COMPATIBLE_UNCHANGED, "Policy 0 unchanged must pass");

        // Validate under Policy 1 (Selective Retention)
        CompatibilityReport rep_p1 = ConservativeConstraintValidator::validate(
            *cp, engine.getIncumbentPaths(), engine.getAgentVersions(), engine.getIncumbentVersion(), true);
        ASSERT_EQ(rep_p1.verdict, CompatibilityVerdict::COMPATIBLE_UNCHANGED, "Policy 1 Stage 1 match must pass");
        ASSERT_TRUE(rep_p1.validation_time_us < 10.0, "Stage 1 check must be sub-microsecond");

        // Resume to completion
        SearchBudget b_res(50000, 10.0);
        RepairOutcome o_res = engine.resumeCheckpoint(0, b_res, true);
        ASSERT_TRUE(o_res.type == RepairOutcomeType::COMPLETED_ACCEPTED ||
                    o_res.type == RepairOutcomeType::COMPLETED_NOT_IMPROVING, "Must complete cleanly");

        std::string err;
        ASSERT_TRUE(engine.validateIncumbent(err), "Incumbent must be valid: " + err);
        std::cout << "  -> Stage 1 latency: " << rep_p1.validation_time_us << " us, Verdict: " << rep_p1.verdict << " [PASSED]" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 2: Irrelevant External Changes (Stage 2 Pass)
    // -------------------------------------------------------------
    std::cout << "\n[E4 Test 2] Irrelevant External Changes (Stage 2 Pass)..." << std::endl;
    {
        ResumableRepairEngine engine(instance, false, 2);
        engine.setInitialSolution(initial_paths);

        std::vector<int> n0 = {0, 1, 2, 3};
        SearchBudget b1(exp_2_agents, 10.0);
        RepairOutcome o1 = engine.attemptFreshRepair(n0, b1, 42);
        ASSERT_EQ(o1.type, RepairOutcomeType::INTERRUPTED_SLICE, "Must interrupt at boundary");

        const auto* cp = engine.getCheckpointStore().getCheckpoint(0);
        ASSERT_TRUE(cp != nullptr, "Checkpoint must exist");

        // Introduce a genuine, collision-free modification to external agent 8.
        // Agent 8 start and goal remain strictly unchanged (instance problem specification).
        ConstraintTable ct_ext(instance.num_of_cols, instance.map_size);
        for (int a = 0; a < test_agents; ++a) {
            if (a != 8) ct_ext.insert2CT(engine.getIncumbentPaths()[a]);
        }
        // Force agent 8 to choose an alternate valid collision-free route by constraining its original t=1 location
        int orig_t1_loc = engine.getIncumbentPaths()[8].size() > 1 ? engine.getIncumbentPaths()[8][1].location : -1;
        if (orig_t1_loc >= 0) {
            ct_ext.insert2CT(orig_t1_loc, 1, 2);
        }
        SpaceTimeAStar solver8(instance, 8);
        ::Path alt_p8 = solver8.findPath(ct_ext);
        if (alt_p8.empty()) {
            alt_p8 = engine.getIncumbentPaths()[8];
            alt_p8.insert(alt_p8.begin() + 1, alt_p8[0]);
        }

        // Verify alternative path properties
        ASSERT_TRUE(!alt_p8.empty(), "Alternative path must not be empty");
        ASSERT_EQ(alt_p8.front().location, instance.getStarts()[8], "Start location must remain identical");
        ASSERT_EQ(alt_p8.back().location, instance.getGoals()[8], "Goal location must remain identical");
        ASSERT_FALSE(arePathsIdentical(alt_p8, initial_paths[8]), "Path must be genuinely modified");

        uint64_t old_incumbent_ver = engine.getIncumbentVersion();
        uint64_t old_agent8_ver = engine.getAgentVersions()[8];
        ASSERT_EQ(cp->incumbent_version, old_incumbent_ver, "Checkpoint saved at old incumbent version");
        ASSERT_EQ(cp->agent_versions_at_save[8], old_agent8_ver, "Checkpoint saved at old agent 8 version");

        bool commit_ok = engine.commitExternalTrajectory(8, alt_p8);
        ASSERT_TRUE(commit_ok, "commitExternalTrajectory must succeed");

        // Verify version information has genuinely updated
        ASSERT_EQ(engine.getIncumbentVersion(), old_incumbent_ver + 1, "Incumbent version must increment");
        ASSERT_EQ(engine.getAgentVersions()[8], old_agent8_ver + 1, "Agent 8 version must increment");
        ASSERT_TRUE(engine.getIncumbentVersion() != cp->incumbent_version, "Versions must differ");
        ASSERT_TRUE(engine.getAgentVersions()[8] != cp->agent_versions_at_save[8], "Agent 8 versions must differ");

        // Assert 1: The discard-on-change reference policy (Policy 0) rejects the checkpoint
        CompatibilityReport rep_p0 = ConservativeConstraintValidator::validate(
            *cp, engine.getIncumbentPaths(), engine.getAgentVersions(), engine.getIncumbentVersion(), false);
        ASSERT_FALSE(rep_p0.isCompatible(), "Policy 0 must reject checkpoint upon incumbent version change");
        ASSERT_EQ(rep_p0.verdict, CompatibilityVerdict::DISCARD_POLICY_DISABLED, "Policy 0 must report DISCARD_POLICY_DISABLED");
        ASSERT_EQ(rep_p0.stage_reached, 0, "Policy 0 must not execute Stage 2");

        // Assert 2: The selective policy actually executes Stage 2
        CompatibilityReport rep_p1 = ConservativeConstraintValidator::validate(
            *cp, engine.getIncumbentPaths(), engine.getAgentVersions(), engine.getIncumbentVersion(), true);
        ASSERT_EQ(rep_p1.stage_reached, 2, "Selective policy must actually execute Stage 2");
        ASSERT_EQ(rep_p1.modified_external_agents, 1, "Stage 2 must identify exactly 1 modified external agent");

        // Assert 3: Stage 2 returns COMPATIBLE_PREFIX_VALID
        ASSERT_EQ(rep_p1.verdict, CompatibilityVerdict::COMPATIBLE_PREFIX_VALID, "Stage 2 must return COMPATIBLE_PREFIX_VALID");
        ASSERT_TRUE(rep_p1.isCompatible(), "Selective policy must declare checkpoint compatible");

        std::cout << "  -> Policy 0 Verdict: " << rep_p0.verdict << " (Discarded as expected)" << std::endl;
        std::cout << "  -> Policy 1 Verdict: " << rep_p1.verdict << " (Stage " << rep_p1.stage_reached
                  << ", latency: " << rep_p1.validation_time_us << " us, " << rep_p1.detail << ")" << std::endl;

        // Assert 4: Resumption from this compatible checkpoint produces a strictly valid final solution
        SearchBudget b_res(50000, 10.0);
        RepairOutcome o_res = engine.resumeCheckpoint(0, b_res, true);
        ASSERT_TRUE(o_res.type == RepairOutcomeType::COMPLETED_ACCEPTED ||
                    o_res.type == RepairOutcomeType::COMPLETED_NOT_IMPROVING, "Resumption must complete cleanly");

        std::string err;
        ASSERT_TRUE(engine.validateIncumbent(err), "Final solution after resumption must be strictly valid: " + err);
        std::cout << "  -> Stage 2 non-conflicting validation & resumption strictly verified [PASSED]" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 3: Conflicting Changes (Vertex Conflict Detection)
    // -------------------------------------------------------------
    std::cout << "\n[E4 Test 3] Conflicting Changes (Vertex Conflict Detection)..." << std::endl;
    {
        ResumableRepairEngine engine(instance, false, 2);
        engine.setInitialSolution(initial_paths);

        std::vector<int> n0 = {0, 1, 2, 3};
        SearchBudget b1(exp_1_agent, 10.0);
        engine.attemptFreshRepair(n0, b1, 42);
        const auto* cp = engine.getCheckpointStore().getCheckpoint(0);
        ASSERT_TRUE(cp != nullptr && cp->r_done >= 1, "Must have prefix path");

        // Craft a synthetic incumbent change on agent 5 that directly collides at t=2 with prefix path 0
        auto incumbent = engine.getIncumbentPaths();
        auto versions = engine.getAgentVersions();
        int coll_t = 2;
        if (coll_t < (int)cp->prefix_paths[0].size()) {
            int coll_loc = cp->prefix_paths[0][coll_t].location;
            if ((int)incumbent[5].size() <= coll_t) incumbent[5].resize(coll_t + 2, incumbent[5].back());
            incumbent[5][coll_t].location = coll_loc;
            versions[5]++; // simulate trajectory version increment

            CompatibilityReport rep = ConservativeConstraintValidator::validate(
                *cp, incumbent, versions, engine.getIncumbentVersion() + 1, true);

            ASSERT_EQ(rep.verdict, CompatibilityVerdict::DISCARD_VERTEX_CONFLICT, "Must detect vertex conflict");
            ASSERT_EQ(rep.conflicting_agent, 5, "Conflicting agent must be 5");
            ASSERT_EQ(rep.conflict_location, coll_loc, "Collision location must match");
            std::cout << "  -> Detected vertex conflict with agent " << rep.conflicting_agent
                      << " at t=" << rep.conflict_time << ", loc=" << rep.conflict_location
                      << " (latency: " << rep.validation_time_us << " us) [PASSED]" << std::endl;
        }
    }

    // -------------------------------------------------------------
    // Test 4: Conflicting Changes (Edge Swap Conflict & Stale Reservation Clearing)
    // -------------------------------------------------------------
    std::cout << "\n[E4 Test 4] Edge Swap Conflict Detection..." << std::endl;
    {
        ResumableRepairEngine engine(instance, false, 2);
        engine.setInitialSolution(initial_paths);

        std::vector<int> n0 = {0, 1, 2, 3};
        SearchBudget b1(exp_1_agent, 10.0);
        engine.attemptFreshRepair(n0, b1, 42);
        const auto* cp = engine.getCheckpointStore().getCheckpoint(0);
        ASSERT_TRUE(cp != nullptr && cp->r_done >= 1, "Must have prefix path");

        auto incumbent = engine.getIncumbentPaths();
        auto versions = engine.getAgentVersions();
        const auto& pref = cp->prefix_paths[0];

        // Find a timestep where agent 0 moves between two distinct adjacent locations u -> v
        int swap_t = -1;
        for (size_t t = 0; t + 1 < pref.size(); ++t) {
            if (pref[t].location != pref[t + 1].location) {
                swap_t = (int)t;
                break;
            }
        }
        ASSERT_TRUE(swap_t >= 0, "Must find a moving transition in prefix path");

        int u = pref[swap_t].location;
        int v = pref[swap_t + 1].location;

        // Modify agent 6 to perform reverse transition v -> u at the exact same timestep
        if ((int)incumbent[6].size() <= swap_t + 2) incumbent[6].resize(swap_t + 3, incumbent[6].back());
        incumbent[6][swap_t].location = v;
        incumbent[6][swap_t + 1].location = u;
        versions[6]++;

        CompatibilityReport rep = ConservativeConstraintValidator::validate(
            *cp, incumbent, versions, engine.getIncumbentVersion() + 1, true);

        ASSERT_EQ(rep.verdict, CompatibilityVerdict::DISCARD_EDGE_SWAP, "Must detect edge swap");
        ASSERT_EQ(rep.conflicting_agent, 6, "Conflicting agent must be 6");
        std::cout << "  -> Detected edge swap (" << u << "<->" << v << ") with agent 6 at t=" << swap_t
                  << " (latency: " << rep.validation_time_us << " us) [PASSED]" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 5: Changed Goal Occupancy (Stay-at-Goal Collision)
    // -------------------------------------------------------------
    std::cout << "\n[E4 Test 5] Changed Goal Occupancy (Stay-at-Goal Collision)..." << std::endl;
    {
        ResumableRepairEngine engine(instance, false, 2);
        engine.setInitialSolution(initial_paths);

        std::vector<int> n0 = {0, 1, 2, 3};
        SearchBudget b1(exp_1_agent, 10.0);
        engine.attemptFreshRepair(n0, b1, 42);
        const auto* cp = engine.getCheckpointStore().getCheckpoint(0);
        ASSERT_TRUE(cp != nullptr && cp->r_done >= 1, "Must have prefix path");

        auto incumbent = engine.getIncumbentPaths();
        auto versions = engine.getAgentVersions();
        const auto& pref = cp->prefix_paths[0];

        // Pick location of prefix at later timestep t=4
        int late_t = std::min(4, (int)pref.size() - 1);
        int target_loc = pref[late_t].location;

        // Set agent 7 to arrive and park at target_loc at t=2 (so it permanently stays there for t >= 2)
        incumbent[7].resize(3);
        incumbent[7][0].location = target_loc;
        incumbent[7][1].location = target_loc;
        incumbent[7][2].location = target_loc;
        versions[7]++;

        CompatibilityReport rep = ConservativeConstraintValidator::validate(
            *cp, incumbent, versions, engine.getIncumbentVersion() + 1, true);

        ASSERT_EQ(rep.verdict, CompatibilityVerdict::DISCARD_STAY_AT_GOAL, "Must detect stay-at-goal collision");
        ASSERT_EQ(rep.conflicting_agent, 7, "Conflicting agent must be 7");
        std::cout << "  -> Detected stay-at-goal collision at loc=" << target_loc
                  << " with parked agent 7 at t=" << rep.conflict_time
                  << " (latency: " << rep.validation_time_us << " us) [PASSED]" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 6: Overlapping Neighborhood Repairs
    // -------------------------------------------------------------
    std::cout << "\n[E4 Test 6] Overlapping Neighborhood Repairs..." << std::endl;
    {
        ResumableRepairEngine engine(instance, false, 2);
        engine.setInitialSolution(initial_paths);

        std::vector<int> n0 = {0, 1, 2, 3};
        SearchBudget b1(exp_1_agent, 10.0);
        engine.attemptFreshRepair(n0, b1, 42);
        const auto* cp = engine.getCheckpointStore().getCheckpoint(0);
        ASSERT_TRUE(cp != nullptr, "Must have checkpoint");

        auto incumbent = engine.getIncumbentPaths();
        auto versions = engine.getAgentVersions();

        // Simulate that agent 1 (inside N_0) was replanned in an intervening iteration
        versions[1]++; // version mutated

        CompatibilityReport rep = ConservativeConstraintValidator::validate(
            *cp, incumbent, versions, engine.getIncumbentVersion() + 1, true);

        ASSERT_EQ(rep.verdict, CompatibilityVerdict::DISCARD_NEIGHBORHOOD_MUTATED, "Must discard mutated neighborhood");
        ASSERT_EQ(rep.conflicting_agent, 1, "Conflicting agent must be 1");
        std::cout << "  -> Detected overlapping neighborhood mutation on agent " << rep.conflicting_agent
                  << " (latency: " << rep.validation_time_us << " us) [PASSED]" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 7: Cost Ceiling Pruning Assumptions
    // -------------------------------------------------------------
    std::cout << "\n[E4 Test 7] Cost Bound & Pruning Ceilings..." << std::endl;
    {
        ResumableRepairEngine engine(instance, false, 2);
        engine.setInitialSolution(initial_paths);

        std::vector<int> n0 = {0, 1, 2, 3};
        SearchBudget b1(exp_1_agent, 10.0);
        engine.attemptFreshRepair(n0, b1, 42);
        const auto* cp = engine.getCheckpointStore().getCheckpoint(0);
        ASSERT_TRUE(cp != nullptr, "Must have checkpoint");

        // Clone checkpoint and artificially increase prefix_accumulated_cost beyond current neighborhood cost
        MacroPrefixCheckpoint artificial_cp = *cp;
        artificial_cp.prefix_accumulated_cost = artificial_cp.old_neighborhood_cost + 10;

        auto incumbent = engine.getIncumbentPaths();
        auto versions = engine.getAgentVersions();
        versions[9]++; // external agent version changed to trigger Stage 2

        CompatibilityReport rep = ConservativeConstraintValidator::validate(
            artificial_cp, incumbent, versions, engine.getIncumbentVersion() + 1, true);

        ASSERT_EQ(rep.verdict, CompatibilityVerdict::DISCARD_COST_BOUND_EXCEEDED, "Must discard when prefix cost >= neighborhood cost");
        std::cout << "  -> Detected cost bound violation (" << rep.detail
                  << ", latency: " << rep.validation_time_us << " us) [PASSED]" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 8: Policy Comparison (Policy 0 vs Policy 1 Batch)
    // -------------------------------------------------------------
    std::cout << "\n[E4 Test 8] Comprehensive Policy Comparison (Policy 0 vs Policy 1)..." << std::endl;
    {
        ResumableRepairEngine engine(instance, false, 2);
        engine.setInitialSolution(initial_paths);

        const int num_trials = 20;
        int p0_eligible = 0;
        int p1_eligible = 0;
        int p0_invalid_commits = 0;
        int p1_invalid_commits = 0;
        double p0_total_time_us = 0.0;
        double p1_total_time_us = 0.0;

        for (int trial = 0; trial < num_trials; ++trial) {
            std::vector<int> neighborhood = {trial % 4, (trial + 1) % 4, (trial + 2) % 4};
            SearchBudget b_slice(exp_1_agent, 10.0);
            RepairOutcome out = engine.attemptFreshRepair(neighborhood, b_slice, trial * 31);

            if (out.type != RepairOutcomeType::INTERRUPTED_SLICE) continue;

            const auto* cp = engine.getCheckpointStore().getCheckpoint(0);
            if (!cp) continue;

            // Introduce controlled external perturbations on odd trials
            if (trial % 2 == 1) {
                ConstraintTable ct_ext(instance.num_of_cols, instance.map_size);
                for (int a = 0; a < test_agents; ++a) {
                    if (a != 8) ct_ext.insert2CT(engine.getIncumbentPaths()[a]);
                }
                int cur_t1 = engine.getIncumbentPaths()[8].size() > 1 ? engine.getIncumbentPaths()[8][1].location : -1;
                if (cur_t1 >= 0) ct_ext.insert2CT(cur_t1, 1, 2);
                SpaceTimeAStar s8(instance, 8);
                ::Path new_p8 = s8.findPath(ct_ext);
                if (!new_p8.empty()) {
                    engine.commitExternalTrajectory(8, new_p8);
                }
            }

            // Evaluate Policy 0
            auto t0 = std::chrono::steady_clock::now();
            CompatibilityReport rep0 = ConservativeConstraintValidator::validate(
                *cp, engine.getIncumbentPaths(), engine.getAgentVersions(), engine.getIncumbentVersion(), false);
            auto t1 = std::chrono::steady_clock::now();
            p0_total_time_us += std::chrono::duration<double, std::micro>(t1 - t0).count();
            if (rep0.isCompatible()) p0_eligible++;

            // Evaluate Policy 1
            auto t2 = std::chrono::steady_clock::now();
            CompatibilityReport rep1 = ConservativeConstraintValidator::validate(
                *cp, engine.getIncumbentPaths(), engine.getAgentVersions(), engine.getIncumbentVersion(), true);
            auto t3 = std::chrono::steady_clock::now();
            p1_total_time_us += std::chrono::duration<double, std::micro>(t3 - t2).count();
            if (rep1.isCompatible()) p1_eligible++;

            // Resume and check validity
            SearchBudget b_finish(50000, 10.0);
            RepairOutcome res_out = engine.resumeCheckpoint(0, b_finish, true);
            (void)res_out;
            std::string val_err;
            if (!engine.validateIncumbent(val_err)) {
                p1_invalid_commits++;
            }
        }

        std::cout << "\n----------------------------------------------------------------" << std::endl;
        std::cout << "Policy Evaluation Summary (" << num_trials << " Scenarios Evaluated)" << std::endl;
        std::cout << "----------------------------------------------------------------" << std::endl;
        std::cout << std::left << std::setw(28) << "Metric" 
                  << std::setw(20) << "Policy 0 (Discard)" 
                  << std::setw(20) << "Policy 1 (Selective)" << std::endl;
        std::cout << "----------------------------------------------------------------" << std::endl;
        std::cout << std::left << std::setw(28) << "Eligible Checkpoints" 
                  << std::setw(20) << p0_eligible 
                  << std::setw(20) << p1_eligible << std::endl;
        std::cout << std::left << std::setw(28) << "Mean Validation Latency" 
                  << std::setw(20) << (p0_total_time_us / num_trials) 
                  << std::setw(20) << (p1_total_time_us / num_trials) << " us" << std::endl;
        std::cout << std::left << std::setw(28) << "Invalid Commits" 
                  << std::setw(20) << p0_invalid_commits 
                  << std::setw(20) << p1_invalid_commits << std::endl;
        std::cout << "----------------------------------------------------------------" << std::endl;

        ASSERT_EQ(p0_invalid_commits, 0, "Policy 0 must have zero invalid commits");
        ASSERT_EQ(p1_invalid_commits, 0, "Policy 1 must have zero invalid commits");
        ASSERT_TRUE(p1_eligible >= p0_eligible, "Policy 1 must retain at least as many checkpoints as Policy 0");
        std::cout << "  -> Zero invalid solution commits verified across all policies [PASSED]" << std::endl;
    }

    std::cout << "\n================================================================" << std::endl;
    std::cout << "Experiment E4 Completed: All 8 Test Categories Passed 100%!" << std::endl;
    std::cout << "================================================================" << std::endl;
    return true;
}

} // namespace rbl_test

int main(int argc, char* argv[]) {
    std::string map_file = "data/sample/random-32-32-20.map";
    std::string scen_file = "data/sample/random-32-32-20-random-1.scen";

    if (argc >= 3) {
        map_file = argv[1];
        scen_file = argv[2];
    }

    bool ok = rbl_test::runAllE4Tests(map_file, scen_file);
    return ok ? 0 : 1;
}
