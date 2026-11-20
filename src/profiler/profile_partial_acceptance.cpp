// RouteBudget Lab - Partial Acceptance Profiler & Offline Opportunity Analyzer
// Implementation of experimental runner for:
// - Task 8: Offline Opportunity Analysis (natural interrupted repair snapshot capture & multi-selector replay)
// - Task 9: Controlled 192-Run Matched Live Pilot (DISCARD_NO_PA, RESUMPTION_NO_PA, GREEDY_PARTIAL_ACCEPTANCE, EXACT_PARTIAL_ACCEPTANCE)

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <chrono>
#include <cassert>
#include <algorithm>
#include <random>
#include <map>
#include <cmath>

#include <boost/program_options.hpp>

#include "Instance.h"
#include "rbl_resumable_engine.hpp"
#include "rbl_partial_acceptance.hpp"
#include "rbl_validator.hpp"

namespace po = boost::program_options;
using namespace rbl;

namespace {

std::vector<::Path> computeInitialSolution(const Instance& instance, int num_agents, double& out_time_s) {
    auto t0 = std::chrono::steady_clock::now();
    std::vector<::Path> paths(num_agents);
    ConstraintTable ct(instance.num_of_cols, instance.map_size);
    for (int i = 0; i < num_agents; ++i) {
        SpaceTimeAStar solver(instance, i);
        paths[i] = solver.findPath(ct);
        if (paths[i].empty()) {
            std::cerr << "FATAL: Failed to compute initial path for agent " << i << std::endl;
            std::exit(1);
        }
        ct.insert2CT(paths[i]);
    }
    auto t1 = std::chrono::steady_clock::now();
    out_time_s = std::chrono::duration<double>(t1 - t0).count();
    return paths;
}

bool saveInitPathsToFile(const std::string& path, const std::vector<::Path>& paths) {
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

bool loadInitPathsFromFile(const std::string& path, int num_agents, std::vector<::Path>& out_paths) {
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

bool saveMovingAIPathsToFile(const std::string& path, const Instance& instance, const std::vector<::Path>& paths) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;
    for (size_t i = 0; i < paths.size(); ++i) {
        ofs << "Agent " << i << ":";
        for (size_t j = 0; j < paths[i].size(); ++j) {
            const auto& step = paths[i][j];
            ofs << "(" << instance.getRowCoordinate(step.location) << ","
                << instance.getColCoordinate(step.location) << ")";
            if (j + 1 < paths[i].size()) {
                ofs << "->";
            }
        }
        ofs << "\n";
    }
    return true;
}

bool verifyWithIndependentValidator(const Instance& instance,
                                    const std::vector<::Path>& plan,
                                    long long& out_soc,
                                    std::string& out_err) {
    rbl::Grid g;
    g.width = instance.num_of_cols;
    g.height = instance.num_of_rows;
    g.passable.assign((size_t)g.width * g.height, 0);
    for (int loc = 0; loc < instance.map_size; ++loc) {
        g.passable[loc] = instance.isObstacle(loc) ? 0 : 1;
    }

    int m = (int)plan.size();
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
    if (rep.status != rbl::Status::VALID) {
        out_err = rep.issues.empty() ? statusName(rep.status) : (rep.issues[0].code + ": " + rep.issues[0].detail);
    }
    return rep.status == rbl::Status::VALID;
}

struct LatencyStats {
    double median = 0.0;
    double p95 = 0.0;
    double max = 0.0;
    double total = 0.0;
};

LatencyStats computeLatencyStats(std::vector<double> vals) {
    LatencyStats s;
    if (vals.empty()) return s;
    for (double v : vals) s.total += v;
    std::sort(vals.begin(), vals.end());
    s.median = vals[vals.size() / 2];
    size_t p95_idx = std::min<size_t>(vals.size() - 1, static_cast<size_t>(vals.size() * 0.95));
    s.p95 = vals[p95_idx];
    s.max = vals.back();
    return s;
}

// Find oldest compatible suspended checkpoint slot using FIFO order + stagnation eviction
int selectCompatibleResumeSlot(ResumableRepairEngine& engine,
                               bool resumption_enabled,
                               int max_stagnant_attempts,
                               int& stagnant_evictions,
                               int& incompatible_evictions) {
    if (!resumption_enabled) return -1;
    int cap = engine.getCheckpointStore().getCapacity();
    int best_slot = -1;
    uint64_t oldest_id = std::numeric_limits<uint64_t>::max();

    for (int s = 0; s < cap; ++s) {
        const auto* cp = engine.getCheckpointStore().getCheckpoint(s);
        if (!cp) continue;

        if (cp->consecutive_stagnant_attempts >= max_stagnant_attempts) {
            engine.getCheckpointStore().markDiscarded(s, CheckpointRemoval::Stagnation);
            stagnant_evictions++;
            continue;
        }

        CompatibilityReport rep = engine.validateCheckpoint(s, RetentionPolicy::WHOLE_CHECKPOINT_SELECTIVE);
        if (!rep.isCompatible()) {
            engine.getCheckpointStore().markDiscarded(s);
            incompatible_evictions++;
            continue;
        }

        if (cp->checkpoint_id < oldest_id) {
            oldest_id = cp->checkpoint_id;
            best_slot = s;
        }
    }
    return best_slot;
}

} // anonymous namespace

// Declared first so its timestamp follows destruction of engine/reservation state.
struct ReturnBoundaryRecorder {
    std::string file;
    std::chrono::steady_clock::time_point start;
    double budget = 0;
    ~ReturnBoundaryRecorder() noexcept {
        if (file.empty()) return;
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        try {
            std::ofstream out(file);
            out << std::setprecision(17) << "{\"return_boundary_since_budget_start_s\":" << elapsed
                << ",\"return_boundary_overshoot_s\":" << std::max(0.0, elapsed - budget) << "}\n";
        } catch (...) { /* Runner treats absent or malformed sidecar as failure. */ }
    }
};

int main(int argc, char* argv[]) {
    ReturnBoundaryRecorder return_boundary;
    std::string map_file;
    std::string scen_file;
    std::string output_file;
    std::string policy_str = "RESUMPTION_NO_PA";
    std::string mode_str = "online";
    std::string load_init_path;
    std::string dump_init_path;
    std::string dump_final_path;
    int num_agents = 50;
    double time_budget_s = 5.0;
    double slice_budget_s = 0.005;
    int neighborhood_size = 8;
    int rng_seed = 0;
    double terminal_reserve_s = 0.013;
    bool terminal_reserve_control = false;

    po::options_description desc("RouteBudget Lab - Partial Acceptance Profiler");
    desc.add_options()
        ("help,h", "Produce help message")
        ("map,m", po::value<std::string>(&map_file)->required(), "Path to .map file")
        ("scen,s", po::value<std::string>(&scen_file)->required(), "Path to .scen file")
        ("agents,a", po::value<int>(&num_agents)->default_value(50), "Number of agents")
        ("budget,b", po::value<double>(&time_budget_s)->default_value(5.0), "Total planning budget (seconds)")
        ("slice", po::value<double>(&slice_budget_s)->default_value(0.005), "Repair slice budget (seconds)")
        ("neighborhood-size,k", po::value<int>(&neighborhood_size)->default_value(8), "Neighborhood size k")
        ("seed", po::value<int>(&rng_seed)->default_value(0), "Random seed")
        ("policy,p", po::value<std::string>(&policy_str)->default_value("RESUMPTION_NO_PA"),
         "Policy: RESUMPTION_NO_PA, PA_EVERY_INTERRUPTION_FILTERED, PA_EVICTION_ONLY, PA_TERMINAL_ONLY, PA_EVICTION_AND_TERMINAL (or DISCARD_NO_PA, EXACT_PARTIAL_ACCEPTANCE, GREEDY_PARTIAL_ACCEPTANCE)")
        ("terminal-reserve", po::value<double>(&terminal_reserve_s)->default_value(0.013),
         "Terminal reserve before deadline for recovery evaluation (seconds, default 0.013 = 13 ms)")
        ("terminal-reserve-control", po::bool_switch(&terminal_reserve_control)->default_value(false),
         "Control flag: Stop normal search at terminal reserve cutoff without executing recovery")
        ("mode", po::value<std::string>(&mode_str)->default_value("online"),
         "Execution mode: online | offline_snapshots")
        ("load-init-paths", po::value<std::string>(&load_init_path)->default_value(""), "Load shared initial paths from file")
        ("dump-init-paths", po::value<std::string>(&dump_init_path)->default_value(""), "Dump initial paths to file")
        ("dump-final-paths", po::value<std::string>(&dump_final_path)->default_value(""), "Dump final MovingAI paths to file")
        ("output,o", po::value<std::string>(&output_file)->default_value("partial_acceptance_result.json"), "Output JSON file");

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        if (vm.count("help")) {
            std::cout << desc << std::endl;
            return 0;
        }
        po::notify(vm);
    } catch (const std::exception& e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    }

    const std::set<std::string> known_policies = {"RESUMPTION_NO_PA", "PA_EVERY_INTERRUPTION_FILTERED",
        "PA_EVICTION_ONLY", "PA_TERMINAL_ONLY", "PA_EVICTION_AND_TERMINAL",
        "GATED_BLOCKER_CLOSURE",
        "DISCARD_NO_PA", "EXACT_PARTIAL_ACCEPTANCE", "GREEDY_PARTIAL_ACCEPTANCE",
        "UNINTERRUPTED_REPAIR"};
    if (!known_policies.count(policy_str) || time_budget_s <= 0 || slice_budget_s <= 0 ||
        terminal_reserve_s < 0 || neighborhood_size <= 0 || num_agents <= 0) {
        std::cerr << "Invalid policy or planning parameter" << std::endl; return 2;
    }
    Instance instance(map_file, scen_file, num_agents);

    // 1. Obtain initial solution outside the refinement deadline timer (Controlled Refinement Protocol)
    std::vector<::Path> initial_paths;
    double init_plan_time_s = 0.0;
    std::string init_plan_source = "GENERATED_PRIORITIZED_PLANNING";
    if (!load_init_path.empty() && loadInitPathsFromFile(load_init_path, num_agents, initial_paths)) {
        init_plan_source = "LOADED_FROM_SHARED_FIXTURE";
    } else {
        if (!load_init_path.empty()) { std::cerr << "Invalid shared initial fixture" << std::endl; return 2; }
        initial_paths = computeInitialSolution(instance, num_agents, init_plan_time_s);
        if (!dump_init_path.empty()) {
            saveInitPathsToFile(dump_init_path, initial_paths);
        }
    }

    bool is_uninterrupted = (policy_str == "UNINTERRUPTED_REPAIR");
    bool resumption_enabled = (policy_str != "DISCARD_NO_PA" && !is_uninterrupted);
    SelectiveRecoveryPolicy sel_policy = is_uninterrupted ? SelectiveRecoveryPolicy::RESUMPTION_NO_PA : parseSelectiveRecoveryPolicy(policy_str);
    PartialAcceptancePolicy pa_policy = PartialAcceptancePolicy::OFF;
    if (policy_str == "EXACT_PARTIAL_ACCEPTANCE") {
        pa_policy = PartialAcceptancePolicy::EXACT_CLOSURE;
    } else if (policy_str == "GREEDY_PARTIAL_ACCEPTANCE" || (!is_uninterrupted && sel_policy != SelectiveRecoveryPolicy::RESUMPTION_NO_PA)) {
        pa_policy = PartialAcceptancePolicy::GREEDY_DEPENDENCY;
    }
    bool use_pa = (mode_str == "online") && !is_uninterrupted &&
                  (sel_policy != SelectiveRecoveryPolicy::RESUMPTION_NO_PA ||
                   policy_str == "GREEDY_PARTIAL_ACCEPTANCE" || policy_str == "EXACT_PARTIAL_ACCEPTANCE");

    int store_capacity = resumption_enabled ? 2 : 0;
    ResumableRepairEngine engine(instance, false, store_capacity);
    engine.setInitialSolution(initial_paths);
    engine.setSelectiveRecoveryPolicy(sel_policy);
    engine.setPartialAcceptanceEnabled(use_pa);
    engine.setPartialAcceptancePolicy(pa_policy);
    if (mode_str == "offline_snapshots") {
        engine.setSnapshotCaptureEnabled(true);
    }

    std::string init_val_err;
    if (!engine.validateIncumbent(init_val_err)) {
        std::cerr << "FATAL: Initial incumbent invalid: " << init_val_err << std::endl;
        return 1;
    }

    int64_t initial_cost = engine.getIncumbentCost();

    // 2. Start Monotonic Global Refinement Timer
    const double t_reserve_s = 0.0005; // 500 us safety reserve before starting an operation
    bool terminal_recovery_needed = (mode_str == "online") &&
        (sel_policy == SelectiveRecoveryPolicy::PA_TERMINAL_ONLY ||
         sel_policy == SelectiveRecoveryPolicy::PA_EVICTION_AND_TERMINAL ||
         sel_policy == SelectiveRecoveryPolicy::GATED_BLOCKER_CLOSURE);
    double effective_cutoff_reserve_s = (terminal_recovery_needed || terminal_reserve_control) ? terminal_reserve_s : t_reserve_s;
    const int max_stagnant_attempts = 2;
    auto t_global_start = std::chrono::steady_clock::now();
    return_boundary.file = output_file + ".return.json";
    return_boundary.start = t_global_start;
    return_boundary.budget = time_budget_s;
    auto global_deadline = t_global_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(time_budget_s));

    std::mt19937 rng(rng_seed);
    std::vector<int> all_agents(num_agents);
    for (int i = 0; i < num_agents; ++i) all_agents[i] = i;

    int total_iterations = 0;
    int fresh_actions_count = 0;
    int resumed_actions_count = 0;
    int stagnant_evictions = 0;
    int incompatible_evictions = 0;
    double time_of_last_accepted_commit_s = 0.0;

    std::vector<std::pair<double, int64_t>> anytime_cost_trajectory;
    anytime_cost_trajectory.push_back({0.0, initial_cost});

    // Execute Anytime MAPF-LNS repair loop until global deadline reserve
    while (true) {
        auto t_now = std::chrono::steady_clock::now();
        double elapsed_s = std::chrono::duration<double>(t_now - t_global_start).count();
        double b_k = std::max(0.0, time_budget_s - elapsed_s);

        if (b_k <= effective_cutoff_reserve_s) {
            break;
        }

        int resume_slot = is_uninterrupted ? -1 : selectCompatibleResumeSlot(
            engine, resumption_enabled, max_stagnant_attempts, stagnant_evictions, incompatible_evictions);

        auto t_pre_exec = std::chrono::steady_clock::now();
        double elapsed_pre_s = std::chrono::duration<double>(t_pre_exec - t_global_start).count();
        double b_k_exec = std::max(0.0, time_budget_s - elapsed_pre_s);
        if (b_k_exec <= effective_cutoff_reserve_s) {
            break;
        }

        double effective_slice_s;
        uint64_t expansion_cap;
        if (is_uninterrupted) {
            // Uninterrupted repair runs for the entire remaining global budget without artificial slice cutoffs
            effective_slice_s = std::max(0.0001, b_k_exec - effective_cutoff_reserve_s);
            expansion_cap = UINT64_MAX;
        } else {
            effective_slice_s = std::min(slice_budget_s, std::max(0.0001, b_k_exec - effective_cutoff_reserve_s));
            // Standard RouteBudget Lab slice expansion cap rule (100,000 expansions/sec)
            expansion_cap = std::max<uint64_t>(50, static_cast<uint64_t>(slice_budget_s * 100000.0));
        }
        SearchBudget slice_budget(expansion_cap, effective_slice_s);
        slice_budget.setGlobalDeadline(global_deadline);

        total_iterations++;
        RepairOutcome outcome;

        if (resume_slot >= 0) {
            resumed_actions_count++;
            outcome = engine.resumeCheckpoint(resume_slot, slice_budget, RetentionPolicy::WHOLE_CHECKPOINT_SELECTIVE);
        } else {
            fresh_actions_count++;
            std::shuffle(all_agents.begin(), all_agents.end(), rng);
            int k = std::min(neighborhood_size, num_agents);
            std::vector<int> neighborhood(all_agents.begin(), all_agents.begin() + k);
            unsigned int repair_seed = rng();
            outcome = engine.attemptFreshRepair(neighborhood, slice_budget, repair_seed);
        }

        if (outcome.type == RepairOutcomeType::STOPPED_DEADLINE) break;

    }

    double time_normal_search_stopped_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_global_start).count();
    bool terminal_recovery_called = false;
    if (terminal_recovery_needed && !terminal_reserve_control) {
        terminal_recovery_called = true;
        auto t_term_now = std::chrono::steady_clock::now();
        double rem_s = std::max(0.0, std::chrono::duration<double>(global_deadline - t_term_now).count());
        SearchBudget term_budget(100000, rem_s);
        term_budget.setGlobalDeadline(global_deadline);
        engine.attemptTerminalRecovery(term_budget);
    }
    double time_terminal_recovery_completed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_global_start).count();
    double terminal_reserve_consumed_s = std::max(0.0, time_terminal_recovery_completed_s - time_normal_search_stopped_s);

    auto t_global_end = std::chrono::steady_clock::now();
    double total_solve_time_s = std::chrono::duration<double>(t_global_end - t_global_start).count();
    double deadline_overshoot_s = std::max(0.0, total_solve_time_s - time_budget_s);
    bool deadline_exceeded_flag = (total_solve_time_s > time_budget_s);

    for (const auto& publication : engine.getPublicationRecords()) {
        const double ts = std::chrono::duration<double>(publication.complete - t_global_start).count();
        anytime_cost_trajectory.push_back({ts, publication.cost});
        time_of_last_accepted_commit_s = ts;
    }

    // Compute cost available strictly at or before the deadline vs returned final cost
    int64_t final_cost = engine.getIncumbentCost();
    int64_t cost_at_deadline = initial_cost;
    for (const auto& pt : anytime_cost_trajectory) {
        if (pt.first <= time_budget_s) {
            cost_at_deadline = pt.second;
        }
    }
    int64_t cost_reduction_at_deadline = initial_cost - cost_at_deadline;
    int64_t cost_reduction_final = initial_cost - final_cost;

    const auto serialization_start = std::chrono::steady_clock::now();
    // 3. Offline validation and path serialization (strictly outside online solver timer)
    if (!dump_final_path.empty()) {
        saveMovingAIPathsToFile(dump_final_path, instance, engine.getIncumbentPaths());
    }

    std::string internal_val_err;
    bool internal_valid = engine.validateIncumbent(internal_val_err);
    long long independent_soc = 0;
    std::string independent_val_err;
    bool independent_valid = verifyWithIndependentValidator(
        instance, engine.getIncumbentPaths(), independent_soc, independent_val_err);
    bool is_valid = internal_valid && independent_valid && (independent_soc == final_cost);
    std::string val_err = !internal_valid ? internal_val_err :
                          (!independent_valid ? independent_val_err :
                          (independent_soc != final_cost ? "SoC mismatch vs rbl::checkPlan" : ""));

    // 4. If in offline_snapshots mode, replay all captured snapshots offline through all selectors
    if (mode_str == "offline_snapshots") {
        const auto& snapshots = engine.getCapturedSnapshots();
        const auto& goals = instance.getGoals();

        std::ofstream out(output_file);
        if (!out.is_open()) {
            std::cerr << "Failed to open output file: " << output_file << std::endl;
            return 1;
        }

        int positive_feasible_count = 0;
        int greedy_positive_count = 0;
        int exact_gt_greedy_count = 0;
        int exact_vs_oracle_mismatch = 0;
        int nn_ref_vs_exact_mismatch = 0;
        int64_t total_exact_gain = 0;
        int64_t total_greedy_gain = 0;

        out << std::fixed << std::setprecision(9);
        out << "{\n";
        out << "  \"mode\": \"offline_snapshots\",\n";
        out << "  \"collection_policy\": \"" << policy_str << "\",\n";
        out << "  \"map\": \"" << map_file << "\",\n";
        out << "  \"scen\": \"" << scen_file << "\",\n";
        out << "  \"num_agents\": " << num_agents << ",\n";
        out << "  \"time_budget_s\": " << time_budget_s << ",\n";
        out << "  \"slice_budget_s\": " << slice_budget_s << ",\n";
        out << "  \"neighborhood_size\": " << neighborhood_size << ",\n";
        out << "  \"seed\": " << rng_seed << ",\n";
        out << "  \"total_repair_attempts\": " << total_iterations << ",\n";
        out << "  \"total_interruptions\": " << engine.getTotalInterruptionsObserved() << ",\n";
        out << "  \"interruptions_with_completed_paths\": " << engine.getInterruptionsWithCompletedPrefix() << ",\n";
        out << "  \"captured_snapshot_count\": " << snapshots.size() << ",\n";
        out << "  \"snapshots\": [\n";

        for (size_t s_idx = 0; s_idx < snapshots.size(); ++s_idx) {
            const auto& snap = snapshots[s_idx];
            int r = (int)snap.candidate_agents.size();
            std::vector<ReplacementCandidate> candidates(r);
            for (int i = 0; i < r; ++i) {
                int a = snap.candidate_agents[i];
                candidates[i].agent_id = a;
                candidates[i].replacement_path = snap.candidate_paths[i];
                candidates[i].old_path = snap.incumbent_paths[a];
                candidates[i].cost_old = PartialAcceptanceOptimizer::computeArrivalCost(snap.incumbent_paths[a], goals[a]);
                candidates[i].cost_new = PartialAcceptanceOptimizer::computeArrivalCost(snap.candidate_paths[i], goals[a]);
                candidates[i].weight = static_cast<int64_t>(candidates[i].cost_old) - static_cast<int64_t>(candidates[i].cost_new);
            }

            auto t_gb0 = std::chrono::steady_clock::now();
            DependencyGraph graph = PartialAcceptanceOptimizer::buildDependencyGraph(
                candidates, snap.incumbent_paths, instance);
            auto t_gb1 = std::chrono::steady_clock::now();
            double gb_us = std::chrono::duration<double, std::micro>(t_gb1 - t_gb0).count();

            PartialAcceptanceResult greedy_res = PartialAcceptanceOptimizer::solveGreedyDependencyAware(
                graph, candidates, snap.incumbent_version);
            PartialAcceptanceResult exact_res = PartialAcceptanceOptimizer::solveExactClosure(
                graph, candidates, snap.incumbent_version);
            PartialAcceptanceResult nn_res = PartialAcceptanceOptimizer::solveNonnegativeClosureReference(
                graph, candidates, snap.incumbent_version);

            bool oracle_ran = (r <= 12);
            PartialAcceptanceResult oracle_res;
            if (oracle_ran) {
                oracle_res = PartialAcceptanceOptimizer::solveExhaustiveOracle(
                    candidates, snap.incumbent_paths, instance, snap.incumbent_version);
                if (exact_res.predicted_gain != oracle_res.predicted_gain) {
                    exact_vs_oracle_mismatch++;
                }
            }
            if (graph.feasible_negative_weight_count == 0 &&
                nn_res.predicted_gain != exact_res.predicted_gain) {
                nn_ref_vs_exact_mismatch++;
            }

            // Independently validate exact mixture when positive gain is selected
            bool exact_mixture_valid = true;
            double val_us = 0.0;
            if (exact_res.predicted_gain > 0) {
                positive_feasible_count++;
                total_exact_gain += exact_res.predicted_gain;
                auto t_v0 = std::chrono::steady_clock::now();
                std::vector<::Path> mix = snap.incumbent_paths;
                for (size_t k = 0; k < exact_res.selected_agent_ids.size(); ++k) {
                    mix[exact_res.selected_agent_ids[k]] = exact_res.selected_paths[k];
                }
                int64_t mix_soc = 0;
                std::string mix_err;
                exact_mixture_valid = PartialAcceptanceOptimizer::validateCompletePlanIndependently(
                    mix, instance, mix_soc, mix_err);
                if (exact_mixture_valid && (snap.incumbent_cost - mix_soc != exact_res.predicted_gain)) {
                    exact_mixture_valid = false;
                }
                auto t_v1 = std::chrono::steady_clock::now();
                val_us = std::chrono::duration<double, std::micro>(t_v1 - t_v0).count();
            }
            if (greedy_res.predicted_gain > 0) {
                greedy_positive_count++;
                total_greedy_gain += greedy_res.predicted_gain;
            }
            if (exact_res.predicted_gain > greedy_res.predicted_gain) {
                exact_gt_greedy_count++;
            }

            out << "    {\n";
            out << "      \"snapshot_id\": " << snap.snapshot_id << ",\n";
            out << "      \"attempt_id\": " << snap.attempt_id << ",\n";
            out << "      \"action_kind\": \"" << (snap.is_resumed ? "RESUME" : "FRESH") << "\",\n";
            out << "      \"incumbent_version\": " << snap.incumbent_version << ",\n";
            out << "      \"incumbent_cost\": " << snap.incumbent_cost << ",\n";
            out << "      \"neighborhood_size\": " << snap.priority_order.size() << ",\n";
            out << "      \"candidate_count\": " << r << ",\n";
            out << "      \"slice_duration_s\": " << snap.slice_duration_s << ",\n";
            out << "      \"remaining_budget_s\": " << snap.remaining_budget_s << ",\n";
            out << "      \"slice_expansions\": " << snap.slice_expansions << ",\n";
            out << "      \"candidate_agents\": [";
            for (int i = 0; i < r; ++i) out << snap.candidate_agents[i] << (i + 1 < r ? ", " : "");
            out << "],\n";
            out << "      \"weights\": [";
            for (int i = 0; i < r; ++i) out << candidates[i].weight << (i + 1 < r ? ", " : "");
            out << "],\n";
            out << "      \"positive_weight_count\": " << graph.positive_weight_count << ",\n";
            out << "      \"zero_weight_count\": " << graph.zero_weight_count << ",\n";
            out << "      \"negative_weight_count\": " << graph.negative_weight_count << ",\n";
            out << "      \"forbidden_count\": " << graph.forbidden_count << ",\n";
            out << "      \"transitively_excluded_count\": " << graph.transitively_excluded_count << ",\n";
            out << "      \"dependency_arc_count\": " << graph.dependency_arc_count << ",\n";
            out << "      \"feasible_negative_weight_count\": " << graph.feasible_negative_weight_count << ",\n";
            int64_t admissible_u = 0;
            for (const auto& cand : candidates) if (cand.weight > 0) admissible_u += cand.weight;
            out << "      \"admissible_gain_u\": " << admissible_u << ",\n";
            out << "      \"pos_blocker_count\": " << graph.positive_gain_external_blockers.size() << ",\n";
            out << "      \"graph_build_time_us\": " << gb_us << ",\n";
            out << "      \"no_pa_gain\": 0,\n";
            out << "      \"greedy_gain\": " << greedy_res.predicted_gain << ",\n";
            out << "      \"greedy_selected_count\": " << greedy_res.selected_agent_ids.size() << ",\n";
            out << "      \"greedy_selection_time_us\": " << greedy_res.selection_time_us << ",\n";
            out << "      \"exact_gain\": " << exact_res.predicted_gain << ",\n";
            out << "      \"exact_selected_count\": " << exact_res.selected_agent_ids.size() << ",\n";
            out << "      \"exact_selected_positive_count\": " << exact_res.selected_positive_weight_count << ",\n";
            out << "      \"exact_selected_zero_count\": " << exact_res.selected_zero_weight_count << ",\n";
            out << "      \"exact_selected_negative_count\": " << exact_res.selected_negative_weight_count << ",\n";
            out << "      \"exact_selection_time_us\": " << exact_res.selection_time_us << ",\n";
            out << "      \"exact_validation_time_us\": " << val_us << ",\n";
            out << "      \"exact_mixture_valid\": " << (exact_mixture_valid ? "true" : "false") << ",\n";
            out << "      \"nonnegative_ref_gain\": " << nn_res.predicted_gain << ",\n";
            out << "      \"nonnegative_ref_time_us\": " << nn_res.selection_time_us << ",\n";
            out << "      \"oracle_ran\": " << (oracle_ran ? "true" : "false") << ",\n";
            out << "      \"oracle_gain\": " << (oracle_ran ? oracle_res.predicted_gain : 0) << ",\n";
            out << "      \"oracle_time_us\": " << (oracle_ran ? oracle_res.selection_time_us : 0.0) << "\n";
            out << "    }" << (s_idx + 1 < snapshots.size() ? "," : "") << "\n";
        }

        out << "  ],\n";
        out << "  \"summary\": {\n";
        out << "    \"positive_feasible_opportunities\": " << positive_feasible_count << ",\n";
        out << "    \"greedy_positive_opportunities\": " << greedy_positive_count << ",\n";
        out << "    \"exact_strictly_exceeds_greedy_count\": " << exact_gt_greedy_count << ",\n";
        out << "    \"total_exact_gain\": " << total_exact_gain << ",\n";
        out << "    \"total_greedy_gain\": " << total_greedy_gain << ",\n";
        out << "    \"exact_vs_oracle_mismatch_count\": " << exact_vs_oracle_mismatch << ",\n";
        out << "    \"nn_ref_vs_exact_mismatch_count\": " << nn_ref_vs_exact_mismatch << "\n";
        out << "  }\n";
        out << "}\n";

        std::cout << "[Offline Snapshots] Map=" << map_file << " Agents=" << num_agents
                  << " Seed=" << rng_seed << " | Attempts=" << total_iterations
                  << " Interrupts=" << engine.getTotalInterruptionsObserved()
                  << " WithCandidates=" << snapshots.size()
                  << " PosFeasible=" << positive_feasible_count
                  << " ExactGain=" << total_exact_gain
                  << " GreedyGain=" << total_greedy_gain
                  << " Exact>Greedy=" << exact_gt_greedy_count
                  << " OracleMismatch=" << exact_vs_oracle_mismatch << std::endl;
        return (exact_vs_oracle_mismatch == 0 && nn_ref_vs_exact_mismatch == 0) ? 0 : 1;
    }

    // 5. Aggregate Partial Acceptance Telemetry from online history
    const auto& pa_hist = engine.getPartialAcceptanceHistory();
    std::map<int, int> cand_count_hist;
    int64_t total_cands_eval = 0;
    int64_t total_pos_w = 0, total_zero_w = 0, total_neg_w = 0;
    int64_t total_forb = 0, total_trans_excl = 0, total_dep_arcs = 0;
    int64_t total_feas_neg = 0;
    int64_t total_sel_pos = 0, total_sel_zero = 0, total_sel_neg = 0;
    int64_t nn_fast_path_eligible = 0;

    std::vector<double> gb_times, sel_times, val_times, com_times, tot_pa_times;
    for (const auto& h : pa_hist) {
        cand_count_hist[h.candidate_count]++;
        total_cands_eval += h.candidate_count;
        total_pos_w += h.positive_weight_count;
        total_zero_w += h.zero_weight_count;
        total_neg_w += h.negative_weight_count;
        total_forb += h.forbidden_count;
        total_trans_excl += h.transitively_excluded_count;
        total_dep_arcs += h.dependency_arc_count;
        total_feas_neg += h.feasible_negative_weight_count;
        if (h.feasible_negative_weight_count == 0 && h.candidate_count > 0) {
            nn_fast_path_eligible++;
        }
        if (h.status == PartialAcceptanceStatus::COMMITTED) {
            total_sel_pos += h.selected_positive_weight_count;
            total_sel_zero += h.selected_zero_weight_count;
            total_sel_neg += h.selected_negative_weight_count;
        }
        gb_times.push_back(h.graph_build_time_us);
        sel_times.push_back(h.selection_time_us);
        if (h.validation_time_us > 0.0) val_times.push_back(h.validation_time_us);
        if (h.commit_time_us > 0.0) com_times.push_back(h.commit_time_us);
        tot_pa_times.push_back(h.total_time_us);
    }

    LatencyStats gb_stats = computeLatencyStats(gb_times);
    LatencyStats sel_stats = computeLatencyStats(sel_times);
    LatencyStats val_stats = computeLatencyStats(val_times);
    LatencyStats com_stats = computeLatencyStats(com_times);
    LatencyStats tot_pa_stats = computeLatencyStats(tot_pa_times);

    std::cout << "--------------------------------------------------------" << std::endl;
    std::cout << "Policy:                " << policy_str << std::endl;
    std::cout << "Initial Cost:          " << initial_cost << std::endl;
    std::cout << "Cost at Deadline:      " << cost_at_deadline << " (Reduction: " << cost_reduction_at_deadline << ")" << std::endl;
    std::cout << "Final Cost:            " << final_cost << " (Reduction: " << cost_reduction_final << ")" << std::endl;
    std::cout << "Solve Time (s):        " << std::fixed << std::setprecision(6) << total_solve_time_s
              << " (Overshoot: " << deadline_overshoot_s * 1e6 << " us)" << std::endl;
    std::cout << "Total Repair Attempts: " << total_iterations << " (Fresh: " << fresh_actions_count
              << ", Resumed: " << resumed_actions_count << ")" << std::endl;
    std::cout << "Interruptions:         " << engine.getTotalInterruptionsObserved()
              << " (With Completed Paths: " << engine.getInterruptionsWithCompletedPrefix() << ")" << std::endl;
    std::cout << "Accepted Repairs:      " << engine.getAcceptedRepairs() << std::endl;
    std::cout << "PA Attempts:           " << engine.getPartialAcceptanceAttempts() << std::endl;
    std::cout << "PA Positive Opps:      " << engine.getPartialAcceptancePositiveOpportunities() << std::endl;
    std::cout << "PA Committed:          " << engine.getPartialAcceptanceCommitted() << std::endl;
    std::cout << "PA Total Gain:         " << engine.getPartialAcceptanceTotalGain() << std::endl;
    std::cout << "Incumbent Version:     " << engine.getIncumbentVersion() << std::endl;
    std::cout << "Plan Valid:            " << (is_valid ? "YES" : "NO: " + val_err) << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;

    // Output JSON
    std::ofstream out(output_file);
    if (out.is_open()) {
        out << std::fixed << std::setprecision(9);
        out << "{\n";
        out << "  \"policy\": \"" << policy_str << "\",\n";
        out << "  \"resumption_enabled\": " << (resumption_enabled ? "true" : "false") << ",\n";
        out << "  \"partial_acceptance_enabled\": " << (use_pa ? "true" : "false") << ",\n";
        out << "  \"partial_acceptance_policy\": \"" << toString(pa_policy) << "\",\n";
        out << "  \"map\": \"" << map_file << "\",\n";
        out << "  \"scen\": \"" << scen_file << "\",\n";
        out << "  \"num_agents\": " << num_agents << ",\n";
        out << "  \"time_budget_s\": " << time_budget_s << ",\n";
        out << "  \"slice_budget_s\": " << slice_budget_s << ",\n";
        out << "  \"neighborhood_size\": " << neighborhood_size << ",\n";
        out << "  \"seed\": " << rng_seed << ",\n";
        out << "  \"initial_plan_source\": \"" << init_plan_source << "\",\n";
        out << "  \"initial_planning_time_s\": " << init_plan_time_s << ",\n";
        out << "  \"initial_cost\": " << initial_cost << ",\n";
        out << "  \"cost_at_deadline\": " << cost_at_deadline << ",\n";
        out << "  \"final_cost\": " << final_cost << ",\n";
        out << "  \"cost_reduction_at_deadline\": " << cost_reduction_at_deadline << ",\n";
        out << "  \"cost_reduction\": " << cost_reduction_final << ",\n";
        out << "  \"total_solve_time_s\": " << total_solve_time_s << ",\n";
        out << "  \"deadline_overshoot_s\": " << deadline_overshoot_s << ",\n";
        out << "  \"deadline_exceeded_flag\": " << (deadline_exceeded_flag ? "true" : "false") << ",\n";
        out << "  \"time_of_last_accepted_commit_s\": " << time_of_last_accepted_commit_s << ",\n";
        out << "  \"total_iterations\": " << total_iterations << ",\n";
        out << "  \"total_repair_attempts\": " << total_iterations << ",\n";
        out << "  \"fresh_actions_count\": " << fresh_actions_count << ",\n";
        out << "  \"resumed_actions_count\": " << resumed_actions_count << ",\n";
        out << "  \"accepted_repairs\": " << engine.getAcceptedRepairs() << ",\n";
        out << "  \"non_improving_repairs\": " << engine.getNonImprovingRepairs() << ",\n";
        out << "  \"failed_repairs\": " << engine.getFailedRepairs() << ",\n";
        out << "  \"total_interrupted_repairs\": " << engine.getTotalInterruptionsObserved() << ",\n";
        out << "  \"interruptions_with_complete_paths\": " << engine.getInterruptionsWithCompletedPrefix() << ",\n";
        out << "  \"suspended_interrupted_slices\": " << engine.getInterruptedSlices() << ",\n";
        out << "  \"stagnant_checkpoints_evicted\": " << stagnant_evictions << ",\n";
        out << "  \"incompatible_checkpoints_discarded\": " << incompatible_evictions << ",\n";
        out << "  \"total_expansions\": " << engine.getTotalExpansions() << ",\n";
        out << "  \"repeated_expansions\": " << engine.getRepeatedExpansions() << ",\n";
        out << "  \"pa_attempts\": " << engine.getPartialAcceptanceAttempts() << ",\n";
        out << "  \"pa_positive_opportunities\": " << engine.getPartialAcceptancePositiveOpportunities() << ",\n";
        out << "  \"pa_committed\": " << engine.getPartialAcceptanceCommitted() << ",\n";
        out << "  \"pa_budget_skipped\": " << engine.getPartialAcceptanceBudgetSkipped() << ",\n";
        out << "  \"pa_total_gain\": " << engine.getPartialAcceptanceTotalGain() << ",\n";
        out << "  \"pa_total_time_us\": " << engine.getPartialAcceptanceTotalTimeUs() << ",\n";
        out << "  \"selective_recovery_policy\": \"" << toString(sel_policy) << "\",\n";
        out << "  \"terminal_reserve_s\": " << terminal_reserve_s << ",\n";
        out << "  \"terminal_reserve_control\": " << (terminal_reserve_control ? "true" : "false") << ",\n";
        out << "  \"time_normal_search_stopped_s\": " << time_normal_search_stopped_s << ",\n";
        out << "  \"terminal_reserve_consumed_s\": " << terminal_reserve_consumed_s << ",\n";
        out << "  \"terminal_recovery_called\": " << (terminal_recovery_called ? "true" : "false") << ",\n";
        out << "  \"filter_evaluations\": " << engine.getFilterEvaluations() << ",\n";
        out << "  \"filter_skips\": " << engine.getFilterSkips() << ",\n";
        out << "  \"filter_total_time_us\": " << engine.getFilterTotalTimeUs() << ",\n";
        out << "  \"extraction_total_time_us\": " << engine.getExtractionTotalTimeUs() << ",\n";
        out << "  \"eviction_attempts\": " << engine.getEvictionRecoveryAttempts() << ",\n";
        out << "  \"eviction_filter_skips\": " << engine.getEvictionRecoveryFilterSkips() << ",\n";
        out << "  \"eviction_committed\": " << engine.getEvictionRecoveryCommitted() << ",\n";
        out << "  \"eviction_total_gain\": " << engine.getEvictionRecoveryTotalGain() << ",\n";
        out << "  \"eviction_total_time_us\": " << engine.getEvictionRecoveryTotalTimeUs() << ",\n";
        out << "  \"terminal_recovery_calls\": " << engine.getTerminalRecoveryCalls() << ",\n";
        out << "  \"terminal_checkpoints_examined\": " << engine.getTerminalRecoveryExamined() << ",\n";
        out << "  \"terminal_filter_skips\": " << engine.getTerminalRecoveryFilterSkips() << ",\n";
        out << "  \"terminal_committed\": " << engine.getTerminalRecoveryCommitted() << ",\n";
        out << "  \"terminal_total_gain\": " << engine.getTerminalRecoveryTotalGain() << ",\n";
        out << "  \"terminal_total_time_us\": " << engine.getTerminalRecoveryTotalTimeUs() << ",\n";
        out << "  \"pa_telemetry\": {\n";
        out << "    \"total_candidates_evaluated\": " << total_cands_eval << ",\n";
        out << "    \"positive_weights\": " << total_pos_w << ",\n";
        out << "    \"zero_weights\": " << total_zero_w << ",\n";
        out << "    \"negative_weights\": " << total_neg_w << ",\n";
        out << "    \"forbidden_candidate_count\": " << total_forb << ",\n";
        out << "    \"transitively_excluded_count\": " << total_trans_excl << ",\n";
        out << "    \"dependency_arc_count\": " << total_dep_arcs << ",\n";
        out << "    \"feasible_negative_weights\": " << total_feas_neg << ",\n";
        out << "    \"nonnegative_fast_path_eligible_count\": " << nn_fast_path_eligible << ",\n";
        out << "    \"selected_positive_weights\": " << total_sel_pos << ",\n";
        out << "    \"selected_zero_weights\": " << total_sel_zero << ",\n";
        out << "    \"selected_negative_weights\": " << total_sel_neg << ",\n";
        out << "    \"graph_construction_time_us\": {\"median\": " << gb_stats.median << ", \"p95\": " << gb_stats.p95 << ", \"max\": " << gb_stats.max << ", \"total\": " << gb_stats.total << "},\n";
        out << "    \"selection_time_us\": {\"median\": " << sel_stats.median << ", \"p95\": " << sel_stats.p95 << ", \"max\": " << sel_stats.max << ", \"total\": " << sel_stats.total << "},\n";
        out << "    \"validation_time_us\": {\"median\": " << val_stats.median << ", \"p95\": " << val_stats.p95 << ", \"max\": " << val_stats.max << ", \"total\": " << val_stats.total << "},\n";
        out << "    \"commit_time_us\": {\"median\": " << com_stats.median << ", \"p95\": " << com_stats.p95 << ", \"max\": " << com_stats.max << ", \"total\": " << com_stats.total << "},\n";
        out << "    \"total_pa_overhead_us\": {\"median\": " << tot_pa_stats.median << ", \"p95\": " << tot_pa_stats.p95 << ", \"max\": " << tot_pa_stats.max << ", \"total\": " << tot_pa_stats.total << "}\n";
        out << "  },\n";
        out << "  \"incumbent_version\": " << engine.getIncumbentVersion() << ",\n";
        out << "  \"plan_valid\": " << (is_valid ? "true" : "false") << ",\n";
        out << "  \"validation_error\": \"" << val_err << "\",\n";
        out << "  \"checkpoint_capacity\": " << store_capacity << ",\n";
        out << "  \"natural_capacity_evictions\": " << engine.getCheckpointStore().getTotalEvictions() << ",\n";
        out << "  \"normal_checkpoint_consumptions\": " << engine.getCheckpointStore().getRemovalCount(CheckpointRemoval::Consumption) << ",\n";
        out << "  \"deadline_checkpoint_removals\": " << engine.getCheckpointStore().getDeadlineRemovals() << ",\n";
        out << "  \"stagnation_discards\": " << engine.getCheckpointStore().getRemovalCount(CheckpointRemoval::Stagnation) << ",\n";
        out << "  \"invalid_checkpoint_removals\": " << engine.getCheckpointStore().getRemovalCount(CheckpointRemoval::Invalid) << ",\n";
        out << "  \"recovery_checkpoint_consumptions\": " << engine.getCheckpointStore().getRemovalCount(CheckpointRemoval::Recovery) << ",\n";
        out << "  \"normal_search_cutoff_s\": " << time_budget_s - effective_cutoff_reserve_s << ",\n";
        out << "  \"terminal_recovery_start_s\": " << time_normal_search_stopped_s << ",\n";
        out << "  \"terminal_recovery_completion_s\": " << time_terminal_recovery_completed_s << ",\n";
        out << "  \"recovery_events\": [\n";
        const auto& events = engine.getRecoveryEvents();
        for (size_t i = 0; i < events.size(); ++i) {
            const auto& e = events[i]; const auto& r = e.result;
            const bool committed = r.status == PartialAcceptanceStatus::COMMITTED;
            out << "    {\"trigger\":" << std::quoted(e.trigger)
                << ",\"checkpoint_id\":" << e.checkpoint_id
                << ",\"checkpoint_creation_version\":" << e.checkpoint_creation_version
                << ",\"incumbent_version\":" << e.current_version
                << ",\"completed_routes\":" << e.completed
                << ",\"candidates\":" << r.candidate_count
                << ",\"positive\":" << r.positive_weight_count
                << ",\"zero\":" << r.zero_weight_count
                << ",\"negative\":" << r.negative_weight_count
                << ",\"U\":" << (r.bound_computed ? std::to_string(r.optimistic_upper_bound_U) : "null")
                << ",\"filter_skipped\":" << (r.filter_skipped ? "true" : "false")
                << ",\"dependencies\":" << r.dependency_arc_count
                << ",\"conflicts\":" << r.incompatibility_count
                << ",\"outcome\":" << std::quoted(std::string(toString(r.status)))
                << ",\"reason\":" << std::quoted(r.detail)
                << ",\"selected_negative_dependencies\":" << r.selected_negative_weight_count
                << ",\"verified_gain\":" << (committed ? r.verified_gain : 0)
                << ",\"extraction_us\":" << r.extraction_time_us
                << ",\"filter_us\":" << r.filter_time_us
                << ",\"graph_us\":" << r.graph_build_time_us
                << ",\"selection_us\":" << r.selection_time_us
                << ",\"validation_us\":" << r.validation_time_us
                << ",\"commit_us\":" << r.commit_time_us
                << ",\"total_us\":" << r.total_time_us
                << ",\"commit_start_s\":";
            if (committed) out << std::chrono::duration<double>(e.commit_start - t_global_start).count(); else out << "null";
            out << ",\"commit_complete_s\":";
            if (committed) out << std::chrono::duration<double>(e.commit_complete - t_global_start).count(); else out << "null";
            out << ",\"commit_on_time\":" << (committed && e.commit_complete <= global_deadline ? "true" : "false")
                << "}" << (i + 1 < events.size() ? "," : "") << "\n";
        }
        out << "  ],\n";
        out << "  \"anytime_cost_trajectory\": [\n";
        for (size_t i = 0; i < anytime_cost_trajectory.size(); ++i) {
            out << "    {\"timestamp_s\": " << anytime_cost_trajectory[i].first
                << ", \"cost\": " << anytime_cost_trajectory[i].second << "}"
                << (i + 1 < anytime_cost_trajectory.size() ? "," : "") << "\n";
        }
        out << "  ]\n";
        out << "}\n";
    }

    out.close();
    const auto final_output_complete = std::chrono::steady_clock::now();
    std::ofstream timing(output_file + ".timing.json");
    timing << std::setprecision(17) << "{\"serialization_and_output_s\":"
           << std::chrono::duration<double>(final_output_complete - serialization_start).count()
           << ",\"final_output_complete_since_budget_start_s\":"
           << std::chrono::duration<double>(final_output_complete - t_global_start).count() << "}\n";
    return is_valid ? 0 : 1;
}
