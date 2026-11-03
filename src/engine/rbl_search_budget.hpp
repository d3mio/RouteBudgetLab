#pragma once
// RouteBudget Lab - Cooperative Search Budget & Outcome Tracking (O1, WP1, FR-02)
//
// Provides:
// 1. SearchBudget: Cooperative interruption accounting inside low-level search and between agents.
// 2. RepairOutcome: Five distinct, mutually-exclusive repair outcomes:
//    - COMPLETED_ACCEPTED
//    - COMPLETED_NOT_IMPROVING
//    - FAILED_NO_PATH
//    - INTERRUPTED_SLICE (with prefix metrics: |N|, r_done, current-agent work, reusable prefix boolean)
//    - STOPPED_DEADLINE

#include <chrono>
#include <cstdint>
#include <string>

namespace rbl {

enum class RepairOutcomeType {
    COMPLETED_ACCEPTED,      // All agents routed; sum of costs strictly improved
    COMPLETED_NOT_IMPROVING, // All agents routed or cost-pruned; cost >= old_cost
    FAILED_NO_PATH,          // Search failed to find a feasible path for an agent
    INTERRUPTED_SLICE,       // Active slice budget expired during repair
    STOPPED_DEADLINE,        // Global solver deadline expired
    DISCARDED_INCOMPATIBLE,  // Resume refused: checkpoint failed the compatibility validator (Phase 4)
    REJECTED_INVALID,        // Completed candidate failed final whole-plan validation; not committed
    REJECTED_LATE_COMMIT     // Validation finished after global deadline; commit rejected to preserve deadline
};

inline const char* toString(RepairOutcomeType o) {
    switch (o) {
        case RepairOutcomeType::COMPLETED_ACCEPTED: return "COMPLETED_ACCEPTED";
        case RepairOutcomeType::COMPLETED_NOT_IMPROVING: return "COMPLETED_NOT_IMPROVING";
        case RepairOutcomeType::FAILED_NO_PATH: return "FAILED_NO_PATH";
        case RepairOutcomeType::INTERRUPTED_SLICE: return "INTERRUPTED_SLICE";
        case RepairOutcomeType::STOPPED_DEADLINE: return "STOPPED_DEADLINE";
        case RepairOutcomeType::DISCARDED_INCOMPATIBLE: return "DISCARDED_INCOMPATIBLE";
        case RepairOutcomeType::REJECTED_INVALID: return "REJECTED_INVALID";
        case RepairOutcomeType::REJECTED_LATE_COMMIT: return "REJECTED_LATE_COMMIT";
        default: return "UNKNOWN";
    }
}

inline std::ostream& operator<<(std::ostream& os, RepairOutcomeType o) {
    return os << toString(o);
}

struct InterruptionRecord {
    int neighborhood_size = 0;
    int completed_prefix_len = 0; // r_done
    int unfinished_agent_id = -1;
    uint64_t current_agent_expansions = 0;
    double current_agent_time_s = 0.0;
    bool has_reusable_prefix = false; // true if r_done > 0
};

struct RepairOutcome {
    RepairOutcomeType type = RepairOutcomeType::FAILED_NO_PATH;
    int neighborhood_size = 0;
    int64_t old_cost = 0;
    int64_t new_cost = 0;
    int64_t cost_delta = 0; // new_cost - old_cost (negative is improvement)
    double duration_s = 0.0;
    uint64_t total_expansions = 0;
    uint64_t repeated_expansions = 0; // Work repeated upon restarting unfinished agents
    bool was_partial_recovery = false; // True if checkpoint was resumed after partial prefix truncation
    int salvaged_prefix_len = 0;      // Number of prefix paths preserved after truncation
    int original_prefix_len = 0;      // Number of prefix paths before truncation
    InterruptionRecord interruption;
};

// Cooperative Search Budget
// Supports deterministic expansion cutoffs (for E1 equivalence tests)
// and wall-clock monotonic time cutoffs (for real-time performance evaluation).
enum class RecoveryStage { Extraction, Dependency, Conflict, Selection, Validation,
                           Preparation, BeforePublication, AfterPublication };

class SearchBudget {
public:
    uint64_t max_expansions = UINT64_MAX;
    double max_time_seconds = 1e9;
    std::chrono::steady_clock::time_point start_time;
    uint64_t current_expansions = 0;
    int max_agents = -1; // -1 means no limit on completed agents
    std::chrono::steady_clock::time_point global_deadline_time;
    bool has_global_deadline = false;
    bool interrupted = false;
    bool deadline_exceeded = false;
    double partial_acceptance_reserve_s = 0.0002; // 200 us reserve before starting online partial acceptance
    int64_t max_flow_steps = -1;                  // -1 means no step limit on flow solver
    int64_t current_flow_steps = 0;

    // Deterministic deadline injection; disabled during production runs.
    int injected_stage = -1;
    int stage_polls_remaining = -1;
    void expireAtStageForTest(RecoveryStage stage, int successful_polls = 0) {
        injected_stage = static_cast<int>(stage);
        stage_polls_remaining = successful_polls;
    }
    bool pollStage(RecoveryStage stage) {
        if (injected_stage == static_cast<int>(stage) && stage_polls_remaining-- == 0)
            deadline_exceeded = true;
        return !isGlobalBudgetExhausted(0.0);
    }

    SearchBudget() {
        start_time = std::chrono::steady_clock::now();
    }

    SearchBudget(uint64_t max_exp, double max_time_s)
        : max_expansions(max_exp), max_time_seconds(max_time_s) {
        start_time = std::chrono::steady_clock::now();
    }

    void setGlobalDeadline(std::chrono::steady_clock::time_point deadline) {
        global_deadline_time = deadline;
        has_global_deadline = true;
    }

    void setMaxAgents(int m) {
        max_agents = m;
    }

    void setMaxFlowSteps(int64_t steps) {
        max_flow_steps = steps;
        current_flow_steps = 0;
    }

    inline bool isGlobalDeadlineExceeded() const {
        if (deadline_exceeded) return true;
        if (!has_global_deadline) return false;
        return std::chrono::steady_clock::now() >= global_deadline_time;
    }

    inline double getRemainingGlobalSeconds() const {
        if (deadline_exceeded) return 0.0;
        if (has_global_deadline) {
            auto now = std::chrono::steady_clock::now();
            return std::chrono::duration<double>(global_deadline_time - now).count();
        }
        if (max_time_seconds < 1e8) {
            return max_time_seconds - getElapsedSeconds();
        }
        return 1e9;
    }

    // Checks whether global deadline (plus optional safety reserve) is exhausted.
    // Note: When has_global_deadline is true, slice expiration (max_time_seconds) does NOT
    // mean the global deadline is exhausted; only global_deadline_time governs the global budget.
    inline bool isGlobalBudgetExhausted(double reserve_s = 0.0) const {
        if (deadline_exceeded) return true;
        if (has_global_deadline) {
            return getRemainingGlobalSeconds() <= reserve_s;
        }
        if (max_time_seconds < 1e8) {
            return (getElapsedSeconds() + reserve_s) >= max_time_seconds;
        }
        return false;
    }

    // Cooperative check inside flow/selection loops
    inline bool checkFlowStep() {
        current_flow_steps++;
        if (max_flow_steps >= 0 && current_flow_steps > max_flow_steps) {
            interrupted = true;
            deadline_exceeded = true;
            return false;
        }
        if (isGlobalBudgetExhausted(0.0)) {
            interrupted = true;
            deadline_exceeded = true;
            return false;
        }
        return true;
    }

    void reset(uint64_t max_exp, double max_time_s) {
        max_expansions = max_exp;
        max_time_seconds = max_time_s;
        max_agents = -1;
        max_flow_steps = -1;
        current_flow_steps = 0;
        current_expansions = 0;
        interrupted = false;
        deadline_exceeded = false;
        start_time = std::chrono::steady_clock::now();
    }

    // Cooperative check called inside search loops
    // Returns true if search is allowed to proceed, false if cutoff reached.
    inline bool checkNodeExpansion() {
        current_expansions++;
        if (current_expansions > max_expansions) {
            interrupted = true;
            return false;
        }

        // Periodically check clock every 64 expansions to minimize chrono overhead
        if ((current_expansions & 63) == 0) {
            if (isGlobalDeadlineExceeded()) {
                interrupted = true;
                deadline_exceeded = true;
                return false;
            }
            if (max_time_seconds < 1e8) {
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - start_time).count();
                if (elapsed >= max_time_seconds) {
                    interrupted = true;
                    return false;
                }
            }
        }
        return true;
    }

    // Explicit check between agents
    inline bool checkAgentBoundary(int completed_agents = -1) {
        if (max_agents >= 0 && completed_agents >= 0 && completed_agents >= max_agents) {
            interrupted = true;
            return false;
        }
        if (current_expansions >= max_expansions) {
            interrupted = true;
            return false;
        }
        if (isGlobalDeadlineExceeded()) {
            interrupted = true;
            deadline_exceeded = true;
            return false;
        }
        if (max_time_seconds < 1e8) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start_time).count();
            if (elapsed >= max_time_seconds) {
                interrupted = true;
                return false;
            }
        }
        return true;
    }

    double getElapsedSeconds() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - start_time).count();
    }
};

} // namespace rbl
