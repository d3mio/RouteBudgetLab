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
#include "rbl_validator.hpp"

using namespace rbl;

struct NeighborhoodConfig {
    int size = 4;
};

// Helper: load paths from file
bool loadPathsFromFile(const std::string& path_file, const Instance& instance, int num_agents, std::vector<::Path>& out_paths) {
    std::string text;
    if (!rbl::readWholeFile(path_file, text)) return false;
    ParsedPaths pp;
    std::string err;
    bool unsolved = false;
    // Upstream BALANCE outputs (row, col), so pairIsXY is false
    if (!parsePaths(text, num_agents, false, pp, err, unsolved)) {
        return false;
    }
    out_paths.resize(num_agents);
    for (int i = 0; i < num_agents; ++i) {
        out_paths[i].resize(pp.paths[i].size());
        for (size_t t = 0; t < pp.paths[i].size(); ++t) {
            int col = pp.paths[i][t].x;
            int row = pp.paths[i][t].y;
            out_paths[i][t] = PathEntry(instance.linearizeCoordinate(row, col));
        }
    }
    return true;
}

// Helper: generate initial paths via prioritized planning with safe constraint table
std::vector<::Path> generateInitialPaths(const Instance& instance, int num_agents) {
    std::vector<::Path> paths(num_agents);
    ConstraintTable ct(instance.num_of_cols, instance.map_size);
    for (int i = 0; i < num_agents; ++i) {
        SpaceTimeAStar solver(instance, i);
        paths[i] = solver.findPath(ct);
        if (paths[i].empty()) {
            std::cerr << "Failed to find initial path for agent " << i << std::endl;
            exit(1);
        }
        ct.insert2CT(paths[i]);
    }
    return paths;
}

int main(int argc, char* argv[]) {
    std::string map_file, scen_file, initial_paths_file, out_json;
    int num_agents = 50;
    int iterations = 100;
    double slice_ms = 0.0; // 0.0 = unconstrained
    int neighborhood_size = 4;
    unsigned int seed = 42;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--map" && i + 1 < argc) map_file = argv[++i];
        else if (arg == "--scen" && i + 1 < argc) scen_file = argv[++i];
        else if (arg == "--agents" && i + 1 < argc) num_agents = std::stoi(argv[++i]);
        else if (arg == "--paths" && i + 1 < argc) initial_paths_file = argv[++i];
        else if (arg == "--iterations" && i + 1 < argc) iterations = std::stoi(argv[++i]);
        else if (arg == "--slice-ms" && i + 1 < argc) slice_ms = std::stod(argv[++i]);
        else if (arg == "--n-size" && i + 1 < argc) neighborhood_size = std::stoi(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) seed = std::stoi(argv[++i]);
        else if (arg == "--out-json" && i + 1 < argc) out_json = argv[++i];
    }

    if (map_file.empty() || scen_file.empty()) {
        std::cerr << "Usage: rbl_profile_outcomes --map <file> --scen <file> [options]" << std::endl;
        return 1;
    }

    Instance instance(map_file, scen_file, num_agents);

    std::vector<::Path> initial_paths;
    if (!initial_paths_file.empty() && loadPathsFromFile(initial_paths_file, instance, num_agents, initial_paths)) {
        // Loaded from file
    } else {
        initial_paths = generateInitialPaths(instance, num_agents);
    }

    ResumableRepairEngine engine(instance, false, 2);
    engine.setInitialSolution(initial_paths);

    std::string val_err;
    if (!engine.validateIncumbent(val_err)) {
        std::cerr << "Initial solution invalid: " << val_err << std::endl;
        return 1;
    }

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> agent_dist(0, num_agents - 1);

    int count_accepted = 0;
    int count_not_improving = 0;
    int count_failed = 0;
    int count_interrupted = 0;
    int count_stopped_deadline = 0;

    int interruptions_with_reusable_prefix = 0;
    std::vector<int> prefix_lens_at_interruption;
    std::vector<uint64_t> current_agent_work_at_interruption;
    std::vector<double> active_durations_s;
    std::vector<double> whole_iteration_durations_s;

    for (int it = 0; it < iterations; ++it) {
        auto t_iter_start = std::chrono::steady_clock::now();

        // Select random neighborhood of size neighborhood_size
        std::vector<int> all_agents(num_agents);
        for (int i = 0; i < num_agents; ++i) all_agents[i] = i;
        std::shuffle(all_agents.begin(), all_agents.end(), rng);
        std::vector<int> neighborhood(all_agents.begin(), all_agents.begin() + std::min(neighborhood_size, num_agents));

        // Search budget: unconstrained or artificial slice
        SearchBudget budget;
        if (slice_ms > 0.0) {
            budget = SearchBudget(10000000, slice_ms / 1000.0);
        } else {
            budget = SearchBudget(10000000, 60.0); // essentially unconstrained
        }

        auto t_active_start = std::chrono::steady_clock::now();
        RepairOutcome outcome = engine.attemptFreshRepair(neighborhood, budget, rng());
        auto t_active_end = std::chrono::steady_clock::now();

        double active_dur = std::chrono::duration<double>(t_active_end - t_active_start).count();
        active_durations_s.push_back(active_dur);

        switch (outcome.type) {
            case RepairOutcomeType::COMPLETED_ACCEPTED:
                count_accepted++;
                break;
            case RepairOutcomeType::COMPLETED_NOT_IMPROVING:
                count_not_improving++;
                break;
            case RepairOutcomeType::FAILED_NO_PATH:
                count_failed++;
                break;
            case RepairOutcomeType::INTERRUPTED_SLICE:
                count_interrupted++;
                if (outcome.interruption.has_reusable_prefix) {
                    interruptions_with_reusable_prefix++;
                }
                prefix_lens_at_interruption.push_back(outcome.interruption.completed_prefix_len);
                current_agent_work_at_interruption.push_back(outcome.interruption.current_agent_expansions);
                break;
            case RepairOutcomeType::STOPPED_DEADLINE:
                count_stopped_deadline++;
                break;
        }

        auto t_iter_end = std::chrono::steady_clock::now();
        double iter_dur = std::chrono::duration<double>(t_iter_end - t_iter_start).count();
        whole_iteration_durations_s.push_back(iter_dur);
    }

    // Output JSON
    std::cout << "================================================================" << std::endl;
    std::cout << "Repair Outcome Breakdown (" << (slice_ms > 0.0 ? "ARTIFICIAL SLICE " + std::to_string((int)slice_ms) + "ms" : "UNCONSTRAINED") << ")" << std::endl;
    std::cout << "Map: " << map_file << " | Agents: " << num_agents << " | Iterations: " << iterations << std::endl;
    std::cout << "  Completed and Accepted:       " << count_accepted << " (" << std::fixed << std::setprecision(1) << (100.0 * count_accepted / iterations) << "%)" << std::endl;
    std::cout << "  Completed but Non-Improving:  " << count_not_improving << " (" << (100.0 * count_not_improving / iterations) << "%)" << std::endl;
    std::cout << "  Failed to Find Feasible Path: " << count_failed << " (" << (100.0 * count_failed / iterations) << "%)" << std::endl;
    std::cout << "  Interrupted by Active Slice:  " << count_interrupted << " (" << (100.0 * count_interrupted / iterations) << "%)" << std::endl;
    std::cout << "  Stopped by Global Deadline:   " << count_stopped_deadline << " (" << (100.0 * count_stopped_deadline / iterations) << "%)" << std::endl;
    if (count_interrupted > 0) {
        std::cout << "  -> Interrupted with Reusable Prefix: " << interruptions_with_reusable_prefix << " / " << count_interrupted
                  << " (" << (100.0 * interruptions_with_reusable_prefix / count_interrupted) << "%)" << std::endl;
    }
    std::cout << "================================================================" << std::endl;

    if (!out_json.empty()) {
        std::ofstream jf(out_json);
        jf << "{\n";
        jf << "  \"experiment_label\": \"" << (slice_ms > 0.0 ? "ARTIFICIAL_SLICE_" + std::to_string((int)slice_ms) + "MS" : "UNCONSTRAINED_REPAIR") << "\",\n";
        jf << "  \"map\": \"" << map_file << "\",\n";
        jf << "  \"agents\": " << num_agents << ",\n";
        jf << "  \"neighborhood_size\": " << neighborhood_size << ",\n";
        jf << "  \"iterations\": " << iterations << ",\n";
        jf << "  \"slice_ms\": " << slice_ms << ",\n";
        jf << "  \"outcomes\": {\n";
        jf << "    \"completed_accepted\": " << count_accepted << ",\n";
        jf << "    \"completed_not_improving\": " << count_not_improving << ",\n";
        jf << "    \"failed_no_path\": " << count_failed << ",\n";
        jf << "    \"interrupted_slice\": " << count_interrupted << ",\n";
        jf << "    \"stopped_deadline\": " << count_stopped_deadline << "\n";
        jf << "  },\n";
        jf << "  \"interruption_metrics\": {\n";
        jf << "    \"total_interruptions\": " << count_interrupted << ",\n";
        jf << "    \"interruptions_with_reusable_prefix\": " << interruptions_with_reusable_prefix << ",\n";
        jf << "    \"reusable_prefix_pct\": " << (count_interrupted > 0 ? (100.0 * interruptions_with_reusable_prefix / count_interrupted) : 0.0) << "\n";
        jf << "  }\n";
        jf << "}\n";
    }

    return 0;
}
