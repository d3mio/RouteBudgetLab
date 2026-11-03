#pragma once
// RouteBudget Lab - Overhead-Aware Resume-or-Start Controller (Component 4: O4, FR-05, WP4)
//
// Implements:
// 1. Proposed Controller Scoring Equation:
//    Score(a, Delta) = E_hat[AcceptedImprovement(a, Delta)] / (E_hat[min(T_rem(a), Delta)] + Overhead_hat(a))
//    subject to admissibility: Delta + Overhead_hat(a) + t_reserve <= b_k
// 2. Strict Monotonic Wall-Clock Accounting via std::chrono::steady_clock
// 3. Three matched policy modes:
//    - RESTART_ONLY_K0 (K=0)
//    - FIXED_FIFO_RESUME (K=2)
//    - SCORE_BASED_ADAPTIVE (K=2)
// 4. Deterministic fallback for all-zero or non-finite predictions
// 5. Bounded response for repeated no-progress resumptions (stagnation eviction)
// 6. Recheck before commit to prevent late commits exceeding deadline B

#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <random>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <memory>

#include "rbl_resumable_engine.hpp"
#include "rbl_feature_extractor.hpp"
#include "rbl_estimator.hpp"

namespace rbl {

enum class ControllerPolicy {
    RESTART_ONLY_K0,       // Baseline: No checkpoints (K=0), fresh repairs only with fixed 10ms slice
    FIXED_FIFO_RESUME,     // Reference: FIFO resumption whenever compatible checkpoint exists (K=2, fixed 10ms slice)
    SCORE_BASED_FIXED,     // Matched Control: Score-based resume-or-start selection with matched fixed 10ms slice (K=2)
    SCORE_BASED_ADAPTIVE,  // Proposed: Score-based resume-or-start with adaptive slice grid (K=2)
    ELAPSED_TIME_ONLY      // Ablation (E5): Score-based selection using only elapsed-time features, no progress features (K=2)
};

inline const char* toString(ControllerPolicy p) {
    switch (p) {
        case ControllerPolicy::RESTART_ONLY_K0:      return "RESTART_ONLY_K0";
        case ControllerPolicy::FIXED_FIFO_RESUME:    return "FIXED_FIFO_RESUME";
        case ControllerPolicy::SCORE_BASED_FIXED:    return "SCORE_BASED_FIXED";
        case ControllerPolicy::SCORE_BASED_ADAPTIVE: return "SCORE_BASED_ADAPTIVE";
        case ControllerPolicy::ELAPSED_TIME_ONLY:    return "ELAPSED_TIME_ONLY";
        default: return "UNKNOWN";
    }
}

enum class ActionKind {
    FRESH_REPAIR,
    RESUME_CHECKPOINT
};

inline const char* toString(ActionKind k) {
    return (k == ActionKind::FRESH_REPAIR) ? "FRESH" : "RESUME";
}

struct ActionCandidate {
    ActionKind kind = ActionKind::FRESH_REPAIR;
    int checkpoint_slot = -1;             // for RESUME_CHECKPOINT
    std::vector<int> neighborhood;         // for FRESH_REPAIR
    int neighborhood_size = 0;
    double completed_prefix_ratio = 0.0;  // for sorting / fallback
    uint64_t accumulated_expansions = 0;
};

// Prospective Overhead Model (WP4, FR-05)
// Incurred costs (validation, candidate generation, feature extraction, scoring)
// are already elapsed time deducted from b_k.
// Overhead_hat(a) models prospective restore/init and conditional saving costs.
struct ProspectiveOverheadModel {
    double t_init_s = 2.0e-6;       // 2 us constraint table initialization for fresh repair
    double t_restore_s = 4.0e-6;    // 4 us prefix path restoration for resumed repair
    double t_save_s = 1.0e-6;       // 1 us checkpoint state snapshot
    double p_interrupt = 0.15;      // Historical probability of slice interruption
    double t_reserve_s = 0.0005;    // 0.5 ms safety reserve for final validation & commit

    double estimateOverhead(ActionKind kind) const {
        if (kind == ActionKind::FRESH_REPAIR) {
            return t_init_s + p_interrupt * t_save_s;
        } else {
            return t_restore_s + p_interrupt * t_save_s;
        }
    }
};

struct ControllerDecision {
    int decision_id = 0;
    double timestamp_s = 0.0;
    double remaining_budget_s = 0.0;
    ActionCandidate action;
    double slice_duration_s = 0.0;
    double predicted_improvement = 0.0;
    double predicted_active_time_s = 0.0;
    double prospective_overhead_s = 0.0;
    double score = 0.0;
    bool is_fallback = false;
    std::string fallback_reason;
    double decision_latency_us = 0.0; // t_cand + t_compat + t_feat + t_score + t_select
    RepairOutcome outcome;
};

struct SolveSummary {
    ControllerPolicy policy = ControllerPolicy::SCORE_BASED_ADAPTIVE;
    std::string map_name;
    int agent_count = 0;
    unsigned int seed = 0;
    double global_deadline_B_s = 0.0;

    // Solution Cost & Validation
    bool initial_plan_found = false;
    int64_t initial_cost = 0;
    int64_t final_cost = 0;
    int64_t total_improvement = 0;
    bool final_solution_valid = false;
    std::string validation_error;

    // Time Accounting (all steady_clock monotonic)
    double total_solve_time_s = 0.0;
    double initial_planning_time_s = 0.0;
    double initial_validation_time_s = 0.0;
    double total_active_search_time_s = 0.0;
    double total_decision_time_s = 0.0;
    double total_compat_check_time_s = 0.0;
    double total_overhead_time_s = 0.0;
    double deadline_overshoot_s = 0.0;
    double time_of_last_accepted_commit_s = 0.0; // Unclipped timestamp of last accepted commit

    // Action & Outcome Counts
    int total_decisions = 0;
    int fresh_actions_count = 0;
    int resumed_actions_count = 0;
    int accepted_repairs = 0;
    int accepted_from_resumed = 0;
    int accepted_from_fresh = 0;
    int non_improving_repairs = 0;
    int interrupted_slices = 0;
    int interrupted_with_nonempty_prefix = 0;
    int interrupted_with_empty_prefix = 0;
    int failed_repairs = 0;
    int incompatible_checkpoints_discarded = 0;
    int stagnant_checkpoints_evicted = 0;
    int late_commits_rejected = 0;
    int fallback_decisions_count = 0;
    int partial_recovery_count = 0;
    int total_salvaged_prefix_agents = 0;

    // Search Work
    uint64_t total_expansions = 0;
    uint64_t repeated_expansions = 0;

    // Memory & Latency
    size_t fixed_bookkeeping_memory_bytes = 0; // Fixed C++ store object overhead
    size_t peak_payload_memory_bytes = 0;       // Dynamic checkpoint heap footprint (strictly 0 for K=0)
    size_t peak_checkpoint_memory_bytes = 0;    // Total = fixed + payload
    double median_decision_latency_us = 0.0;
    double p95_decision_latency_us = 0.0;
    double max_decision_latency_us = 0.0;

    // Slice Selection Distribution (slice_ms -> count)
    std::map<int, int> slice_durations_selected_count;

    // Anytime trajectory of (timestamp_s, incumbent_cost)
    std::vector<std::pair<double, int64_t>> anytime_cost_trajectory;

    std::vector<ControllerDecision> decisions;
};

class ResumeOrStartController {
public:
    ResumeOrStartController(
        ResumableRepairEngine& engine,
        const SearchProgressEstimator& estimator,
        EstimatorMode estimator_mode = EstimatorMode::RIDGE_REGRESSION,
        ControllerPolicy policy = ControllerPolicy::SCORE_BASED_ADAPTIVE,
        double global_deadline_B_s = 10.0,
        unsigned int seed = 42)
        : engine_(engine),
          estimator_(estimator),
          estimator_mode_(estimator_mode),
          policy_(policy),
          global_deadline_B_s_(global_deadline_B_s),
          rng_(seed)
    {
        candidate_k_values_ = {2, 4, 8};
        setPolicy(policy_);
    }

    void setPolicy(ControllerPolicy p) {
        policy_ = p;
        if (policy_ == ControllerPolicy::RESTART_ONLY_K0) {
            engine_.getCheckpointStore().setCapacity(0);
            candidate_slices_s_ = {0.010}; // Fixed 10ms slice
        } else if (policy_ == ControllerPolicy::FIXED_FIFO_RESUME) {
            engine_.getCheckpointStore().setCapacity(2);
            candidate_slices_s_ = {0.010}; // Fixed 10ms slice
        } else if (policy_ == ControllerPolicy::SCORE_BASED_FIXED) {
            engine_.getCheckpointStore().setCapacity(2);
            candidate_slices_s_ = {0.010}; // Matched Control: Fixed 10ms slice
        } else if (policy_ == ControllerPolicy::SCORE_BASED_ADAPTIVE) {
            engine_.getCheckpointStore().setCapacity(2);
            candidate_slices_s_ = {0.002, 0.005, 0.010, 0.020, 0.050}; // Adaptive slice grid
        } else if (policy_ == ControllerPolicy::ELAPSED_TIME_ONLY) {
            engine_.getCheckpointStore().setCapacity(2);
            candidate_slices_s_ = {0.002, 0.005, 0.010, 0.020, 0.050};
            estimator_mode_ = EstimatorMode::ELAPSED_TIME_ONLY;
        }
    }

    void setEstimatorMode(EstimatorMode m) { estimator_mode_ = m; }
    void setOverheadModel(const ProspectiveOverheadModel& model) { overhead_model_ = model; }
    void setMaxStagnantAttempts(int max_att) { max_stagnant_attempts_ = max_att; }
    void setCandidateSlices(const std::vector<double>& slices) { candidate_slices_s_ = slices; }
    void setRetentionPolicy(RetentionPolicy p) { retention_policy_ = p; }
    RetentionPolicy getRetentionPolicy() const { return retention_policy_; }
    void setSelectiveRetention(bool val) {
        retention_policy_ = val ? RetentionPolicy::WHOLE_CHECKPOINT_SELECTIVE : RetentionPolicy::DISCARD_ON_CHANGE;
    }
    bool getSelectiveRetention() const { return retention_policy_ != RetentionPolicy::DISCARD_ON_CHANGE; }

    // Direct interface for microbenchmarking the decision pipeline
    bool selectScoreBasedForAudit(ControllerDecision& decision, double b_k, double budget_ratio) {
        return selectScoreBased(decision, b_k, budget_ratio);
    }

    // Run the full solve loop with monotonic deadline accounting
    SolveSummary run(const std::vector<Path>& initial_paths) {
        SolveSummary summary;
        summary.policy = policy_;
        summary.agent_count = (int)initial_paths.size();
        summary.global_deadline_B_s = global_deadline_B_s_;
        stagnant_evictions_this_run_ = 0;

        auto t_global_start = std::chrono::steady_clock::now();
        auto global_deadline = t_global_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(global_deadline_B_s_));

        if (initial_paths.empty() || (int)initial_paths.size() != engine_.getInstance().getDefaultNumberOfAgents()) {
            summary.initial_plan_found = false;
            summary.validation_error = "Initial plan empty or agent count mismatch";
            return summary;
        }

        // 1. Initial Solution Setup & Independent Validation
        auto t_val_start = std::chrono::steady_clock::now();
        if (engine_.getIncumbentPaths().empty()) {
            engine_.setInitialSolution(initial_paths);
        }
        std::string val_err;
        if (!engine_.validateIncumbent(val_err)) {
            summary.initial_plan_found = true;
            summary.final_solution_valid = false;
            summary.validation_error = "Initial incumbent has collisions: " + val_err;
            return summary;
        }
        auto t_val_end = std::chrono::steady_clock::now();
        summary.initial_validation_time_s = std::chrono::duration<double>(t_val_end - t_val_start).count();

        summary.initial_plan_found = true;
        summary.initial_cost = engine_.getIncumbentCost();
        double t_first_sol = summary.initial_planning_time_s + summary.initial_validation_time_s;
        summary.time_of_last_accepted_commit_s = t_first_sol;
        summary.anytime_cost_trajectory.push_back({t_first_sol, summary.initial_cost});

        int decision_id = 0;
        std::vector<double> decision_latencies;
        incompatible_evictions_this_run_ = 0;

        // 2. Controller Loop (Until deadline B exhausted or stopped)
        while (true) {
            auto t_now = std::chrono::steady_clock::now();
            double elapsed_s = std::chrono::duration<double>(t_now - t_global_start).count();
            double b_k = std::max(0.0, global_deadline_B_s_ - elapsed_s);

            // Admissibility boundary: if remaining budget cannot even cover safety reserve, exit
            if (b_k <= overhead_model_.t_reserve_s) {
                break;
            }

            double budget_ratio = std::max(0.0, std::min(1.0, b_k / global_deadline_B_s_));

            // Measure decision pipeline latency
            auto t_dec_start = std::chrono::steady_clock::now();

            ControllerDecision decision;
            decision.decision_id = decision_id++;
            decision.timestamp_s = elapsed_s;
            decision.remaining_budget_s = b_k;

            // Make action and slice selection according to policy
            bool action_selected = selectAction(decision, b_k, budget_ratio);

            auto t_dec_end = std::chrono::steady_clock::now();
            decision.decision_latency_us = std::chrono::duration<double, std::micro>(t_dec_end - t_dec_start).count();
            decision_latencies.push_back(decision.decision_latency_us);
            summary.total_decision_time_s += (decision.decision_latency_us / 1e6);

            if (!action_selected) {
                // No admissible action could be scheduled within remaining budget
                break;
            }

            if (decision.is_fallback) {
                summary.fallback_decisions_count++;
            }
            summary.slice_durations_selected_count[(int)std::round(decision.slice_duration_s * 1000.0)]++;

            // Refresh remaining budget immediately before execution
            auto t_pre_exec = std::chrono::steady_clock::now();
            double b_k_exec = std::max(0.0, global_deadline_B_s_ - std::chrono::duration<double>(t_pre_exec - t_global_start).count());
            if (decision.slice_duration_s + overhead_model_.t_reserve_s > b_k_exec) {
                // Adjust slice if tight, or break if not enough budget
                decision.slice_duration_s = std::max(0.001, b_k_exec - overhead_model_.t_reserve_s);
                if (decision.slice_duration_s < 0.001) break;
            }

            // 3. Action Execution with SearchBudget
            uint64_t expansion_cap = std::max<uint64_t>(50, (uint64_t)(decision.slice_duration_s * 100000.0));
            SearchBudget budget(expansion_cap, decision.slice_duration_s);
            budget.setGlobalDeadline(global_deadline);

            RepairOutcome outcome;
            if (decision.action.kind == ActionKind::RESUME_CHECKPOINT) {
                summary.resumed_actions_count++;
                outcome = engine_.resumeCheckpoint(decision.action.checkpoint_slot, budget, retention_policy_);
                if (outcome.was_partial_recovery) {
                    summary.partial_recovery_count++;
                    summary.total_salvaged_prefix_agents += outcome.salvaged_prefix_len;
                }
            } else {
                summary.fresh_actions_count++;
                outcome = engine_.attemptFreshRepair(decision.action.neighborhood, budget, rng_());
            }

            decision.outcome = outcome;
            summary.decisions.push_back(decision);

            // Update summary statistics
            summary.total_active_search_time_s += outcome.duration_s;
            summary.total_expansions += outcome.total_expansions;
            summary.repeated_expansions += outcome.repeated_expansions;

            if (outcome.type == RepairOutcomeType::COMPLETED_ACCEPTED) {
                summary.accepted_repairs++;
                if (decision.action.kind == ActionKind::RESUME_CHECKPOINT) {
                    summary.accepted_from_resumed++;
                } else {
                    summary.accepted_from_fresh++;
                }
                double t_commit = elapsed_s + outcome.duration_s;
                summary.time_of_last_accepted_commit_s = t_commit;
                summary.anytime_cost_trajectory.push_back({t_commit, engine_.getIncumbentCost()});
            } else if (outcome.type == RepairOutcomeType::COMPLETED_NOT_IMPROVING) {
                summary.non_improving_repairs++;
            } else if (outcome.type == RepairOutcomeType::INTERRUPTED_SLICE) {
                summary.interrupted_slices++;
                if (outcome.interruption.completed_prefix_len > 0) {
                    summary.interrupted_with_nonempty_prefix++;
                } else {
                    summary.interrupted_with_empty_prefix++;
                }
            } else if (outcome.type == RepairOutcomeType::REJECTED_LATE_COMMIT) {
                summary.late_commits_rejected++;
            } else if (outcome.type == RepairOutcomeType::DISCARDED_INCOMPATIBLE) {
                summary.incompatible_checkpoints_discarded++;
            } else if (outcome.type == RepairOutcomeType::STOPPED_DEADLINE) {
                break;
            } else {
                summary.failed_repairs++;
            }

            summary.fixed_bookkeeping_memory_bytes = engine_.getCheckpointStore().getFixedBookkeepingBytes();
            size_t payload_mem = engine_.getCheckpointStore().getRetainedPayloadMemoryBytes();
            if (payload_mem > summary.peak_payload_memory_bytes) {
                summary.peak_payload_memory_bytes = payload_mem;
            }
            size_t total_mem = engine_.getCheckpointStore().getTotalRetainedMemoryBytes();
            if (total_mem > summary.peak_checkpoint_memory_bytes) {
                summary.peak_checkpoint_memory_bytes = total_mem;
            }
        }

        // 4. Final Solution Validation & Accounting
        auto t_global_end = std::chrono::steady_clock::now();
        summary.total_solve_time_s = std::chrono::duration<double>(t_global_end - t_global_start).count();
        summary.deadline_overshoot_s = std::max(0.0, summary.total_solve_time_s - global_deadline_B_s_);

        summary.final_cost = engine_.getIncumbentCost();
        summary.total_improvement = summary.initial_cost - summary.final_cost;
        summary.total_decisions = decision_id;

        // Independent whole-plan collision check of final incumbent
        std::string final_err;
        summary.final_solution_valid = engine_.validateIncumbent(final_err);
        summary.validation_error = final_err;

        // Decision Latency Statistics
        if (!decision_latencies.empty()) {
            std::sort(decision_latencies.begin(), decision_latencies.end());
            summary.median_decision_latency_us = decision_latencies[decision_latencies.size() / 2];
            size_t p95_idx = (size_t)std::min<size_t>(decision_latencies.size() - 1, (size_t)(decision_latencies.size() * 0.95));
            summary.p95_decision_latency_us = decision_latencies[p95_idx];
            summary.max_decision_latency_us = decision_latencies.back();
        }

        summary.stagnant_checkpoints_evicted = stagnant_evictions_this_run_;
        summary.incompatible_checkpoints_discarded += incompatible_evictions_this_run_;

        return summary;
    }

private:
    ResumableRepairEngine& engine_;
    SearchProgressEstimator estimator_;
    EstimatorMode estimator_mode_;
    ControllerPolicy policy_;
    double global_deadline_B_s_;
    std::mt19937 rng_;

    std::vector<double> candidate_slices_s_;
    std::vector<int> candidate_k_values_;
    ProspectiveOverheadModel overhead_model_;
    int max_stagnant_attempts_ = 2; // Bounded response: evict checkpoint after 2 consecutive non-advancing resumptions
    int stagnant_evictions_this_run_ = 0;
    int incompatible_evictions_this_run_ = 0;
    RetentionPolicy retention_policy_ = RetentionPolicy::WHOLE_CHECKPOINT_SELECTIVE; // Policy 0 (Discard), 1 (Whole Selective), or 2 (Partial Recovery)

    // Generate fresh candidate neighborhoods
    std::vector<ActionCandidate> generateFreshCandidates() {
        std::vector<ActionCandidate> candidates;
        int num_agents = engine_.getInstance().getDefaultNumberOfAgents();
        if (num_agents <= 1) return candidates;

        std::vector<int> all_agents(num_agents);
        for (int i = 0; i < num_agents; ++i) all_agents[i] = i;

        for (int k_target : candidate_k_values_) {
            int k = std::min(k_target, std::max(2, num_agents - 1));
            std::shuffle(all_agents.begin(), all_agents.end(), rng_);
            ActionCandidate cand;
            cand.kind = ActionKind::FRESH_REPAIR;
            cand.neighborhood_size = k;
            cand.neighborhood.assign(all_agents.begin(), all_agents.begin() + k);
            cand.completed_prefix_ratio = 0.0;
            candidates.push_back(cand);
        }
        return candidates;
    }

    // Inspect checkpoint store for eligible resume candidates
    std::vector<ActionCandidate> generateResumeCandidates() {
        std::vector<ActionCandidate> candidates;
        if (policy_ == ControllerPolicy::RESTART_ONLY_K0) return candidates;

        int capacity = engine_.getCheckpointStore().getCapacity();
        for (int s = 0; s < capacity; ++s) {
            const auto* cp = engine_.getCheckpointStore().getCheckpoint(s);
            if (!cp) continue;

            // Bounded Response: Stagnation Check
            // If checkpoint has repeatedly failed to advance r_done, evict to prevent infinite stalling
            if (cp->consecutive_stagnant_attempts >= max_stagnant_attempts_) {
                engine_.getCheckpointStore().markDiscarded(s, CheckpointRemoval::Stagnation);
                stagnant_evictions_this_run_++;
                continue;
            }

            // Run conservative compatibility check
            CompatibilityReport rep = engine_.validateCheckpoint(s, retention_policy_);
            if (rep.isCompatible()) {
                ActionCandidate cand;
                cand.kind = ActionKind::RESUME_CHECKPOINT;
                cand.checkpoint_slot = s;
                cand.neighborhood_size = (int)cp->priority_order.size();
                int eff_rdone = rep.isPartial() ? rep.valid_prefix_len : cp->r_done;
                cand.completed_prefix_ratio = (cp->priority_order.empty()) ? 0.0 :
                    ((double)eff_rdone / (double)cp->priority_order.size());
                cand.accumulated_expansions = cp->accumulated_expansions;
                candidates.push_back(cand);
            } else {
                // Incompatible checkpoint: mark discarded so store slot is freed for new checkpoints
                engine_.getCheckpointStore().markDiscarded(s);
                incompatible_evictions_this_run_++;
            }
        }
        return candidates;
    }

    // Main action and slice selector implementing policy logic
    bool selectAction(ControllerDecision& decision, double b_k, double budget_ratio) {
        if (policy_ == ControllerPolicy::RESTART_ONLY_K0) {
            return selectRestartOnly(decision, b_k, budget_ratio);
        } else if (policy_ == ControllerPolicy::FIXED_FIFO_RESUME) {
            return selectFixedFifo(decision, b_k, budget_ratio);
        } else {
            return selectScoreBased(decision, b_k, budget_ratio);
        }
    }

    bool selectRestartOnly(ControllerDecision& decision, double b_k, double budget_ratio) {
        (void)budget_ratio;
        auto fresh_cands = generateFreshCandidates();
        if (fresh_cands.empty()) return false;

        double fixed_slice_s = 0.010; // 10 ms fixed slice
        double overhead_s = overhead_model_.estimateOverhead(ActionKind::FRESH_REPAIR);
        if (fixed_slice_s + overhead_s + overhead_model_.t_reserve_s > b_k) {
            fixed_slice_s = std::max(0.001, b_k - overhead_s - overhead_model_.t_reserve_s);
            if (fixed_slice_s < 0.001) return false;
        }

        decision.action = fresh_cands[0];
        decision.slice_duration_s = fixed_slice_s;
        decision.prospective_overhead_s = overhead_s;
        decision.score = 0.0;
        decision.is_fallback = false;
        return true;
    }

    bool selectFixedFifo(ControllerDecision& decision, double b_k, double budget_ratio) {
        (void)budget_ratio;
        double fixed_slice_s = 0.010; // 10 ms fixed slice
        auto resume_cands = generateResumeCandidates();

        if (!resume_cands.empty()) {
            // FIFO: Oldest checkpoint has lowest slot or checkpoint_id
            decision.action = resume_cands[0];
        } else {
            auto fresh_cands = generateFreshCandidates();
            if (fresh_cands.empty()) return false;
            decision.action = fresh_cands[0];
        }

        double overhead_s = overhead_model_.estimateOverhead(decision.action.kind);
        if (fixed_slice_s + overhead_s + overhead_model_.t_reserve_s > b_k) {
            fixed_slice_s = std::max(0.001, b_k - overhead_s - overhead_model_.t_reserve_s);
            if (fixed_slice_s < 0.001) return false;
        }

        decision.slice_duration_s = fixed_slice_s;
        decision.prospective_overhead_s = overhead_s;
        decision.score = 0.0;
        decision.is_fallback = false;
        return true;
    }

    bool selectScoreBased(ControllerDecision& decision, double b_k, double budget_ratio) {
        auto resume_cands = generateResumeCandidates();
        auto fresh_cands = generateFreshCandidates();

        std::vector<ActionCandidate> all_cands;
        all_cands.insert(all_cands.end(), resume_cands.begin(), resume_cands.end());
        all_cands.insert(all_cands.end(), fresh_cands.begin(), fresh_cands.end());

        if (all_cands.empty()) return false;

        double best_score = -1.0;
        ActionCandidate best_action;
        double best_slice_s = 0.010;
        double best_pred_imp = 0.0;
        double best_pred_time = 0.0;
        double best_overhead = 0.0;
        bool found_admissible = false;

        for (const auto& cand : all_cands) {
            double ovh = overhead_model_.estimateOverhead(cand.kind);

            for (double delta_s : candidate_slices_s_) {
                // Admissibility check: Delta + Overhead_hat(a) + t_reserve <= b_k
                if (delta_s + ovh + overhead_model_.t_reserve_s > b_k) {
                    continue; // Inadmissible under current remaining budget
                }

                // Extract decision-time features
                SearchProgressFeatures feat;
                if (cand.kind == ActionKind::RESUME_CHECKPOINT) {
                    feat = FeatureExtractor::extractResumed(engine_, cand.checkpoint_slot, delta_s, budget_ratio);
                } else {
                    feat = FeatureExtractor::extractFresh(engine_, cand.neighborhood, delta_s, budget_ratio);
                }

                // Query Remaining-Work Estimator
                EstimationResult est = estimator_.predict(feat, estimator_mode_);
                double pred_imp = est.expected_accepted_improvement;
                double pred_time = est.expected_active_search_time_s;

                // Score(a, Delta) = E_hat[Improvement] / (E_hat[min(T_rem, Delta)] + Overhead_hat(a))
                double denom = pred_time + ovh;
                double score = 0.0;
                if (denom > 1e-9 && std::isfinite(denom) && std::isfinite(pred_imp)) {
                    score = pred_imp / denom;
                }

                found_admissible = true;

                if (score > best_score) {
                    best_score = score;
                    best_action = cand;
                    best_slice_s = delta_s;
                    best_pred_imp = pred_imp;
                    best_pred_time = pred_time;
                    best_overhead = ovh;
                }
            }
        }

        if (!found_admissible) {
            // Even smallest slice doesn't fit in remaining budget
            return false;
        }

        // 4. Handle Weak Predictions & Lack of Progress:
        // When all predicted gains are zero, invoke the documented deterministic fallback policy.
        if (best_score <= 1e-9) {
            decision.is_fallback = true;
            decision.fallback_reason = "ALL_ZERO_PREDICTED_IMPROVEMENT";

            // Deterministic Fallback Policy:
            // 1. Prioritize compatible resumed actions with highest completed prefix ratio r_done / |A_c|
            // 2. Allocate the smallest admissible slice to minimize wasted active time
            if (!resume_cands.empty()) {
                std::sort(resume_cands.begin(), resume_cands.end(), [](const ActionCandidate& a, const ActionCandidate& b) {
                    if (std::abs(a.completed_prefix_ratio - b.completed_prefix_ratio) > 1e-5) {
                        return a.completed_prefix_ratio > b.completed_prefix_ratio;
                    }
                    return a.accumulated_expansions < b.accumulated_expansions;
                });
                best_action = resume_cands[0];
                best_slice_s = candidate_slices_s_.front(); // Smallest slice (e.g. 2 ms)
            } else {
                // If no resumed action, pick smallest fresh neighborhood (|A_c|=2)
                best_action = fresh_cands.front();
                best_slice_s = 0.010; // 10 ms default
                if (best_slice_s + overhead_model_.estimateOverhead(ActionKind::FRESH_REPAIR) + overhead_model_.t_reserve_s > b_k) {
                    best_slice_s = candidate_slices_s_.front();
                }
            }
            best_overhead = overhead_model_.estimateOverhead(best_action.kind);
            best_score = 0.0;
        } else {
            decision.is_fallback = false;
        }

        decision.action = best_action;
        decision.slice_duration_s = best_slice_s;
        decision.predicted_improvement = best_pred_imp;
        decision.predicted_active_time_s = best_pred_time;
        decision.prospective_overhead_s = best_overhead;
        decision.score = best_score;
        return true;
    }
};

} // namespace rbl
