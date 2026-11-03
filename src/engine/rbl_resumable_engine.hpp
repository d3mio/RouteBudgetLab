#pragma once
// RouteBudget Lab - Resumable Repair Engine (Phase 3: Macro-Prefix Resumable Repair)
//
// Implementation of Component 1 (O2, FR-02, NFR-02, NFR-03):
// - Cooperative interruption checks inside low-level search and between agents.
// - Macro-prefix checkpointing contract:
//   * Preserves completed prefix paths P_{1..r_done}.
//   * Discards unfinished agent frontier.
//   * Preserves committed incumbent unchanged via transactional isolation.
//   * Restores prefix reservations and deterministic RNG state on resumption.
// - Bounded checkpoint storage (K in {0, 1, 2}) with explicit lifecycle states.
// - Outcome separation: COMPLETED_ACCEPTED, COMPLETED_NOT_IMPROVING, FAILED_NO_PATH,
//   INTERRUPTED_SLICE, STOPPED_DEADLINE.

#include <vector>
#include <string>
#include <memory>
#include <random>
#include <chrono>

#include "Instance.h"
#include "PathTable.h"
#include "ConstraintTable.h"
#include "SingleAgentSolver.h"
#include "SpaceTimeAStar.h"
#include "SIPP.h"

#include "rbl_checkpoint.hpp"
#include "rbl_search_budget.hpp"
#include "rbl_constraint_validator.hpp"
#include "rbl_partial_acceptance.hpp"

namespace rbl {

// Snapshot of a naturally interrupted repair for offline opportunity replay (Task 8)
struct InterruptedRepairSnapshot {
    int snapshot_id = 0;
    int attempt_id = 0;
    bool is_resumed = false;
    uint64_t incumbent_version = 0;
    int64_t incumbent_cost = 0;
    std::vector<Path> incumbent_paths;
    std::vector<int> priority_order;
    std::vector<int> candidate_agents;
    std::vector<Path> candidate_paths;
    double slice_duration_s = 0.0;
    double remaining_budget_s = 0.0;
    uint64_t slice_expansions = 0;
};

struct RecoveryEvent {
    std::string trigger;
    uint64_t checkpoint_id, checkpoint_creation_version, current_version;
    int completed;
    PartialAcceptanceResult result;
    std::chrono::steady_clock::time_point commit_start{}, commit_complete{};
};
struct PublicationRecord {
    std::chrono::steady_clock::time_point start, complete;
    int64_t cost = 0;
};
class ResumableRepairEngine {
public:
    ResumableRepairEngine(const Instance& instance, bool use_sipp = false, int checkpoint_capacity = 2);
    ~ResumableRepairEngine();

    // Set initial incumbent solution paths
    void setInitialSolution(const std::vector<Path>& initial_paths);

    // Incumbent solution access
    const std::vector<Path>& getIncumbentPaths() const { return incumbent_paths; }
    int64_t getIncumbentCost() const { return incumbent_sum_of_costs; }
    uint64_t getIncumbentVersion() const { return incumbent_version; }

    // Checkpoint store access
    BoundedCheckpointStore& getCheckpointStore() { return checkpoint_store; }
    const BoundedCheckpointStore& getCheckpointStore() const { return checkpoint_store; }

    // Attempt a fresh repair on neighborhood N_i with budget
    RepairOutcome attemptFreshRepair(const std::vector<int>& neighborhood,
                                     SearchBudget& budget,
                                     unsigned int rng_seed = 0);

    // Attempt to resume a suspended checkpoint with budget
    // policy: DISCARD_ON_CHANGE (Policy A), WHOLE_CHECKPOINT_SELECTIVE (Policy B), or PARTIAL_PREFIX_RECOVERY (Policy C)
    RepairOutcome resumeCheckpoint(int slot_idx,
                                   SearchBudget& budget,
                                   RetentionPolicy policy = RetentionPolicy::WHOLE_CHECKPOINT_SELECTIVE);
    RepairOutcome resumeCheckpoint(int slot_idx,
                                   SearchBudget& budget,
                                   bool selective_retention);

    // Get per-agent trajectory versions
    const std::vector<uint64_t>& getAgentVersions() const { return agent_trajectory_versions; }

    // Side-effect-free compatibility check of a stored checkpoint against the CURRENT incumbent.
    CompatibilityReport validateCheckpoint(int slot_idx, RetentionPolicy policy = RetentionPolicy::WHOLE_CHECKPOINT_SELECTIVE) const;
    CompatibilityReport validateCheckpoint(int slot_idx, bool selective_retention) const;

    // Report produced by the most recent resumeCheckpoint() call (stage reached, verdict, latency).
    const CompatibilityReport& getLastCompatibilityReport() const { return last_compat_report; }

    // Full plan (all agents) of the most recent repair whose neighbourhood was completely routed,
    // whether or not it was accepted. Empty if no repair completed yet.
    const std::vector<Path>& getLastCompletedCandidate() const { return last_completed_candidate; }

    // Read-only reservation table (reflects the committed incumbent between repair calls).
    const PathTable& getPathTable() const { return path_table; }
    const Instance& getInstance() const { return instance; }

    // Diagnostic & Controlled Perturbation / Testing API only:
    // Directly commit a valid, collision-free trajectory for an external agent (used in E4/E8 perturbation fixtures).
    // Verifies start/goal, valid moves, and absence of collisions with current incumbent.
    // Production planner execution strictly uses attemptFreshRepair() and resumeCheckpoint(),
    // which enforce strict cost improvement (new_cost < old_cost) and candidate collision-checking before commitment.
    // If allow_worsening is false, worsening paths (new_cost >= old_cost) are rejected.
    bool commitExternalTrajectory(int agent_id, const Path& new_path, bool allow_worsening = true);

    // Testing & Controlled Experimentation only:
    // Update an incumbent agent trajectory and its version in the path_table and incumbent state,
    // preserving stored checkpoints for perturbation testing.
    void setIncumbentPathForTest(int agent_id, const Path& new_path);

    // Validation: checks whether current incumbent paths are collision-free internally
    bool validateIncumbent(std::string& error_msg) const;

    // Reset statistics
    void resetStats();

    // Cumulative statistics
    int getAcceptedRepairs() const { return total_accepted_repairs; }
    int getNonImprovingRepairs() const { return total_non_improving_repairs; }
    int getFailedRepairs() const { return total_failed_repairs; }
    int getInterruptedSlices() const { return total_interrupted_slices; }
    uint64_t getTotalExpansions() const { return total_expansions; }
    uint64_t getRepeatedExpansions() const { return total_repeated_expansions; }

    // Partial Acceptance Feature Controls & Policy
    void setPartialAcceptanceEnabled(bool enable) { enable_partial_acceptance = enable; }
    bool isPartialAcceptanceEnabled() const { return enable_partial_acceptance; }
    void setPartialAcceptancePolicy(PartialAcceptancePolicy policy) {
        partial_acceptance_policy = policy;
        if (policy == PartialAcceptancePolicy::OFF) {
            enable_partial_acceptance = false;
            selective_recovery_policy = SelectiveRecoveryPolicy::RESUMPTION_NO_PA;
        } else if (selective_recovery_policy == SelectiveRecoveryPolicy::RESUMPTION_NO_PA) {
            enable_partial_acceptance = true;
            selective_recovery_policy = SelectiveRecoveryPolicy::PA_EVERY_INTERRUPTION_FILTERED;
        }
    }
    PartialAcceptancePolicy getPartialAcceptancePolicy() const { return partial_acceptance_policy; }

    // Selective Recovery Policy Controls (Claude Prompt Section 7)
    void setSelectiveRecoveryPolicy(SelectiveRecoveryPolicy policy) {
        selective_recovery_policy = policy;
        enable_partial_acceptance = (policy != SelectiveRecoveryPolicy::RESUMPTION_NO_PA);
        if (enable_partial_acceptance && partial_acceptance_policy == PartialAcceptancePolicy::OFF) {
            partial_acceptance_policy = PartialAcceptancePolicy::GREEDY_DEPENDENCY;
        }
    }
    SelectiveRecoveryPolicy getSelectiveRecoveryPolicy() const { return selective_recovery_policy; }

    const PartialAcceptanceResult& getLastPartialAcceptanceResult() const { return last_partial_acceptance_result; }
    const std::vector<PartialAcceptanceResult>& getPartialAcceptanceHistory() const { return partial_acceptance_history; }

    // Atomic Batch Commit for Partial Acceptance:
    // Safely validates candidate full plan, verifies strict cost improvement,
    // and transactionally updates incumbent paths, versions, and reservations.
    bool commitPartialAcceptance(const std::vector<int>& selected_agents,
                                 const std::vector<Path>& replacement_paths,
                                 uint64_t expected_incumbent_version,
                                 PartialAcceptanceResult& out_result,
                                 SearchBudget* budget = nullptr);

    // Evaluate partial acceptance over candidate replacements without modifying incumbent
    PartialAcceptanceResult evaluatePartialAcceptance(
        const std::vector<int>& candidate_agents,
        const std::vector<Path>& candidate_replacements,
        SearchBudget* budget = nullptr);

    // Eviction-triggered recovery hook (Section 8)
    bool attemptEvictionRecovery(int victim_slot, SearchBudget& budget);

    // Terminal reserve recovery hook (Section 9)
    bool attemptTerminalRecovery(SearchBudget& budget);

    // Partial acceptance cumulative statistics
    int getPartialAcceptanceAttempts() const { return total_partial_acceptance_attempts; }
    int getPartialAcceptancePositiveOpportunities() const { return total_partial_acceptance_positive_opps; }
    int getPartialAcceptanceCommitted() const { return total_partial_acceptance_committed; }
    int getPartialAcceptanceBudgetSkipped() const { return total_partial_acceptance_budget_skipped; }
    int64_t getPartialAcceptanceTotalGain() const { return total_partial_acceptance_gain; }
    double getPartialAcceptanceTotalTimeUs() const { return total_partial_acceptance_time_us; }

    // Filter cumulative statistics (Section 5)
    int getFilterEvaluations() const { return total_filter_evaluations; }
    int getFilterSkips() const { return total_filter_skips; }
    double getFilterTotalTimeUs() const { return total_filter_time_us; }
    double getExtractionTotalTimeUs() const { return total_extraction_time_us; }

    // Eviction recovery cumulative statistics (Section 8)
    int getEvictionRecoveryAttempts() const { return total_eviction_pa_attempts; }
    int getEvictionRecoveryFilterSkips() const { return total_eviction_pa_filter_skips; }
    int getEvictionRecoveryCommitted() const { return total_eviction_pa_committed; }
    int64_t getEvictionRecoveryTotalGain() const { return total_eviction_pa_gain; }
    double getEvictionRecoveryTotalTimeUs() const { return total_eviction_pa_time_us; }

    // Terminal recovery cumulative statistics (Section 9)
    int getTerminalRecoveryCalls() const { return total_terminal_recovery_calls; }
    int getTerminalRecoveryExamined() const { return total_terminal_pa_examined; }
    int getTerminalRecoveryFilterSkips() const { return total_terminal_pa_filter_skips; }
    int getTerminalRecoveryCommitted() const { return total_terminal_pa_committed; }
    int64_t getTerminalRecoveryTotalGain() const { return total_terminal_pa_gain; }
    double getTerminalRecoveryTotalTimeUs() const { return total_terminal_pa_time_us; }

    // Offline snapshot capture controls (Task 8)
    void setSnapshotCaptureEnabled(bool enable) { enable_snapshot_capture = enable; }
    bool isSnapshotCaptureEnabled() const { return enable_snapshot_capture; }
    const std::vector<InterruptedRepairSnapshot>& getCapturedSnapshots() const { return captured_snapshots; }
    int getTotalInterruptionsObserved() const { return total_interruptions_observed; }
    int getInterruptionsWithCompletedPrefix() const { return total_interruptions_with_completed_prefix; }

    void setPublicationFailureStageForTest(int stage) { publication_failure_stage = stage; }
    std::pair<uint64_t, int64_t> getValidationCacheForTest() const {
        return {last_validated_incumbent_version, last_validated_incumbent_cost};
    }
    void setStoredCostForTest(int64_t cost) { incumbent_sum_of_costs = cost; }
    const std::vector<RecoveryEvent>& getRecoveryEvents() const { return recovery_events; }
    const std::vector<PublicationRecord>& getPublicationRecords() const { return publications; }
    bool isUsingSipp() const { return use_sipp; }

private:
    std::vector<RecoveryEvent> recovery_events;
    void recordRecovery(const std::string& trigger, uint64_t id, uint64_t creation_version,
                        int completed, const PartialAcceptanceResult& result);
    bool eviction_recovery_active = false;
    std::vector<PublicationRecord> publications;
    int publication_failure_stage = 0;
    const Instance& instance;
    bool use_sipp = false;
    int num_agents = 0;

    // Incumbent state
    std::vector<Path> incumbent_paths;
    int64_t incumbent_sum_of_costs = 0;
    uint64_t incumbent_version = 1;
    mutable uint64_t last_validated_incumbent_version = 0;
    mutable int64_t last_validated_incumbent_cost = -1;
    std::vector<uint64_t> agent_trajectory_versions;

    // Global collision and path table for incumbent
    PathTable path_table;

    // Low-level single agent solvers (one per agent)
    std::vector<SingleAgentSolver*> single_agent_solvers;

    // Bounded checkpoint store (K in {0, 1, 2})
    BoundedCheckpointStore checkpoint_store;

    // Unique checkpoint ID counter
    uint64_t next_checkpoint_id = 1;

    CompatibilityReport last_compat_report;
    std::vector<Path> last_completed_candidate;

    // Independent-of-search whole-plan collision check used before any commit.
    bool planIsCollisionFree(const std::vector<Path>& plan) const;

    // Statistics
    int total_accepted_repairs = 0;
    int total_non_improving_repairs = 0;
    int total_failed_repairs = 0;
    int total_interrupted_slices = 0;
    uint64_t total_expansions = 0;
    uint64_t total_repeated_expansions = 0;

    // Internal low-level search with budget check
    Path findPathWithBudget(int agent_id,
                            const ConstraintTable& ct,
                            SearchBudget& budget,
                            unsigned int rng_seed);

    // All allocations and reservation construction precede nonthrowing publication.
    bool publishCandidate(std::vector<Path> candidate, const std::vector<int>& changed,
                          uint64_t expected_version, bool require_improvement,
                          SearchBudget* budget = nullptr);

    // Helper: compute sum of costs for a vector of paths
    static int64_t computeCost(const std::vector<Path>& paths);

    // Partial acceptance state
    bool enable_partial_acceptance = false;
    PartialAcceptancePolicy partial_acceptance_policy = PartialAcceptancePolicy::OFF;
    SelectiveRecoveryPolicy selective_recovery_policy = SelectiveRecoveryPolicy::RESUMPTION_NO_PA;
    PartialAcceptanceResult last_partial_acceptance_result;
    std::vector<PartialAcceptanceResult> partial_acceptance_history;
    int total_partial_acceptance_attempts = 0;
    int total_partial_acceptance_positive_opps = 0;
    int total_partial_acceptance_committed = 0;
    int total_partial_acceptance_budget_skipped = 0;
    int64_t total_partial_acceptance_gain = 0;
    double total_partial_acceptance_time_us = 0.0;

    // Filter telemetry (Section 5)
    int total_filter_evaluations = 0;
    int total_filter_skips = 0;
    double total_filter_time_us = 0.0;
    double total_extraction_time_us = 0.0;

    // Eviction recovery telemetry (Section 8)
    int total_eviction_pa_attempts = 0;
    int total_eviction_pa_filter_skips = 0;
    int total_eviction_pa_committed = 0;
    int64_t total_eviction_pa_gain = 0;
    double total_eviction_pa_time_us = 0.0;

    // Terminal recovery telemetry (Section 9)
    int total_terminal_recovery_calls = 0;
    int total_terminal_pa_examined = 0;
    int total_terminal_pa_filter_skips = 0;
    int total_terminal_pa_committed = 0;
    int64_t total_terminal_pa_gain = 0;
    double total_terminal_pa_time_us = 0.0;

    // Offline snapshot capture state
    bool enable_snapshot_capture = false;
    std::vector<InterruptedRepairSnapshot> captured_snapshots;
    int total_interruptions_observed = 0;
    int total_interruptions_with_completed_prefix = 0;
    int total_repair_attempts_counter = 0;

    // Helper: process interrupted partial acceptance transaction
    bool processInterruptedPartialAcceptance(const std::vector<int>& candidate_agents,
                                            const std::vector<Path>& candidate_paths,
                                            SearchBudget& budget,
                                            RepairOutcome& outcome, uint64_t checkpoint_id, uint64_t creation_version);
};

} // namespace rbl
