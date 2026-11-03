#pragma once
// RouteBudget Lab - Maximum-Gain Partial Acceptance of Interrupted MAPF-LNS Repairs
//
// Formulation & Optimization:
// - Formulates partial acceptance of complete replacement paths Q_i from an
//   interrupted neighborhood repair as a Maximum-Weight Closure Problem.
// - Solves the exact closure problem via a Minimum (s,t)-Cut reduction using
//   Dinic's maximum flow algorithm on a constructed dependency network with
//   capacity M = 1 + sum(|w_i|) using checked 64-bit integer arithmetic.
// - Provides a simpler exact nonnegative-case reference (backward exclusion propagation
//   from forbidden nodes), a transitive dependency-aware greedy baseline, and an
//   independent path-mixture 2^r exhaustive oracle.
// - Enforces safe lifecycle: validation against all incumbent agents, independent
//   whole-plan collision verification, version consistency, and atomic commitment.

#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <chrono>
#include <algorithm>
#include <limits>
#include <sstream>
#include <iostream>

#include "Instance.h"
#include "rbl_cost.hpp"
#include "rbl_search_budget.hpp"

namespace rbl {

// Status of partial acceptance evaluation or commit
enum class PartialAcceptanceStatus {
    DISABLED,             // Feature flag disabled; no action taken
    INVALID_INPUT,        // Precondition or capacity/overflow violation
    NO_CANDIDATES,        // No completed replacement paths available (R is empty)
    NO_GAIN,              // Maximum feasible gain <= 0; incumbent retained
    BUDGET_SKIPPED,       // Insufficient budget remaining or flow interrupted by deadline
    VERSION_CHANGED,      // Incumbent version changed during or before commit; transaction aborted
    OPTIMAL_SELECTION,    // Feasible subset found with strictly positive gain (ready to commit)
    COMMITTED,            // Transaction successfully validated and applied to incumbent
    VALIDATION_FAILED     // Candidate full plan failed independent collision or kinematic check
};

inline const char* toString(PartialAcceptanceStatus s) {
    switch (s) {
        case PartialAcceptanceStatus::DISABLED: return "DISABLED";
        case PartialAcceptanceStatus::INVALID_INPUT: return "INVALID_INPUT";
        case PartialAcceptanceStatus::NO_CANDIDATES: return "NO_CANDIDATES";
        case PartialAcceptanceStatus::NO_GAIN: return "NO_GAIN";
        case PartialAcceptanceStatus::BUDGET_SKIPPED: return "BUDGET_SKIPPED";
        case PartialAcceptanceStatus::VERSION_CHANGED: return "VERSION_CHANGED";
        case PartialAcceptanceStatus::OPTIMAL_SELECTION: return "OPTIMAL_SELECTION";
        case PartialAcceptanceStatus::COMMITTED: return "COMMITTED";
        case PartialAcceptanceStatus::VALIDATION_FAILED: return "VALIDATION_FAILED";
        default: return "UNKNOWN";
    }
}

inline std::ostream& operator<<(std::ostream& os, PartialAcceptanceStatus s) {
    return os << toString(s);
}

// Policy governing partial acceptance selection
enum class PartialAcceptancePolicy {
    OFF,                  // Disabled (default)
    EXACT_CLOSURE,        // Exact min-cut / maximum-weight closure selection
    GREEDY_DEPENDENCY     // Transitive dependency-aware greedy baseline
};

inline const char* toString(PartialAcceptancePolicy p) {
    switch (p) {
        case PartialAcceptancePolicy::OFF: return "OFF";
        case PartialAcceptancePolicy::EXACT_CLOSURE: return "EXACT_CLOSURE";
        case PartialAcceptancePolicy::GREEDY_DEPENDENCY: return "GREEDY_DEPENDENCY";
        default: return "UNKNOWN";
    }
}

inline std::ostream& operator<<(std::ostream& os, PartialAcceptancePolicy p) {
    return os << toString(p);
}

// Policy governing selective recovery triggers (Claude Prompt Section 7)
enum class SelectiveRecoveryPolicy {
    RESUMPTION_NO_PA,                // Normal resumption baseline, no partial acceptance
    PA_EVERY_INTERRUPTION_FILTERED, // Filtered recovery evaluated on every slice interruption
    PA_EVICTION_ONLY,               // Normal resumption; recovery attempted only upon natural checkpoint eviction
    PA_TERMINAL_ONLY,               // Normal resumption; recovery attempted only during terminal reserve before deadline
    PA_EVICTION_AND_TERMINAL        // Normal resumption; recovery attempted on eviction and during terminal reserve
};

inline const char* toString(SelectiveRecoveryPolicy p) {
    switch (p) {
        case SelectiveRecoveryPolicy::RESUMPTION_NO_PA: return "RESUMPTION_NO_PA";
        case SelectiveRecoveryPolicy::PA_EVERY_INTERRUPTION_FILTERED: return "PA_EVERY_INTERRUPTION_FILTERED";
        case SelectiveRecoveryPolicy::PA_EVICTION_ONLY: return "PA_EVICTION_ONLY";
        case SelectiveRecoveryPolicy::PA_TERMINAL_ONLY: return "PA_TERMINAL_ONLY";
        case SelectiveRecoveryPolicy::PA_EVICTION_AND_TERMINAL: return "PA_EVICTION_AND_TERMINAL";
        default: return "UNKNOWN";
    }
}

inline std::ostream& operator<<(std::ostream& os, SelectiveRecoveryPolicy p) {
    return os << toString(p);
}

inline SelectiveRecoveryPolicy parseSelectiveRecoveryPolicy(const std::string& str) {
    if (str == "RESUMPTION_NO_PA" || str == "DISCARD_NO_PA") {
        return SelectiveRecoveryPolicy::RESUMPTION_NO_PA;
    } else if (str == "PA_EVERY_INTERRUPTION_FILTERED" || str == "EXACT_PARTIAL_ACCEPTANCE" || str == "GREEDY_PARTIAL_ACCEPTANCE") {
        return SelectiveRecoveryPolicy::PA_EVERY_INTERRUPTION_FILTERED;
    } else if (str == "PA_EVICTION_ONLY") {
        return SelectiveRecoveryPolicy::PA_EVICTION_ONLY;
    } else if (str == "PA_TERMINAL_ONLY") {
        return SelectiveRecoveryPolicy::PA_TERMINAL_ONLY;
    } else if (str == "PA_EVICTION_AND_TERMINAL") {
        return SelectiveRecoveryPolicy::PA_EVICTION_AND_TERMINAL;
    }
    return SelectiveRecoveryPolicy::RESUMPTION_NO_PA;
}

// Representation of one agent replacement candidate
struct ReplacementCandidate {
    int agent_id = -1;
    Path replacement_path; // Q_i
    Path old_path;         // P_i
    int64_t cost_old = 0;      // c(P_i)
    int64_t cost_new = 0;      // c(Q_i)
    int64_t weight = 0;    // w_i = c(P_i) - c(Q_i)
};

// Dependency graph capturing implication constraints
struct DependencyGraph {
    int num_candidates = 0;
    std::vector<int> candidate_agents;               // mapping node index k -> agent_id
    std::unordered_map<int, int> agent_to_candidate; // mapping agent_id -> node index k
    std::vector<int64_t> weights;                    // w_k
    std::vector<std::vector<int>> adj;               // adj[u] contains v where u -> v (x_u <= x_v)
    std::vector<std::pair<int,int>> incompatibilities; // replacement pairs that cannot coexist
    bool cancelled = false;
    std::vector<bool> forbidden;                     // forbidden[u] is true if x_u must be 0
    std::vector<bool> transitively_excluded;         // true if u is forbidden or transitively depends on forbidden
    std::vector<std::string> conflict_reasons;       // human-readable reasons for diagnostics
    int incompatibility_count = 0;
    int dependency_arc_count = 0;
    int forbidden_count = 0;
    int transitively_excluded_count = 0;             // nodes not directly forbidden that depend on forbidden nodes
    int positive_weight_count = 0;
    int zero_weight_count = 0;
    int negative_weight_count = 0;
    int feasible_negative_weight_count = 0;          // non-excluded nodes with w_k < 0
};

// Result contract for partial acceptance evaluation and commitment
struct PartialAcceptanceResult {
    PartialAcceptanceStatus status = PartialAcceptanceStatus::DISABLED;
    PartialAcceptancePolicy policy = PartialAcceptancePolicy::OFF;
    uint64_t source_incumbent_version = 0;
    std::vector<int> selected_agent_ids;
    std::vector<Path> selected_paths;
    int64_t predicted_gain = 0;
    int64_t verified_gain = 0;
    int candidate_count = 0;
    int positive_weight_count = 0;
    int zero_weight_count = 0;
    int negative_weight_count = 0;
    int forbidden_count = 0;
    int transitively_excluded_count = 0;
    int feasible_negative_weight_count = 0;
    int selected_positive_weight_count = 0;
    int selected_zero_weight_count = 0;
    int selected_negative_weight_count = 0;
    int incompatibility_count = 0;
    int dependency_arc_count = 0;
    bool used_nonnegative_fast_path = false;
    bool filter_skipped = false;
    bool bound_computed = false;
    int64_t optimistic_upper_bound_U = 0;

    // High-resolution timing breakdown in microseconds
    double prep_time_us = 0.0;
    double extraction_time_us = 0.0;
    double filter_time_us = 0.0;
    double graph_build_time_us = 0.0;
    double selection_time_us = 0.0;
    double validation_time_us = 0.0;
    double commit_time_us = 0.0;
    double total_time_us = 0.0;

    std::string detail;
};

// Algorithmic Core for Partial Acceptance
class PartialAcceptanceOptimizer {
public:
    // Compute authoritatively defined path arrival cost (arrival timestep at goal).
    // Avoids counting terminal padding waits as extra cost.
    static int64_t computeArrivalCost(const Path& path, int goal_location);

    // Compute cheap optimistic upper bound U = sum(max(w_i, 0)) using checked arithmetic (Section 5).
    // Returns false if arithmetic overflow occurs.
    static bool computeOptimisticUpperBound(const std::vector<ReplacementCandidate>& candidates,
                                            int64_t& out_u,
                                            bool& out_overflow);

    // Validate kinematic movement rules for a path on a given map instance.
    static bool checkPathKinematics(const Path& path, int start_loc, int goal_loc, const Instance& instance, std::string& err, SearchBudget* budget = nullptr, RecoveryStage stage = RecoveryStage::Extraction);

    // Check pairwise collision-freedom among candidate replacements (Assumption 3).
    static bool checkPairwiseCandidateCollisions(const std::vector<ReplacementCandidate>& candidates, std::string& err);

    // Check if path A conflicts with path B (vertex collision, edge swap, or persistent goal occupancy).
    static bool checkTrajectoryConflict(const Path& path_a, const Path& path_b, std::string* reason = nullptr, SearchBudget* budget = nullptr, RecoveryStage stage = RecoveryStage::Conflict);

    // Independently validate a complete mixed plan (all agents) without using DependencyGraph,
    // and compute its authoritative arrival-cost Sum of Costs.
    static bool validateCompletePlanIndependently(const std::vector<Path>& plan,
                                                  const Instance& instance,
                                                  int64_t& out_soc,
                                                  std::string& out_err, SearchBudget* budget = nullptr);

    // Populate backward exclusion and weight-sign telemetry on a DependencyGraph.
    static void computeGraphTelemetry(DependencyGraph& graph);

    // Build the dependency graph over candidate replacements against all incumbent paths.
    // Compares each Q_i (i in R) against each P_j (j in A, j != i):
    // - If conflict and j in R: dependency arc i -> j (x_i <= x_j)
    // - If conflict and j not in R: forbidden constraint (x_i = 0)
    static DependencyGraph buildDependencyGraph(const std::vector<ReplacementCandidate>& candidates,
                                                const std::vector<Path>& incumbent_paths,
                                                const Instance& instance, SearchBudget* budget = nullptr);

    // Solve exact maximum-weight closure using Dinic's min-cut reduction with checked arithmetic.
    static PartialAcceptanceResult solveExactClosure(const DependencyGraph& graph,
                                                     const std::vector<ReplacementCandidate>& candidates,
                                                     uint64_t incumbent_version,
                                                     SearchBudget* budget = nullptr);

    // Simple exact reference for the nonnegative selectable-weight case:
    // Marks forbidden nodes, propagates exclusion backward to every node depending on an excluded node,
    // and selects all remaining selectable nodes when all remaining selectable weights are >= 0.
    static PartialAcceptanceResult solveNonnegativeClosureReference(const DependencyGraph& graph,
                                                                    const std::vector<ReplacementCandidate>& candidates,
                                                                    uint64_t incumbent_version,
                                                                    SearchBudget* budget = nullptr);

    // Solve using transitive dependency-aware greedy baseline.
    static PartialAcceptanceResult solveGreedyDependencyAware(const DependencyGraph& graph,
                                                             const std::vector<ReplacementCandidate>& candidates,
                                                             uint64_t incumbent_version,
                                                             SearchBudget* budget = nullptr);

    // Independent Exhaustive 2^r Oracle (Task 2):
    // Enumerates all 2^r subsets, constructs the complete path mixture of old and replacement paths,
    // validates movement validity, starts/goals, vertex collisions, edge swaps, and permanent goal occupancy
    // independently of DependencyGraph, and recomputes Sum of Costs using computeArrivalCost.
    static PartialAcceptanceResult solveExhaustiveOracle(const std::vector<ReplacementCandidate>& candidates,
                                                        const std::vector<Path>& incumbent_paths,
                                                        const Instance& instance,
                                                        uint64_t incumbent_version, SearchBudget* budget = nullptr);

    // Graph-level 2^r closure enumerator (for synthetic abstract graph tests where path fixtures are not used).
    static PartialAcceptanceResult solveExhaustiveGraphClosureOracle(const DependencyGraph& graph,
                                                                     const std::vector<ReplacementCandidate>& candidates,
                                                                     uint64_t incumbent_version, SearchBudget* budget = nullptr);
};

} // namespace rbl
