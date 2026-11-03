#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <random>
#include <algorithm>
#include <sstream>
#include <memory>

#include "Instance.h"
#include "rbl_resumable_engine.hpp"
#include "rbl_feature_extractor.hpp"
#include "LNS.h"
#include "rbl_estimator.hpp"
#include "rbl_controller.hpp"
#include "rbl_validator.hpp"

namespace rbl {

bool computeInitialPlan(const Instance& instance, int num_agents, std::vector<::Path>& out_paths, double& time_taken_s) {
    auto t0 = std::chrono::steady_clock::now();
    PIBTPPS_option pipp_option;
    pipp_option.windowSize = 5;
    pipp_option.winPIBTSoft = true;

    LNS lns(instance, 120.0, "PP", "PP", "Adaptive", 8, 0, true, "Adaptive", true, 0, pipp_option, "Random", 1);
    bool succ = lns.run();
    if (!succ) {
        return false;
    }
    out_paths.clear();
    out_paths.resize(num_agents);
    for (int i = 0; i < num_agents; ++i) {
        out_paths[i] = lns.agents[i].path;
    }
    auto t1 = std::chrono::steady_clock::now();
    time_taken_s = std::chrono::duration<double>(t1 - t0).count();
    return true;
}

bool savePathsToFile(const std::string& path, const std::vector<::Path>& paths) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;
    for (size_t i = 0; i < paths.size(); ++i) {
        ofs << i << ":";
        for (const auto& step : paths[i]) {
            ofs << "(" << step.location << ")->";
        }
        ofs << "\n";
    }
    return true;
}

bool saveMovingAIPathsToFile(const std::string& path, const Instance& instance, const std::vector<::Path>& paths) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;
    for (size_t i = 0; i < paths.size(); ++i) {
        ofs << "Agent " << i << ":";
        for (const auto& step : paths[i]) {
            ofs << "(" << instance.getColCoordinate(step.location) << "," << instance.getRowCoordinate(step.location) << ")->";
        }
        ofs << "\n";
    }
    return true;
}

bool loadPathsFromFile(const std::string& path, int num_agents, std::vector<::Path>& out_paths) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return false;
    out_paths.clear();
    out_paths.resize(num_agents);
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        int a = std::stoi(line.substr(0, colon));
        if (a < 0 || a >= num_agents) continue;
        std::string rest = line.substr(colon + 1);
        ::Path p;
        size_t pos = 0;
        while ((pos = rest.find('(')) != std::string::npos) {
            size_t close = rest.find(')', pos);
            if (close == std::string::npos) break;
            int loc = std::stoi(rest.substr(pos + 1, close - pos - 1));
            p.push_back(PathEntry(loc));
            rest = rest.substr(close + 1);
        }
        out_paths[a] = p;
    }
    for (int i = 0; i < num_agents; ++i) {
        if (out_paths[i].empty()) return false;
    }
    return true;
}

} // namespace rbl

int main(int argc, char** argv) {
    std::string map_path;
    std::string scen_path;
    int num_agents = 50;
    double budget_s = 1.0;
    std::string policy_str = "SCORE_BASED_ADAPTIVE";
    std::string estimator_str = "RIDGE_REGRESSION";
    unsigned int seed = 42;
    std::string output_json;
    std::string load_init_path;
    std::string dump_init_path;
    std::string dump_final_path;
    std::string val_policy_str = "selective";
    std::string timing_protocol_str = "refinement"; // "refinement" or "end_to_end"

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--map" && i + 1 < argc) map_path = argv[++i];
        else if (arg == "--scen" && i + 1 < argc) scen_path = argv[++i];
        else if (arg == "--agents" && i + 1 < argc) num_agents = std::stoi(argv[++i]);
        else if (arg == "--budget" && i + 1 < argc) budget_s = std::stod(argv[++i]);
        else if (arg == "--policy" && i + 1 < argc) policy_str = argv[++i];
        else if (arg == "--estimator" && i + 1 < argc) estimator_str = argv[++i];
        else if (arg == "--validation-policy" && i + 1 < argc) val_policy_str = argv[++i];
        else if (arg == "--timing-protocol" && i + 1 < argc) timing_protocol_str = argv[++i];
        else if (arg == "--seed" && i + 1 < argc) seed = (unsigned int)std::stoul(argv[++i]);
        else if (arg == "--output" && i + 1 < argc) output_json = argv[++i];
        else if (arg == "--load-init-paths" && i + 1 < argc) load_init_path = argv[++i];
        else if (arg == "--dump-init-paths" && i + 1 < argc) dump_init_path = argv[++i];
        else if (arg == "--dump-final-paths" && i + 1 < argc) dump_final_path = argv[++i];
        else {
            std::cerr << "Unknown or incomplete argument: " << arg << std::endl;
            return 1;
        }
    }

    if (map_path.empty() || scen_path.empty()) {
        std::cerr << "Usage: rbl_eval_runner --map <map> --scen <scen> [options]" << std::endl;
        return 1;
    }

    rbl::ControllerPolicy policy = rbl::ControllerPolicy::SCORE_BASED_ADAPTIVE;
    if (policy_str == "RESTART_ONLY_K0") policy = rbl::ControllerPolicy::RESTART_ONLY_K0;
    else if (policy_str == "FIXED_FIFO_RESUME") policy = rbl::ControllerPolicy::FIXED_FIFO_RESUME;
    else if (policy_str == "SCORE_BASED_FIXED") policy = rbl::ControllerPolicy::SCORE_BASED_FIXED;
    else if (policy_str == "SCORE_BASED_ADAPTIVE") policy = rbl::ControllerPolicy::SCORE_BASED_ADAPTIVE;
    else if (policy_str == "ELAPSED_TIME_ONLY") policy = rbl::ControllerPolicy::ELAPSED_TIME_ONLY;
    else {
        std::cerr << "Unknown policy: " << policy_str << std::endl;
        return 1;
    }

    rbl::EstimatorMode est_mode = rbl::EstimatorMode::RIDGE_REGRESSION;
    if (estimator_str == "RIDGE_REGRESSION") est_mode = rbl::EstimatorMode::RIDGE_REGRESSION;
    else if (estimator_str == "TABULAR_BASELINE") est_mode = rbl::EstimatorMode::TABULAR_BASELINE;
    else if (estimator_str == "CONSTANT_BASELINE") est_mode = rbl::EstimatorMode::CONSTANT_BASELINE;
    else if (estimator_str == "ELAPSED_TIME_ONLY") est_mode = rbl::EstimatorMode::ELAPSED_TIME_ONLY;

    Instance instance(map_path, scen_path, num_agents);
    std::vector<::Path> init_paths;
    double init_plan_time_s = 0.0;
    bool init_ok = false;
    std::string init_plan_source = "LOADED_FROM_FILE";

    if (timing_protocol_str == "end_to_end") {
        init_ok = rbl::computeInitialPlan(instance, num_agents, init_paths, init_plan_time_s);
        init_plan_source = "GENERATED_ON_THE_FLY";
    } else {
        if (!load_init_path.empty()) {
            init_ok = rbl::loadPathsFromFile(load_init_path, num_agents, init_paths);
        }
        if (!init_ok) {
            init_ok = rbl::computeInitialPlan(instance, num_agents, init_paths, init_plan_time_s);
            init_plan_source = "GENERATED_ON_THE_FLY";
            if (init_ok && !dump_init_path.empty()) {
                rbl::savePathsToFile(dump_init_path, init_paths);
            }
        }
    }

    if (!init_ok) {
        std::cerr << "Failed to obtain valid initial plan for " << map_path << " with " << num_agents << " agents" << std::endl;
        if (!output_json.empty()) {
            std::ofstream ofs(output_json);
            ofs << "{\n"
                << "  \"initial_plan_found\": false,\n"
                << "  \"map_name\": \"" << map_path << "\",\n"
                << "  \"scenario_name\": \"" << scen_path << "\",\n"
                << "  \"agent_count\": " << num_agents << ",\n"
                << "  \"budget_s\": " << budget_s << ",\n"
                << "  \"policy\": \"" << policy_str << "\"\n"
                << "}\n";
        }
        return 0; // return cleanly with failure flagged
    }

    double effective_refine_budget_s = budget_s;
    if (timing_protocol_str == "end_to_end") {
        effective_refine_budget_s = std::max(0.001, budget_s - init_plan_time_s);
    }

    rbl::SolveSummary summary;
    if (timing_protocol_str == "end_to_end" && init_plan_time_s >= budget_s) {
        // Budget exhausted during initial planning
        summary.initial_plan_found = true;
        summary.final_solution_valid = true;
        summary.initial_cost = 0;
        for (const auto& p : init_paths) summary.initial_cost += std::max(0, (int)p.size() - 1);
        summary.final_cost = summary.initial_cost;
        summary.total_improvement = 0;
        summary.total_solve_time_s = init_plan_time_s;
        summary.deadline_overshoot_s = std::max(0.0, init_plan_time_s - budget_s);
        if (!dump_final_path.empty()) {
            rbl::saveMovingAIPathsToFile(dump_final_path, instance, init_paths);
        }
    } else {
        // Run solver with selected policy and controller
        int store_cap = (policy == rbl::ControllerPolicy::RESTART_ONLY_K0) ? 0 : 2;
        rbl::ResumableRepairEngine engine(instance, false, store_cap);
        engine.setInitialSolution(init_paths);

        rbl::SearchProgressEstimator estimator;
        rbl::ResumeOrStartController controller(engine, estimator, est_mode, policy, effective_refine_budget_s, seed);
        if (val_policy_str == "discard_on_change") {
            controller.setRetentionPolicy(rbl::RetentionPolicy::DISCARD_ON_CHANGE);
        } else if (val_policy_str == "partial_prefix" || val_policy_str == "partial") {
            controller.setRetentionPolicy(rbl::RetentionPolicy::PARTIAL_PREFIX_RECOVERY);
        } else {
            controller.setRetentionPolicy(rbl::RetentionPolicy::WHOLE_CHECKPOINT_SELECTIVE);
        }

        summary = controller.run(init_paths);
        if (!dump_final_path.empty()) {
            rbl::saveMovingAIPathsToFile(dump_final_path, instance, engine.getIncumbentPaths());
        }
        if (timing_protocol_str == "end_to_end") {
            summary.total_solve_time_s += init_plan_time_s;
            summary.deadline_overshoot_s = std::max(0.0, summary.total_solve_time_s - budget_s);
        }
    }
    summary.initial_planning_time_s = init_plan_time_s;

    // Build JSON output
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);
    ss << "{\n";
    ss << "  \"map_path\": \"" << map_path << "\",\n";
    ss << "  \"scen_path\": \"" << scen_path << "\",\n";
    ss << "  \"map_name\": \"" << map_path.substr(map_path.find_last_of('/') + 1) << "\",\n";
    ss << "  \"scenario_name\": \"" << scen_path.substr(scen_path.find_last_of('/') + 1) << "\",\n";
    ss << "  \"agent_count\": " << num_agents << ",\n";
    ss << "  \"budget_s\": " << budget_s << ",\n";
    ss << "  \"seed\": " << seed << ",\n";
    ss << "  \"policy\": \"" << rbl::toString(policy) << "\",\n";
    ss << "  \"estimator_mode\": \"" << rbl::toString(est_mode) << "\",\n";
    std::string recorded_val_policy = "selective";
    if (val_policy_str == "discard_on_change") recorded_val_policy = "discard_on_change";
    else if (val_policy_str == "partial_prefix" || val_policy_str == "partial") recorded_val_policy = "partial_prefix";
    ss << "  \"validation_policy\": \"" << recorded_val_policy << "\",\n";
    ss << "  \"timing_protocol\": \"" << timing_protocol_str << "\",\n";
    ss << "  \"initial_plan_source\": \"" << init_plan_source << "\",\n";
    ss << "  \"initial_plan_found\": " << (summary.initial_plan_found ? "true" : "false") << ",\n";
    ss << "  \"final_solution_valid\": " << (summary.final_solution_valid ? "true" : "false") << ",\n";
    ss << "  \"validation_error\": \"" << summary.validation_error << "\",\n";
    ss << "  \"initial_cost\": " << summary.initial_cost << ",\n";
    ss << "  \"final_cost\": " << summary.final_cost << ",\n";
    ss << "  \"total_improvement\": " << summary.total_improvement << ",\n";
    ss << "  \"time_of_last_accepted_commit_s\": " << summary.time_of_last_accepted_commit_s << ",\n";
    ss << "  \"total_solve_time_s\": " << summary.total_solve_time_s << ",\n";
    ss << "  \"initial_planning_time_s\": " << summary.initial_planning_time_s << ",\n";
    ss << "  \"initial_validation_time_s\": " << summary.initial_validation_time_s << ",\n";
    ss << "  \"total_active_search_time_s\": " << summary.total_active_search_time_s << ",\n";
    ss << "  \"total_decision_time_s\": " << summary.total_decision_time_s << ",\n";
    ss << "  \"deadline_overshoot_s\": " << summary.deadline_overshoot_s << ",\n";
    ss << "  \"total_decisions\": " << summary.total_decisions << ",\n";
    ss << "  \"fresh_actions_count\": " << summary.fresh_actions_count << ",\n";
    ss << "  \"resumed_actions_count\": " << summary.resumed_actions_count << ",\n";
    ss << "  \"accepted_repairs\": " << summary.accepted_repairs << ",\n";
    ss << "  \"accepted_from_resumed\": " << summary.accepted_from_resumed << ",\n";
    ss << "  \"accepted_from_fresh\": " << summary.accepted_from_fresh << ",\n";
    ss << "  \"non_improving_repairs\": " << summary.non_improving_repairs << ",\n";
    ss << "  \"interrupted_slices\": " << summary.interrupted_slices << ",\n";
    ss << "  \"interrupted_with_nonempty_prefix\": " << summary.interrupted_with_nonempty_prefix << ",\n";
    ss << "  \"interrupted_with_empty_prefix\": " << summary.interrupted_with_empty_prefix << ",\n";
    ss << "  \"incompatible_checkpoints_discarded\": " << summary.incompatible_checkpoints_discarded << ",\n";
    ss << "  \"partial_recovery_count\": " << summary.partial_recovery_count << ",\n";
    ss << "  \"total_salvaged_prefix_agents\": " << summary.total_salvaged_prefix_agents << ",\n";
    ss << "  \"stagnant_checkpoints_evicted\": " << summary.stagnant_checkpoints_evicted << ",\n";
    ss << "  \"late_commits_rejected\": " << summary.late_commits_rejected << ",\n";
    ss << "  \"fallback_decisions_count\": " << summary.fallback_decisions_count << ",\n";
    ss << "  \"total_expansions\": " << summary.total_expansions << ",\n";
    ss << "  \"repeated_expansions\": " << summary.repeated_expansions << ",\n";
    double rep_ratio = (summary.total_expansions > 0) ?
        ((double)summary.repeated_expansions / (double)summary.total_expansions) : 0.0;
    ss << "  \"repeated_work_ratio\": " << rep_ratio << ",\n";
    ss << "  \"fixed_bookkeeping_memory_bytes\": " << summary.fixed_bookkeeping_memory_bytes << ",\n";
    ss << "  \"peak_payload_memory_bytes\": " << summary.peak_payload_memory_bytes << ",\n";
    ss << "  \"peak_checkpoint_memory_bytes\": " << summary.peak_checkpoint_memory_bytes << ",\n";
    ss << "  \"median_decision_latency_us\": " << summary.median_decision_latency_us << ",\n";
    ss << "  \"p95_decision_latency_us\": " << summary.p95_decision_latency_us << ",\n";
    ss << "  \"max_decision_latency_us\": " << summary.max_decision_latency_us << ",\n";

    // Slice distribution
    ss << "  \"slice_durations_selected_count\": {\n";
    size_t s_idx = 0;
    for (const auto& kv : summary.slice_durations_selected_count) {
        ss << "    \"" << kv.first << "ms\": " << kv.second;
        if (++s_idx < summary.slice_durations_selected_count.size()) ss << ",";
        ss << "\n";
    }
    ss << "  },\n";

    // Anytime trajectory
    ss << "  \"anytime_cost_trajectory\": [\n";
    for (size_t t = 0; t < summary.anytime_cost_trajectory.size(); ++t) {
        ss << "    {\"timestamp_s\": " << summary.anytime_cost_trajectory[t].first
           << ", \"cost\": " << summary.anytime_cost_trajectory[t].second << "}";
        if (t + 1 < summary.anytime_cost_trajectory.size()) ss << ",";
        ss << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";

    if (!output_json.empty()) {
        std::ofstream ofs(output_json);
        ofs << ss.str();
    } else {
        std::cout << ss.str();
    }

    return 0;
}
