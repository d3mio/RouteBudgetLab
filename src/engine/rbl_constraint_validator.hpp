#pragma once
// RouteBudget Lab - Two-Stage Conservative Constraint Compatibility Validator (Component 2: O3, FR-03, RQ2, E4)
//
// Exact Two-Stage Validation Contract:
// Stage 1: Fast O(|A|) version-counter check. If no external agent trajectories have changed,
//          the checkpoint is immediately compatible with sub-microsecond latency.
// Stage 2: Spatio-temporal reservation footprint and constraint check:
//          - Detects overlapping neighborhood repairs (discards if any agent in N_i was modified in incumbent).
//          - Checks retained prefix paths P_{1..r_done} against all modified external trajectories
//            for vertex conflicts, edge swaps, and stay-at-goal occupancy.
//          - Rechecks saved neighborhood cost pruning ceiling against current incumbent cost.
//          - Discards conservatively with explicit diagnostic reason if compatibility cannot be established.
//
// Scope: checks only the state macro-prefix mode retains (concrete prefix paths, neighbourhood
// membership, versions, cost ceiling). It says nothing about an unfinished search frontier, which
// macro-prefix mode discards. Final whole-plan validation before commit is still required.

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

#include "common.h"
#include "Instance.h"
#include "rbl_checkpoint.hpp"

namespace rbl {

enum class RetentionPolicy {
    DISCARD_ON_CHANGE = 0,          // Policy A: Discard whenever incumbent changes
    WHOLE_CHECKPOINT_SELECTIVE = 1, // Policy B: All-or-nothing selective footprint validation
    PARTIAL_PREFIX_RECOVERY = 2     // Policy C: Conservative prefix truncation & partial salvage
};

inline const char* toString(RetentionPolicy p) {
    switch (p) {
        case RetentionPolicy::DISCARD_ON_CHANGE: return "DISCARD_ON_CHANGE";
        case RetentionPolicy::WHOLE_CHECKPOINT_SELECTIVE: return "WHOLE_CHECKPOINT_SELECTIVE";
        case RetentionPolicy::PARTIAL_PREFIX_RECOVERY: return "PARTIAL_PREFIX_RECOVERY";
        default: return "UNKNOWN";
    }
}

enum class CompatibilityVerdict {
    COMPATIBLE_UNCHANGED = 0,        // Stage 1 match: version unchanged, 100% compatible
    COMPATIBLE_PREFIX_VALID = 1,     // Stage 2 passed: external changes verified non-conflicting (full prefix)
    COMPATIBLE_PARTIAL_PREFIX = 2,   // Stage 2 partial: first k paths valid (0 < k < r_done), invalid at k
    DISCARD_NEIGHBORHOOD_MUTATED = 3,// An agent in the checkpoint's neighborhood was modified in the incumbent
    DISCARD_VERTEX_CONFLICT = 4,     // Retained prefix collides with a modified external agent at (x, y, t)
    DISCARD_EDGE_SWAP = 5,           // Retained prefix swaps edge with a modified external agent (u->v, v->u)
    DISCARD_STAY_AT_GOAL = 6,        // Retained prefix traverses a cell occupied by a parked external agent
    DISCARD_COST_BOUND_EXCEEDED = 7, // Prefix accumulated cost >= current incumbent neighborhood cost
    DISCARD_POLICY_DISABLED = 8,     // Discarded because selective retention is disabled (Policy 0: Discard-on-Change)
    COMPATIBLE_VALIDATED = COMPATIBLE_PREFIX_VALID // Backward compatibility alias
};

inline const char* toString(CompatibilityVerdict v) {
    switch (v) {
        case CompatibilityVerdict::COMPATIBLE_UNCHANGED: return "COMPATIBLE_UNCHANGED";
        case CompatibilityVerdict::COMPATIBLE_PREFIX_VALID: return "COMPATIBLE_PREFIX_VALID";
        case CompatibilityVerdict::COMPATIBLE_PARTIAL_PREFIX: return "COMPATIBLE_PARTIAL_PREFIX";
        case CompatibilityVerdict::DISCARD_NEIGHBORHOOD_MUTATED: return "DISCARD_NEIGHBORHOOD_MUTATED";
        case CompatibilityVerdict::DISCARD_VERTEX_CONFLICT: return "DISCARD_VERTEX_CONFLICT";
        case CompatibilityVerdict::DISCARD_EDGE_SWAP: return "DISCARD_EDGE_SWAP";
        case CompatibilityVerdict::DISCARD_STAY_AT_GOAL: return "DISCARD_STAY_AT_GOAL";
        case CompatibilityVerdict::DISCARD_COST_BOUND_EXCEEDED: return "DISCARD_COST_BOUND_EXCEEDED";
        case CompatibilityVerdict::DISCARD_POLICY_DISABLED: return "DISCARD_POLICY_DISABLED";
        default: return "UNKNOWN";
    }
}

inline std::ostream& operator<<(std::ostream& os, CompatibilityVerdict v) {
    return os << toString(v);
}

struct CompatibilityReport {
    CompatibilityVerdict verdict = CompatibilityVerdict::DISCARD_POLICY_DISABLED;
    double validation_time_us = 0.0;
    int conflicting_agent = -1;
    int conflict_time = -1;
    int conflict_location = -1;
    int stage_reached = 0;            // 0 = Policy-0 version comparison only, 1 = Stage 1, 2 = Stage 2
    int modified_external_agents = 0; // external agents whose trajectory version changed (Stage 2 only)
    int valid_prefix_len = 0;         // Number of valid prefix paths safe to reuse (k)
    int original_prefix_len = 0;      // Number of prefix paths before validation (r_done)
    int first_conflict_agent_index = -1; // Index in priority_order where first conflict occurred (-1 if none)
    std::string detail;

    bool isCompatible() const {
        return verdict == CompatibilityVerdict::COMPATIBLE_UNCHANGED ||
               verdict == CompatibilityVerdict::COMPATIBLE_PREFIX_VALID ||
               verdict == CompatibilityVerdict::COMPATIBLE_PARTIAL_PREFIX;
    }

    bool isPartial() const {
        return verdict == CompatibilityVerdict::COMPATIBLE_PARTIAL_PREFIX;
    }
};

class ConservativeConstraintValidator {
public:
    // Validate checkpoint compatibility against the current incumbent state
    static CompatibilityReport validate(
        const MacroPrefixCheckpoint& cp,
        const std::vector<Path>& current_incumbent,
        const std::vector<uint64_t>& current_agent_versions,
        uint64_t current_incumbent_version,
        RetentionPolicy policy = RetentionPolicy::WHOLE_CHECKPOINT_SELECTIVE) {

        auto t0 = std::chrono::steady_clock::now();
        CompatibilityReport rep;
        rep.original_prefix_len = cp.r_done;

        // Reference Policy 0: Discard-on-Change
        if (policy == RetentionPolicy::DISCARD_ON_CHANGE) {
            rep.stage_reached = 0;
            if (cp.incumbent_version != current_incumbent_version) {
                rep.verdict = CompatibilityVerdict::DISCARD_POLICY_DISABLED;
                rep.valid_prefix_len = 0;
                rep.detail = "Incumbent version changed from " + std::to_string(cp.incumbent_version) +
                             " to " + std::to_string(current_incumbent_version) + " (Policy: Discard-on-Change)";
                auto t1 = std::chrono::steady_clock::now();
                rep.validation_time_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
                return rep;
            } else {
                rep.verdict = CompatibilityVerdict::COMPATIBLE_UNCHANGED;
                rep.valid_prefix_len = cp.r_done;
                rep.detail = "Incumbent version matches exactly (" + std::to_string(cp.incumbent_version) + ")";
                auto t1 = std::chrono::steady_clock::now();
                rep.validation_time_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
                return rep;
            }
        }

        // Policy 1 (Whole Selective) & Policy 2 (Partial Recovery)
        // Stage 1: Fast version check
        rep.stage_reached = 1;
        if (cp.incumbent_version == current_incumbent_version) {
            rep.verdict = CompatibilityVerdict::COMPATIBLE_UNCHANGED;
            rep.valid_prefix_len = cp.r_done;
            rep.detail = "Stage 1 Match: No incumbent changes since checkpoint creation";
            auto t1 = std::chrono::steady_clock::now();
            rep.validation_time_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
            return rep;
        }

        // Stage 2: Conservative Spatio-Temporal Compatibility Check
        rep.stage_reached = 2;
        if (cp.agent_versions_at_save.size() != current_agent_versions.size()) {
            rep.verdict = CompatibilityVerdict::DISCARD_NEIGHBORHOOD_MUTATED;
            rep.valid_prefix_len = 0;
            rep.detail = "Agent version vectors have different sizes; cannot establish compatibility";
            auto t1 = std::chrono::steady_clock::now();
            rep.validation_time_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
            return rep;
        }

        // Check 1: Overlapping Neighborhood Mutations
        for (int a : cp.neighborhood_agents) {
            if (a < (int)current_agent_versions.size() && a < (int)cp.agent_versions_at_save.size()) {
                if (current_agent_versions[a] != cp.agent_versions_at_save[a]) {
                    rep.verdict = CompatibilityVerdict::DISCARD_NEIGHBORHOOD_MUTATED;
                    rep.valid_prefix_len = 0;
                    rep.conflicting_agent = a;
                    rep.detail = "Neighborhood agent " + std::to_string(a) +
                                 " was replanned in intervening iteration (version " +
                                 std::to_string(cp.agent_versions_at_save[a]) + " -> " +
                                 std::to_string(current_agent_versions[a]) + ")";
                    auto t1 = std::chrono::steady_clock::now();
                    rep.validation_time_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
                    return rep;
                }
            }
        }

        // Check 2: Cost Bound & Pruning Assumptions
        int current_neighborhood_cost = 0;
        for (int a : cp.neighborhood_agents) {
            current_neighborhood_cost += std::max(0, (int)current_incumbent[a].size() - 1);
        }

        if (cp.prefix_accumulated_cost >= current_neighborhood_cost) {
            // Note: If partial recovery is enabled, a sub-prefix might still have lower cost,
            // but if r_done == 0 or whole selective, we discard immediately.
            if (policy != RetentionPolicy::PARTIAL_PREFIX_RECOVERY) {
                rep.verdict = CompatibilityVerdict::DISCARD_COST_BOUND_EXCEEDED;
                rep.valid_prefix_len = 0;
                rep.detail = "Prefix accumulated cost (" + std::to_string(cp.prefix_accumulated_cost) +
                             ") >= current neighborhood incumbent cost (" +
                             std::to_string(current_neighborhood_cost) + "); cannot yield improvement";
                auto t1 = std::chrono::steady_clock::now();
                rep.validation_time_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
                return rep;
            }
        }

        // Check 3: Spatio-Temporal Conflict Detection between Retained Prefix and Modified External Agents
        if (cp.r_done == 0 || cp.prefix_paths.empty()) {
            rep.verdict = CompatibilityVerdict::COMPATIBLE_PREFIX_VALID;
            rep.valid_prefix_len = 0;
            rep.detail = "Empty prefix (r_done = 0): no prefix paths to collide with external modifications";
            auto t1 = std::chrono::steady_clock::now();
            rep.validation_time_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
            return rep;
        }

        // Identify which external agents actually changed their trajectories
        std::vector<int> modified_external_agents;
        for (size_t a = 0; a < current_agent_versions.size(); ++a) {
            if (std::find(cp.neighborhood_agents.begin(), cp.neighborhood_agents.end(), (int)a) != cp.neighborhood_agents.end()) {
                continue;
            }
            if (a < cp.agent_versions_at_save.size() && current_agent_versions[a] != cp.agent_versions_at_save[a]) {
                modified_external_agents.push_back((int)a);
            }
        }

        rep.modified_external_agents = (int)modified_external_agents.size();

        if (modified_external_agents.empty()) {
            if (cp.prefix_accumulated_cost >= current_neighborhood_cost) {
                rep.verdict = CompatibilityVerdict::DISCARD_COST_BOUND_EXCEEDED;
                rep.valid_prefix_len = 0;
                rep.detail = "Prefix accumulated cost exceeds incumbent; discarded";
            } else {
                rep.verdict = CompatibilityVerdict::COMPATIBLE_PREFIX_VALID;
                rep.valid_prefix_len = cp.r_done;
                rep.detail = "Stage 2 Verified: No external agents changed since checkpoint creation";
            }
            auto t1 = std::chrono::steady_clock::now();
            rep.validation_time_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
            return rep;
        }

        // Check each retained prefix path sequentially in priority order
        for (int k = 0; k < cp.r_done; ++k) {
            const Path& prefix_p = cp.prefix_paths[k];
            int agent_k = cp.priority_order[k];
            bool conflict_detected_on_k = false;

            for (int ext_a : modified_external_agents) {
                const Path& ext_p = current_incumbent[ext_a];
                int max_t = std::max((int)prefix_p.size(), (int)ext_p.size());

                for (int t = 0; t < max_t && !conflict_detected_on_k; ++t) {
                    int loc_k = (t < (int)prefix_p.size()) ? prefix_p[t].location : prefix_p.back().location;
                    int loc_ext = (t < (int)ext_p.size()) ? ext_p[t].location : ext_p.back().location;

                    // Vertex collision check
                    if (loc_k == loc_ext) {
                        conflict_detected_on_k = true;
                        rep.conflicting_agent = ext_a;
                        rep.conflict_time = t;
                        rep.conflict_location = loc_k;
                        rep.first_conflict_agent_index = k;

                        if (t >= (int)ext_p.size() - 1) {
                            rep.verdict = CompatibilityVerdict::DISCARD_STAY_AT_GOAL;
                            rep.detail = "Retained prefix agent " + std::to_string(agent_k) +
                                         " collides at t=" + std::to_string(t) + " with parked external agent " +
                                         std::to_string(ext_a) + " at goal location " + std::to_string(loc_k);
                        } else {
                            rep.verdict = CompatibilityVerdict::DISCARD_VERTEX_CONFLICT;
                            rep.detail = "Retained prefix agent " + std::to_string(agent_k) +
                                         " vertex collision at t=" + std::to_string(t) + " with external agent " +
                                         std::to_string(ext_a) + " at location " + std::to_string(loc_k);
                        }
                        break;
                    }

                    // Edge swap check
                    if (t + 1 < max_t) {
                        int next_k = (t + 1 < (int)prefix_p.size()) ? prefix_p[t + 1].location : prefix_p.back().location;
                        int next_ext = (t + 1 < (int)ext_p.size()) ? ext_p[t + 1].location : ext_p.back().location;

                        if (loc_k == next_ext && next_k == loc_ext && loc_k != next_k) {
                            conflict_detected_on_k = true;
                            rep.verdict = CompatibilityVerdict::DISCARD_EDGE_SWAP;
                            rep.conflicting_agent = ext_a;
                            rep.conflict_time = t;
                            rep.conflict_location = loc_k;
                            rep.first_conflict_agent_index = k;
                            rep.detail = "Retained prefix agent " + std::to_string(agent_k) +
                                         " edge swap collision at t=" + std::to_string(t) + "->" + std::to_string(t+1) +
                                         " with external agent " + std::to_string(ext_a) + " (" +
                                         std::to_string(loc_k) + "<->" + std::to_string(next_k) + ")";
                            break;
                        }
                    }
                }
                if (conflict_detected_on_k) break;
            }

            if (conflict_detected_on_k) {
                // Agent k is the earliest invalid path.
                // Under Partial Prefix Recovery: Can we salvage the valid prefix before position k?
                if (policy == RetentionPolicy::PARTIAL_PREFIX_RECOVERY && k > 0) {
                    // Check cost bound of the salvaged prefix P_{0..k-1}
                    int salvaged_cost = 0;
                    for (int j = 0; j < k; ++j) {
                        salvaged_cost += std::max(0, (int)cp.prefix_paths[j].size() - 1);
                    }
                    if (salvaged_cost >= current_neighborhood_cost) {
                        rep.verdict = CompatibilityVerdict::DISCARD_COST_BOUND_EXCEEDED;
                        rep.valid_prefix_len = 0;
                        rep.detail = "Salvaged prefix cost (" + std::to_string(salvaged_cost) +
                                     ") >= current incumbent cost (" + std::to_string(current_neighborhood_cost) + "); discarded";
                    } else {
                        rep.verdict = CompatibilityVerdict::COMPATIBLE_PARTIAL_PREFIX;
                        rep.valid_prefix_len = k;
                        rep.detail = "Partial recovery: first " + std::to_string(k) + " of " + std::to_string(cp.r_done) +
                                     " prefix paths valid; conflict on agent " + std::to_string(agent_k) +
                                     " at position " + std::to_string(k);
                    }
                } else {
                    // Whole selective policy or conflict occurred at k = 0 (no prefix to salvage)
                    rep.valid_prefix_len = 0;
                }
                auto t1 = std::chrono::steady_clock::now();
                rep.validation_time_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
                return rep;
            }
        }

        // All r_done prefix paths are collision-free!
        if (cp.prefix_accumulated_cost >= current_neighborhood_cost) {
            rep.verdict = CompatibilityVerdict::DISCARD_COST_BOUND_EXCEEDED;
            rep.valid_prefix_len = 0;
            rep.detail = "Prefix accumulated cost exceeds incumbent; discarded";
        } else {
            rep.verdict = CompatibilityVerdict::COMPATIBLE_PREFIX_VALID;
            rep.valid_prefix_len = cp.r_done;
            rep.detail = "Stage 2 Verified: Retained prefix collision-free against " +
                         std::to_string(modified_external_agents.size()) + " modified external agents";
        }
        auto t1 = std::chrono::steady_clock::now();
        rep.validation_time_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        return rep;
    }

    // Backward-compatible bool overload
    static CompatibilityReport validate(
        const MacroPrefixCheckpoint& cp,
        const std::vector<Path>& current_incumbent,
        const std::vector<uint64_t>& current_agent_versions,
        uint64_t current_incumbent_version,
        bool selective_retention) {
        return validate(cp, current_incumbent, current_agent_versions, current_incumbent_version,
                        selective_retention ? RetentionPolicy::WHOLE_CHECKPOINT_SELECTIVE : RetentionPolicy::DISCARD_ON_CHANGE);
    }
};

} // namespace rbl
