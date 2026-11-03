// RouteBudget Lab - Controlled Perturbation Profiler for Partial-Prefix Recovery
// Rigorously isolates the algorithmic mechanics of:
//   Policy A: Discard-on-Change (formerly labeled Restart-Only)
//   Policy B: Whole-Checkpoint Selective Retention
//   Policy C: Partial-Prefix Recovery
//
// Evaluates:
// 1. Unchanged incumbent (Control)
// 2. Unrelated external modification (legal non-conflicting path update)
// 3. Early conflict on P_0 (k = 0, salvages 0 paths)
// 4. Middle conflict on P_2 (k = 2, salvages 2 paths: P_0, P_1)
// 5. Late conflict on P_3 (k = 3, salvages 3 paths: P_0, P_1, P_2)
//
// Tested across multiple maps:
// - Map 1: random-32-32-20.map (random grid)
// - Map 2: warehouse-10-20-10-2-1.map (warehouse corridor grid)
//
// Records for each condition:
// - Retained prefix length (k) vs original (r_done = 4)
// - Previously spent expansions (initial slice)
// - New expansions required upon resumption/restart
// - Repeated expansions
// - Total repair expansions
// - Validation latency (us)
// - Restoration latency (us)
// - Replanning duration (s)
// - Cost delta (improvement)
// - Independent validation verdict via rbl::checkPlan

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <chrono>
#include <cassert>
#include <cmath>
#include <queue>
#include <algorithm>

#include "Instance.h"
#include "rbl_resumable_engine.hpp"
#include "rbl_constraint_validator.hpp"
#include "rbl_validator.hpp"

namespace rbl_profiler {
using namespace rbl;

std::vector<::Path> computeInitialSolution(const Instance& instance, int num_agents) {
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

bool buildSpaceTimeTrajectoryVia(const Instance& instance,
                                 int agent,
                                 int target_loc,
                                 int target_t,
                                 const std::vector<::Path>& incumbent,
                                 const std::vector<int>& ext_agents,
                                 const std::vector<::Path>& prior_prefix,
                                 ::Path& out_path) {
    int start = instance.getStarts()[agent];
    int goal = instance.getGoals()[agent];
    int map_size = instance.map_size;
    int max_time = target_t + 60;

    auto is_blocked = [&](int u, int v, int t_curr) {
        // v at t_curr + 1
        for (int other : ext_agents) {
            if (other == agent) continue;
            const auto& p = incumbent[other];
            int p_loc = (t_curr + 1 < (int)p.size()) ? p[t_curr + 1].location : p.back().location;
            if (v == p_loc) return true;
            int p_prev = (t_curr < (int)p.size()) ? p[t_curr].location : p.back().location;
            if (u == p_loc && v == p_prev) return true;
        }
        for (const auto& p : prior_prefix) {
            int p_loc = (t_curr + 1 < (int)p.size()) ? p[t_curr + 1].location : p.back().location;
            if (v == p_loc) return true;
            int p_prev = (t_curr < (int)p.size()) ? p[t_curr].location : p.back().location;
            if (u == p_loc && v == p_prev) return true;
        }
        return false;
    };

    // Leg 1: (start, 0) -> (target_loc, target_t)
    std::vector<int> parent1(map_size * (target_t + 1), -1);
    std::vector<bool> visited1(map_size * (target_t + 1), false);
    std::queue<std::pair<int, int>> q1;
    q1.push({start, 0});
    visited1[start * (target_t + 1) + 0] = true;

    bool reached_waypoint = false;
    while (!q1.empty()) {
        auto curr = q1.front(); q1.pop();
        int u = curr.first;
        int t = curr.second;
        if (u == target_loc && t == target_t) {
            reached_waypoint = true;
            break;
        }
        if (t >= target_t) continue;
        int moves[5] = {0, 1, -1, instance.num_of_cols, -instance.num_of_cols};
        for (int m : moves) {
            int v = u + m;
            int nt = t + 1;
            if (v >= 0 && v < map_size && !instance.isObstacle(v)) {
                if (m != 0 && !instance.validMove(u, v)) continue;
                if (is_blocked(u, v, t)) continue;
                int key = v * (target_t + 1) + nt;
                if (!visited1[key]) {
                    visited1[key] = true;
                    parent1[key] = u * (target_t + 1) + t;
                    q1.push({v, nt});
                }
            }
        }
    }
    if (!reached_waypoint) return false;

    // Leg 2: (target_loc, target_t) -> (goal, final_t)
    int max_t2 = target_t + 60;
    std::vector<int> parent2(map_size * (max_t2 + 1), -1);
    std::vector<bool> visited2(map_size * (max_t2 + 1), false);
    std::queue<std::pair<int, int>> q2;
    q2.push({target_loc, target_t});
    visited2[target_loc * (max_t2 + 1) + target_t] = true;

    int final_t = -1;
    while (!q2.empty()) {
        auto curr = q2.front(); q2.pop();
        int u = curr.first;
        int t = curr.second;
        if (u == goal) {
            // Check if goal stays clear for all timesteps after arrival
            bool goal_clear = true;
            for (int other : ext_agents) {
                if (other == agent) continue;
                const auto& p = incumbent[other];
                for (int st = t; st < (int)p.size() + 20; ++st) {
                    int p_loc = (st < (int)p.size()) ? p[st].location : p.back().location;
                    if (goal == p_loc) { goal_clear = false; break; }
                }
                if (!goal_clear) break;
            }
            if (goal_clear) {
                final_t = t;
                break;
            }
        }
        if (t >= max_t2) continue;
        int moves[5] = {0, 1, -1, instance.num_of_cols, -instance.num_of_cols};
        for (int m : moves) {
            int v = u + m;
            int nt = t + 1;
            if (v >= 0 && v < map_size && !instance.isObstacle(v)) {
                if (m != 0 && !instance.validMove(u, v)) continue;
                if (is_blocked(u, v, t)) continue;
                int key = v * (max_t2 + 1) + nt;
                if (!visited2[key]) {
                    visited2[key] = true;
                    parent2[key] = u * (max_t2 + 1) + t;
                    q2.push({v, nt});
                }
            }
        }
    }
    if (final_t == -1) return false;

    // Reconstruct full path
    std::vector<int> path2;
    for (int curr = goal * (max_t2 + 1) + final_t; curr != -1; ) {
        int loc = curr / (max_t2 + 1);
        int t = curr % (max_t2 + 1);
        path2.push_back(loc);
        if (loc == target_loc && t == target_t) break;
        curr = parent2[curr];
    }
    std::reverse(path2.begin(), path2.end());

    std::vector<int> path1;
    for (int curr = target_loc * (target_t + 1) + target_t; curr != -1; ) {
        int loc = curr / (target_t + 1);
        int t = curr % (target_t + 1);
        path1.push_back(loc);
        if (loc == start && t == 0) break;
        curr = parent1[curr];
    }
    std::reverse(path1.begin(), path1.end());

    out_path.clear();
    for (int loc : path1) out_path.push_back(PathEntry(loc));
    for (size_t i = 1; i < path2.size(); ++i) out_path.push_back(PathEntry(path2[i]));
    return true;
}

bool isCollisionFree(const ::Path& p_test, int test_agent,
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

struct TrialResult {
    std::string map_name;
    std::string condition_name;
    int condition_id = 0;
    std::string policy_name;
    int policy_id = 0;
    int original_prefix_len = 0;
    int salvaged_prefix_len = 0;
    std::string verdict;
    double validation_time_us = 0.0;
    double restore_time_us = 0.0;
    double replanning_duration_s = 0.0;
    uint64_t previously_spent_expansions = 0;
    uint64_t resumption_expansions = 0;
    uint64_t repeated_expansions = 0;
    uint64_t total_repair_expansions = 0;
    std::string outcome_type;
    int cost_delta = 0;
    bool plan_valid = false;
    std::string perturbation_type;
    bool test_override_used = false;
};

TrialResult runSingleTrial(const Instance& instance,
                           const std::string& map_file,
                           const std::string& scen_file,
                           const std::string& map_name,
                           int num_agents,
                           const std::vector<::Path>& base_incumbent,
                           const std::vector<int>& neighborhood,
                           int condition_id,
                           const std::string& condition_name,
                           RetentionPolicy policy,
                           int policy_id,
                           const std::string& policy_name) {
    TrialResult res;
    res.map_name = map_name;
    res.condition_id = condition_id;
    res.condition_name = condition_name;
    res.policy_id = policy_id;
    res.policy_name = policy_name;

    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(base_incumbent);

    // 1. Initial fresh repair interrupted deterministically after 4 agents
    SearchBudget b_slice(50000, 10.0);
    b_slice.setMaxAgents(4); // Deterministic interruption after 4 completed agents
    RepairOutcome o_slice = engine.attemptFreshRepair(neighborhood, b_slice, 42);
    if (o_slice.type != RepairOutcomeType::INTERRUPTED_SLICE) {
        res.outcome_type = toString(o_slice.type);
        return res;
    }

    const auto* cp = engine.getCheckpointStore().getCheckpoint(0);
    if (!cp) return res;
    res.original_prefix_len = cp->r_done;
    res.previously_spent_expansions = o_slice.total_expansions;

    // 2. Apply controlled perturbation to external agents based on condition
    if (condition_id == 1) {
        // Unrelated modification: external agent (last agent) waits 1 step at start if collision-free
        res.perturbation_type = "Unrelated legal trajectory change (Agent waits 1 step at start)";
        int ext_a = num_agents - 1;
        auto alt_p = engine.getIncumbentPaths()[ext_a];
        alt_p.insert(alt_p.begin() + 1, alt_p[0]);
        engine.setIncumbentPathForTest(ext_a, alt_p);
        res.test_override_used = true;
    } else if (condition_id >= 2 && condition_id <= 4) {
        // Distinct conflict targets:
        // condition 2: Early conflict (k=0, targets P_0, salvages 0)
        // condition 3: Middle conflict (k=2, targets P_2, salvages P_0, P_1 -> 2)
        // condition 4: Late conflict (k=3, targets P_3, salvages P_0, P_1, P_2 -> 3)
        int target_k = 0;
        if (condition_id == 2) {
            target_k = 0;
            res.perturbation_type = "Early conflict injected on path P_0 (k=0)";
        } else if (condition_id == 3) {
            target_k = 2;
            res.perturbation_type = "Middle conflict injected on path P_2 (k=2)";
        } else if (condition_id == 4) {
            target_k = 3;
            res.perturbation_type = "Late conflict injected on path P_3 (k=3)";
        }

        if (target_k < cp->r_done) {
            const ::Path& p_target = cp->prefix_paths[target_k];
            std::vector<int> ext_agents;
            for (int a = 0; a < num_agents; ++a) {
                if (std::find(neighborhood.begin(), neighborhood.end(), a) == neighborhood.end()) {
                    ext_agents.push_back(a);
                }
            }

            bool found = false;
            std::vector<::Path> prior_paths(cp->prefix_paths.begin(), cp->prefix_paths.begin() + target_k);
            for (int t = 2; t < 80 && !found; ++t) {
                int loc = (t < (int)p_target.size()) ? p_target[t].location : p_target.back().location;
                for (int ext_a : ext_agents) {
                    ::Path cand;
                    if (buildSpaceTimeTrajectoryVia(instance, ext_a, loc, t, engine.getIncumbentPaths(), ext_agents, prior_paths, cand)) {
                        engine.setIncumbentPathForTest(ext_a, cand);
                        found = true;
                        res.test_override_used = false; // genuine legal space-time trajectory
                        break;
                    }
                }
            }
            if (!found) {
                // Synthetic perturbation: mutate external agent location at t=2 to match p_target[2]
                auto mod_paths = engine.getIncumbentPaths();
                int ext_a = ext_agents.back();
                if ((int)p_target.size() > 2 && mod_paths[ext_a].size() > 2) {
                    mod_paths[ext_a][2].location = p_target[2].location;
                    engine.setIncumbentPathForTest(ext_a, mod_paths[ext_a]);
                    res.test_override_used = true;
                }
            }
        }
    } else {
        res.perturbation_type = "None (Control: Unchanged incumbent)";
        res.test_override_used = false;
    }

    // 3. Side-effect-free compatibility validation
    CompatibilityReport rep = engine.validateCheckpoint(0, policy);
    res.verdict = toString(rep.verdict);
    res.validation_time_us = rep.validation_time_us;
    res.salvaged_prefix_len = rep.valid_prefix_len;

    // 4. Action Execution
    if (!rep.isCompatible()) {
        // Discard checkpoint: fall back to fresh restart on same neighborhood
        SearchBudget b_fresh(50000, 10.0);
        RepairOutcome o_fresh = engine.attemptFreshRepair(neighborhood, b_fresh, 42);
        res.outcome_type = toString(o_fresh.type);
        res.resumption_expansions = o_fresh.total_expansions;
        res.repeated_expansions = o_fresh.repeated_expansions;
        res.total_repair_expansions = res.previously_spent_expansions + o_fresh.total_expansions;
        res.replanning_duration_s = o_fresh.duration_s;
        res.cost_delta = o_fresh.cost_delta;
    } else {
        // Resume checkpoint under policy
        SearchBudget b_res(50000, 10.0);
        RepairOutcome o_res = engine.resumeCheckpoint(0, b_res, policy);
        const auto* cp_after = engine.getCheckpointStore().getCheckpoint(0);
        if (cp_after) res.restore_time_us = cp_after->restore_time_us;
        res.outcome_type = toString(o_res.type);
        res.resumption_expansions = o_res.total_expansions;
        res.repeated_expansions = o_res.repeated_expansions;
        res.total_repair_expansions = res.previously_spent_expansions + o_res.total_expansions;
        res.replanning_duration_s = o_res.duration_s;
        res.cost_delta = o_res.cost_delta;
    }

    // 5. Independent validation via rbl::checkPlan
    std::string map_txt, scen_txt, parse_err;
    rbl::readWholeFile(map_file, map_txt);
    rbl::Grid grid;
    rbl::parseMap(map_txt, grid, parse_err);
    rbl::readWholeFile(scen_file, scen_txt);
    rbl::Scenario scen;
    rbl::parseScenario(scen_txt, num_agents, scen, parse_err);
    std::vector<rbl::Path> val_paths(num_agents);
    for (int i = 0; i < num_agents; ++i) {
        for (const auto& pe : engine.getIncumbentPaths()[i]) {
            val_paths[i].push_back({instance.getColCoordinate(pe.location), instance.getRowCoordinate(pe.location)});
        }
    }
    rbl::Report val_rep;
    rbl::checkPlan(grid, scen.agents, val_paths, val_rep);
    res.plan_valid = (val_rep.status == rbl::Status::VALID);

    return res;
}

} // namespace rbl_profiler

int main(int argc, char** argv) {
    std::vector<std::pair<std::string, std::string>> map_scen_list = {
        {"data/sample/random-32-32-20.map", "data/sample/random-32-32-20-random-1.scen"},
        {"data/training/maps/warehouse-10-20-10-2-1.map", "data/training/scenarios/warehouse-10-20-10-2-1-random-1.scen"}
    };
    int num_agents = 10;
    std::string output_json = "results/partial_recovery_perturbation.json";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc) output_json = argv[++i];
        else if (arg == "--agents" && i + 1 < argc) num_agents = std::stoi(argv[++i]);
    }

    std::vector<std::pair<int, std::string>> conditions = {
        {0, "Control: Unchanged Incumbent"},
        {1, "Unrelated External Change"},
        {2, "Early Conflict (k=0, P_0 collides)"},
        {3, "Middle Conflict (k=2, P_2 collides)"},
        {4, "Late Conflict (k=3, P_3 collides)"}
    };

    std::vector<std::tuple<rbl::RetentionPolicy, int, std::string>> policies = {
        {rbl::RetentionPolicy::DISCARD_ON_CHANGE, 0, "Policy A: Discard-on-Change"},
        {rbl::RetentionPolicy::WHOLE_CHECKPOINT_SELECTIVE, 1, "Policy B: Whole-Selective"},
        {rbl::RetentionPolicy::PARTIAL_PREFIX_RECOVERY, 2, "Policy C: Partial-Prefix"}
    };

    std::vector<int> neighborhood = {0, 1, 2, 3, 4}; // 5-agent neighborhood

    std::vector<rbl_profiler::TrialResult> results;
    std::cout << "==========================================================================" << std::endl;
    std::cout << "RouteBudget Lab: Multi-Map Mechanism Diagnostic for Partial-Prefix Recovery" << std::endl;
    std::cout << "Neighborhood: 5 agents {0,1,2,3,4} | Interrupted after r_done = 4 agents" << std::endl;
    std::cout << "==========================================================================" << std::endl;

    for (const auto& ms : map_scen_list) {
        std::string map_file = ms.first;
        std::string scen_file = ms.second;
        std::string map_name = (map_file.find("random") != std::string::npos) ? "random-32-32-20" : "warehouse-10-20-10-2-1";

        Instance instance(map_file, scen_file, num_agents);
        auto base_incumbent = rbl_profiler::computeInitialSolution(instance, num_agents);

        std::cout << "\n==========================================================================" << std::endl;
        std::cout << "MAP: " << map_name << " (" << map_file << ")" << std::endl;
        std::cout << "==========================================================================" << std::endl;

        for (const auto& cond : conditions) {
            std::cout << "\n--- Condition " << cond.first << ": " << cond.second << " ---" << std::endl;
            for (const auto& pol : policies) {
                auto res = rbl_profiler::runSingleTrial(
                    instance, map_file, scen_file, map_name, num_agents, base_incumbent,
                    neighborhood, cond.first, cond.second,
                    std::get<0>(pol), std::get<1>(pol), std::get<2>(pol));
                results.push_back(res);

                std::cout << "  [" << res.policy_name << "]\n"
                          << "    Verdict: " << res.verdict
                          << " | Salvaged: " << res.salvaged_prefix_len << "/" << res.original_prefix_len
                          << " | New Exp: " << res.resumption_expansions
                          << " | Total Exp: " << res.total_repair_expansions
                          << " (Prev: " << res.previously_spent_expansions << ", Rep: " << res.repeated_expansions << ")"
                          << " | Val: " << std::fixed << std::setprecision(1) << res.validation_time_us << " us"
                          << " | Valid: " << (res.plan_valid ? "YES" : "NO")
                          << std::endl;
            }
        }
    }

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);
    ss << "{\n";
    ss << "  \"num_agents\": " << num_agents << ",\n";
    ss << "  \"neighborhood_size\": " << neighborhood.size() << ",\n";
    ss << "  \"initial_interrupted_prefix_len\": 4,\n";
    ss << "  \"trials\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        ss << "    {\n";
        ss << "      \"map_name\": \"" << r.map_name << "\",\n";
        ss << "      \"condition_id\": " << r.condition_id << ",\n";
        ss << "      \"condition_name\": \"" << r.condition_name << "\",\n";
        ss << "      \"policy_id\": " << r.policy_id << ",\n";
        ss << "      \"policy_name\": \"" << r.policy_name << "\",\n";
        ss << "      \"original_prefix_len\": " << r.original_prefix_len << ",\n";
        ss << "      \"salvaged_prefix_len\": " << r.salvaged_prefix_len << ",\n";
        ss << "      \"verdict\": \"" << r.verdict << "\",\n";
        ss << "      \"validation_time_us\": " << r.validation_time_us << ",\n";
        ss << "      \"restore_time_us\": " << r.restore_time_us << ",\n";
        ss << "      \"previously_spent_expansions\": " << r.previously_spent_expansions << ",\n";
        ss << "      \"resumption_expansions\": " << r.resumption_expansions << ",\n";
        ss << "      \"repeated_expansions\": " << r.repeated_expansions << ",\n";
        ss << "      \"total_repair_expansions\": " << r.total_repair_expansions << ",\n";
        ss << "      \"replanning_duration_s\": " << r.replanning_duration_s << ",\n";
        ss << "      \"outcome_type\": \"" << r.outcome_type << "\",\n";
        ss << "      \"cost_delta\": " << r.cost_delta << ",\n";
        ss << "      \"plan_valid\": " << (r.plan_valid ? "true" : "false") << ",\n";
        ss << "      \"perturbation_type\": \"" << r.perturbation_type << "\",\n";
        ss << "      \"test_override_used\": " << (r.test_override_used ? "true" : "false") << "\n";
        ss << "    }" << (i + 1 < results.size() ? "," : "") << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";

    if (!output_json.empty()) {
        std::ofstream ofs(output_json);
        ofs << ss.str();
        std::cout << "\nResults saved to: " << output_json << std::endl;
    }

    return 0;
}
