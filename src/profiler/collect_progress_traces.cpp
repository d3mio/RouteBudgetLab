#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <random>
#include <algorithm>

#include "Instance.h"
#include "rbl_resumable_engine.hpp"
#include "rbl_feature_extractor.hpp"
#include "rbl_validator.hpp"

namespace rbl {

bool createInitialPlan(const Instance& instance, int num_agents, std::vector<::Path>& out_paths) {
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

struct TraceSample {
    int sample_id;
    std::string partition;
    std::string map_name;
    std::string scen_name;
    int agent_count;
    unsigned int seed;
    int iteration;
    int decision_id;
    std::string action_type_str;
    std::string outcome_type_str;
    SearchProgressFeatures features;
    double target_accepted_improvement;
    double target_active_search_time_s;
    int is_censored;
    double raw_duration_s;
    int raw_cost_delta;
    uint64_t slice_expansions;
};

void writeCsvHeader(std::ostream& os) {
    os << "sample_id,partition,map_name,scen_name,agent_count,seed,iteration,decision_id,"
       << "action_type_str,outcome_type_str,"
       << "action_type,neighborhood_size,completed_prefix_ratio,cumulative_expansions,"
       << "cumulative_active_time_s,repeated_expansions,remaining_agents_count,"
       << "remaining_agents_lowerbound_cost,current_neighborhood_cost,prefix_cost_ratio,"
       << "local_congestion_density,requested_slice_duration_s,remaining_global_budget_ratio,"
       << "target_accepted_improvement,target_active_search_time_s,is_censored,"
       << "raw_duration_s,raw_cost_delta,slice_expansions\n";
}

void writeCsvRow(std::ostream& os, const TraceSample& s) {
    os << s.sample_id << ","
       << s.partition << ","
       << s.map_name << ","
       << s.scen_name << ","
       << s.agent_count << ","
       << s.seed << ","
       << s.iteration << ","
       << s.decision_id << ","
       << s.action_type_str << ","
       << s.outcome_type_str << ","
       << std::fixed << std::setprecision(6)
       << s.features.action_type << ","
       << s.features.neighborhood_size << ","
       << s.features.completed_prefix_ratio << ","
       << s.features.cumulative_expansions << ","
       << s.features.cumulative_active_time_s << ","
       << s.features.repeated_expansions << ","
       << s.features.remaining_agents_count << ","
       << s.features.remaining_agents_lowerbound_cost << ","
       << s.features.current_neighborhood_cost << ","
       << s.features.prefix_cost_ratio << ","
       << s.features.local_congestion_density << ","
       << s.features.requested_slice_duration_s << ","
       << s.features.remaining_global_budget_ratio << ","
       << s.target_accepted_improvement << ","
       << s.target_active_search_time_s << ","
       << s.is_censored << ","
       << s.raw_duration_s << ","
       << s.raw_cost_delta << ","
       << s.slice_expansions << "\n";
}

} // namespace rbl

int main(int argc, char* argv[]) {
    std::string out_csv = "results/traces/search_progress_traces.csv";
    std::string partition_target = "training"; // or "all"
    int agents = 50;
    int runs_per_map = 3;

    if (argc >= 2) out_csv = argv[1];
    if (argc >= 3) partition_target = argv[2];
    if (argc >= 4) agents = std::stoi(argv[3]);
    if (argc >= 5) runs_per_map = std::stoi(argv[4]);

    std::vector<std::pair<std::string, std::string>> map_scen_pairs;

    // Designated partitions
    if (partition_target == "training" || partition_target == "all") {
        map_scen_pairs.push_back({"data/training/maps/random-32-32-20.map", "data/training/scenarios/random-32-32-20-random-1.scen"});
        map_scen_pairs.push_back({"data/training/maps/empty-32-32.map", "data/training/scenarios/empty-32-32-random-1.scen"});
        map_scen_pairs.push_back({"data/training/maps/maze-32-32-2.map", "data/training/scenarios/maze-32-32-2-random-1.scen"});
        map_scen_pairs.push_back({"data/training/maps/warehouse-10-20-10-2-1.map", "data/training/scenarios/warehouse-10-20-10-2-1-random-1.scen"});
    }
    if (partition_target == "validation" || partition_target == "all") {
        map_scen_pairs.push_back({"data/validation/maps/random-64-64-10.map", "data/validation/scenarios/random-64-64-10-random-1.scen"});
        map_scen_pairs.push_back({"data/validation/maps/warehouse-10-20-10-2-2.map", "data/validation/scenarios/warehouse-10-20-10-2-2-random-1.scen"});
    }

    std::cout << "================================================================" << std::endl;
    std::cout << "RouteBudget Lab: Search-Progress Trace Collector (Phase 5 / FR-04)" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "Output CSV: " << out_csv << std::endl;
    std::cout << "Target Partition: " << partition_target << " (" << map_scen_pairs.size() << " map-scenario pairs)" << std::endl;
    std::cout << "Agents per instance: " << agents << std::endl;

    // Slice durations to sample (spanning short, medium, and long slices)
    std::vector<double> slice_grid_s = {0.002, 0.005, 0.010, 0.020, 0.050};
    // Neighborhood sizes to sample
    std::vector<int> neigh_sizes = {2, 4, 8};

    std::ofstream ofs(out_csv);
    if (!ofs.is_open()) {
        std::cerr << "Failed to open output CSV: " << out_csv << std::endl;
        return 1;
    }
    rbl::writeCsvHeader(ofs);

    int sample_id = 0;
    int accepted_count = 0;
    int non_improving_count = 0;
    int interrupted_count = 0;
    int failed_count = 0;
    int fresh_count = 0;
    int resume_count = 0;

    for (const auto& pair : map_scen_pairs) {
        const std::string& map_path = pair.first;
        const std::string& scen_path = pair.second;

        std::string part_name = (map_path.find("/training/") != std::string::npos) ? "training" : "validation";
        std::string map_name = map_path.substr(map_path.find_last_of('/') + 1);
        std::string scen_name = scen_path.substr(scen_path.find_last_of('/') + 1);

        std::cout << "\n[Trace Collection] Map: " << map_name << " (" << part_name << ")..." << std::endl;

        int cur_agents = agents;
        std::vector<::Path> initial_paths;
        std::unique_ptr<Instance> p_instance;

        while (cur_agents >= 5) {
            p_instance = std::make_unique<Instance>(map_path, scen_path, cur_agents);
            if (rbl::createInitialPlan(*p_instance, cur_agents, initial_paths)) {
                break;
            }
            cur_agents = (cur_agents <= 10) ? (cur_agents - 2) : (cur_agents - 5);
        }

        if (initial_paths.empty() || cur_agents < 3) {
            std::cerr << "Warning: Could not create collision-free initial plan for " << map_name << ". Skipping." << std::endl;
            continue;
        }

        std::cout << "Successfully planned initial solution for " << cur_agents << " agents on " << map_name << std::endl;
        Instance& instance = *p_instance;

        for (int r = 0; r < runs_per_map; ++r) {
            unsigned int seed = 42 + r * 1009;
            std::mt19937 rng(seed);

            rbl::ResumableRepairEngine engine(instance, false, 2);
            engine.setInitialSolution(initial_paths);

            double global_budget_B = 10.0;
            auto start_wall = std::chrono::steady_clock::now();

            int decision_id = 0;
            const int max_decisions_per_run = 40;

            while (decision_id < max_decisions_per_run) {
                auto now_wall = std::chrono::steady_clock::now();
                double elapsed_s = std::chrono::duration<double>(now_wall - start_wall).count();
                if (elapsed_s >= global_budget_B) break;
                double budget_ratio = std::max(0.0, (global_budget_B - elapsed_s) / global_budget_B);

                // Check if we have an eligible suspended checkpoint
                int resume_slot = -1;
                for (int s = 0; s < 2; ++s) {
                    const auto* cp = engine.getCheckpointStore().getCheckpoint(s);
                    if (cp) {
                        rbl::CompatibilityReport rep = engine.validateCheckpoint(s, true);
                        if (rep.isCompatible()) {
                            resume_slot = s;
                            break;
                        }
                    }
                }

                // If checkpoint exists, sample either resume or fresh with probability 0.5
                bool do_resume = (resume_slot >= 0) && (rng() % 2 == 0);

                // Pick a slice duration
                double slice_s = slice_grid_s[rng() % slice_grid_s.size()];
                // Conservative expansion cap corresponding to slice_s (approx 100,000 exp / sec)
                uint64_t max_expansions = std::max<uint64_t>(50, (uint64_t)(slice_s * 100000.0));

                rbl::TraceSample sample;
                sample.sample_id = sample_id++;
                sample.partition = part_name;
                sample.map_name = map_name;
                sample.scen_name = scen_name;
                sample.agent_count = cur_agents;
                sample.seed = seed;
                sample.iteration = r;
                sample.decision_id = decision_id++;

                if (do_resume) {
                    // RESUME ACTION
                    sample.action_type_str = "RESUME";
                    resume_count++;

                    // 1. Extract decision-time features BEFORE execution
                    sample.features = rbl::FeatureExtractor::extractResumed(engine, resume_slot, slice_s, budget_ratio);

                    // 2. Execute resumption
                    rbl::SearchBudget budget(max_expansions, slice_s);
                    rbl::RepairOutcome outcome = engine.resumeCheckpoint(resume_slot, budget, true);

                    // 3. Record post-execution observation
                    sample.outcome_type_str = rbl::toString(outcome.type);
                    sample.raw_duration_s = outcome.duration_s;
                    sample.raw_cost_delta = outcome.cost_delta;
                    sample.slice_expansions = outcome.total_expansions;

                    if (outcome.type == rbl::RepairOutcomeType::COMPLETED_ACCEPTED) {
                        accepted_count++;
                        sample.target_accepted_improvement = std::max(0.0, (double)(-outcome.cost_delta));
                        sample.target_active_search_time_s = std::min(outcome.duration_s, slice_s);
                        sample.is_censored = 0;
                    } else if (outcome.type == rbl::RepairOutcomeType::COMPLETED_NOT_IMPROVING) {
                        non_improving_count++;
                        sample.target_accepted_improvement = 0.0;
                        sample.target_active_search_time_s = std::min(outcome.duration_s, slice_s);
                        sample.is_censored = 0;
                    } else if (outcome.type == rbl::RepairOutcomeType::INTERRUPTED_SLICE) {
                        interrupted_count++;
                        sample.target_accepted_improvement = 0.0;
                        sample.target_active_search_time_s = slice_s; // censored at slice limit
                        sample.is_censored = 1;
                    } else {
                        failed_count++;
                        sample.target_accepted_improvement = 0.0;
                        sample.target_active_search_time_s = std::min(outcome.duration_s, slice_s);
                        sample.is_censored = 0;
                    }

                    rbl::writeCsvRow(ofs, sample);
                    ofs.flush();
                } else {
                    // FRESH REPAIR ACTION
                    sample.action_type_str = "FRESH";
                    fresh_count++;

                    int target_k = neigh_sizes[rng() % neigh_sizes.size()];
                    int k = std::min(target_k, std::max(2, cur_agents - 1));
                    std::vector<int> all_agents(cur_agents);
                    for (int i = 0; i < cur_agents; ++i) all_agents[i] = i;
                    std::shuffle(all_agents.begin(), all_agents.end(), rng);
                    std::vector<int> neighborhood(all_agents.begin(), all_agents.begin() + k);

                    // 1. Extract decision-time features BEFORE execution
                    sample.features = rbl::FeatureExtractor::extractFresh(engine, neighborhood, slice_s, budget_ratio);

                    // 2. Execute fresh repair
                    rbl::SearchBudget budget(max_expansions, slice_s);
                    rbl::RepairOutcome outcome = engine.attemptFreshRepair(neighborhood, budget, rng());

                    // 3. Record post-execution observation
                    sample.outcome_type_str = rbl::toString(outcome.type);
                    sample.raw_duration_s = outcome.duration_s;
                    sample.raw_cost_delta = outcome.cost_delta;
                    sample.slice_expansions = outcome.total_expansions;

                    if (outcome.type == rbl::RepairOutcomeType::COMPLETED_ACCEPTED) {
                        accepted_count++;
                        sample.target_accepted_improvement = std::max(0.0, (double)(-outcome.cost_delta));
                        sample.target_active_search_time_s = std::min(outcome.duration_s, slice_s);
                        sample.is_censored = 0;
                    } else if (outcome.type == rbl::RepairOutcomeType::COMPLETED_NOT_IMPROVING) {
                        non_improving_count++;
                        sample.target_accepted_improvement = 0.0;
                        sample.target_active_search_time_s = std::min(outcome.duration_s, slice_s);
                        sample.is_censored = 0;
                    } else if (outcome.type == rbl::RepairOutcomeType::INTERRUPTED_SLICE) {
                        interrupted_count++;
                        sample.target_accepted_improvement = 0.0;
                        sample.target_active_search_time_s = slice_s; // censored at slice limit
                        sample.is_censored = 1;
                    } else {
                        failed_count++;
                        sample.target_accepted_improvement = 0.0;
                        sample.target_active_search_time_s = std::min(outcome.duration_s, slice_s);
                        sample.is_censored = 0;
                    }

                    rbl::writeCsvRow(ofs, sample);
                    ofs.flush();
                }
            }
        }
    }

    ofs.close();

    std::cout << "\n================================================================" << std::endl;
    std::cout << "Trace Collection Complete: " << sample_id << " total samples collected" << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "Action Coverage:  Fresh = " << fresh_count << " | Resumed = " << resume_count << std::endl;
    std::cout << "Outcomes:         Accepted = " << accepted_count 
              << " | Non-Improving = " << non_improving_count 
              << " | Interrupted = " << interrupted_count 
              << " | Failed = " << failed_count << std::endl;
    std::cout << "Output File:      " << out_csv << std::endl;

    return 0;
}
