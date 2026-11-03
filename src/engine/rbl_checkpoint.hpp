#include <type_traits>
#pragma once
// RouteBudget Lab - Macro-Prefix Checkpoint Contract & Bounded Store (FR-02, NFR-02, NFR-03)
//
// Exact contract audited against anytime-mapf LNS / PP repair:
// 1. Preserves completed prefix paths P_{1..r_done}
// 2. Preserves neighborhood membership and priority permutation
// 3. Preserves incumbent version and constraint context for unchanged-constraint verification
// 4. Preserves RNG / tie-breaking state for deterministic restart of unfinished agent
// 5. Preserves accumulated active search time and node expansions
// 6. Implements bounded storage K in {0, 1, 2} with explicit lifecycle states and deterministic eviction

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <array>
#include <iostream>
#include <memory>
#include <algorithm>

// Include anytime-mapf types
#include "common.h"

namespace rbl {

enum class CheckpointLifecycle {
    EMPTY,       // Unused slot
    ACTIVE,      // Currently being assembled in active repair
    SUSPENDED,   // Saved upon slice cutoff; available for resumption
    COMMITTED,   // Successfully completed and merged into incumbent
    DISCARDED,   // Evicted or abandoned
    EXPIRED      // Discarded due to global deadline
};

inline const char* toString(CheckpointLifecycle s) {
    switch (s) {
        case CheckpointLifecycle::EMPTY: return "EMPTY";
        case CheckpointLifecycle::ACTIVE: return "ACTIVE";
        case CheckpointLifecycle::SUSPENDED: return "SUSPENDED";
        case CheckpointLifecycle::COMMITTED: return "COMMITTED";
        case CheckpointLifecycle::DISCARDED: return "DISCARDED";
        case CheckpointLifecycle::EXPIRED: return "EXPIRED";
        default: return "UNKNOWN";
    }
}

inline std::ostream& operator<<(std::ostream& os, CheckpointLifecycle s) {
    return os << toString(s);
}

// Audited Macro-Prefix State Tuple C_i
struct MacroPrefixCheckpoint {
    uint64_t checkpoint_id = 0;
    CheckpointLifecycle lifecycle = CheckpointLifecycle::EMPTY;

    // 1. Neighborhood Transaction Membership and Priority Permutation
    std::vector<int> neighborhood_agents; // All agent IDs in destruction neighborhood N_i
    std::vector<int> priority_order;      // Permuted order of N_i in which agents are replanned
    int r_done = 0;                       // Count of successfully replanned prefix agents (0 <= r_done <= |N|)

    // 2. Completed Prefix Paths
    std::vector<Path> prefix_paths;       // Paths for the first r_done agents in priority_order

    // 3. Old Incumbent Paths (for transactional isolation and rollback)
    std::vector<Path> old_paths;          // Original paths of neighborhood agents before repair
    int64_t old_neighborhood_cost = 0;        // Sum of costs of old_paths
    int64_t prefix_accumulated_cost = 0;      // Current sum of costs for prefix_paths

    // 4. Incumbent and Constraint Version Context
    uint64_t incumbent_version = 0;       // Version counter of incumbent solution when saved
    int64_t incumbent_sum_of_costs = 0;       // Total incumbent sum_of_costs at save time
    std::vector<uint64_t> agent_versions_at_save; // Per-agent trajectory version counters when saved

    // 5. RNG and Tie-Breaking State for Deterministic Restart of Agent r_done
    unsigned int base_rng_seed = 0;       // Base RNG seed passed into repair transaction
    unsigned int rng_seed_at_unfinished_agent = 0;
    uint32_t step_counter_at_unfinished_agent = 0;

    // 6. Cumulative Search Accounting (charged to repair transaction)
    double accumulated_active_time_s = 0.0;
    uint64_t accumulated_expansions = 0;
    uint64_t repeated_expansions = 0;     // Work done on unfinished agent that is repeated upon restart

    // 7. Live Performance Metrics
    double save_time_us = 0.0;
    double restore_time_us = 0.0;
    int consecutive_stagnant_attempts = 0; // Number of consecutive resumptions without advancing r_done

    // Retained memory footprint in bytes
    size_t getMemoryFootprintBytes() const {
        size_t bytes = sizeof(MacroPrefixCheckpoint);
        bytes += neighborhood_agents.capacity() * sizeof(int);
        bytes += priority_order.capacity() * sizeof(int);
        for (const auto& p : prefix_paths) {
            bytes += sizeof(Path) + p.capacity() * sizeof(PathEntry);
        }
        for (const auto& p : old_paths) {
            bytes += sizeof(Path) + p.capacity() * sizeof(PathEntry);
        }
        return bytes;
    }

    // Has a non-empty, reusable prefix
    bool hasReusablePrefix() const {
        return r_done > 0 && !prefix_paths.empty();
    }

    // Agent currently pending replan
    int getUnfinishedAgentId() const {
        if (r_done >= 0 && r_done < (int)priority_order.size()) {
            return priority_order[r_done];
        }
        return -1;
    }

    void reset() {
        checkpoint_id = 0;
        lifecycle = CheckpointLifecycle::EMPTY;
        neighborhood_agents.clear();
        priority_order.clear();
        r_done = 0;
        prefix_paths.clear();
        old_paths.clear();
        old_neighborhood_cost = 0;
        prefix_accumulated_cost = 0;
        incumbent_version = 0;
        incumbent_sum_of_costs = 0;
        agent_versions_at_save.clear();
        base_rng_seed = 0;
        rng_seed_at_unfinished_agent = 0;
        step_counter_at_unfinished_agent = 0;
        accumulated_active_time_s = 0.0;
        accumulated_expansions = 0;
        repeated_expansions = 0;
        save_time_us = 0.0;
        restore_time_us = 0.0;
        consecutive_stagnant_attempts = 0;
    }
};

enum class CheckpointRemoval { Invalid, Stagnation, Consumption, Recovery };

// Bounded Checkpoint Store: Supports K in {0, 1, 2}
class BoundedCheckpointStore {
public:
    explicit BoundedCheckpointStore(int capacity = 2) : max_capacity(std::max(0, std::min(capacity, 2))) {
        for (auto& slot : slots) slot.reset();
    }

    int getCapacity() const { return max_capacity; }
    void setCapacity(int k) {
        max_capacity = std::max(0, std::min(k, 2));
        if (max_capacity < 2) slots[1].reset();
        if (max_capacity < 1) slots[0].reset();
    }

    int getNumSuspended() const {
        int count = 0;
        for (int i = 0; i < max_capacity; ++i) {
            if (slots[i].lifecycle == CheckpointLifecycle::SUSPENDED) count++;
        }
        return count;
    }

    // Returns slot index in [0, max_capacity-1] of the checkpoint that will be evicted
    // on the next saveCheckpoint() call if all slots are full, or -1 if an empty slot exists.
    int findEvictionVictimSlot() const {
        if (max_capacity == 0) return -1;
        for (int i = 0; i < max_capacity; ++i) {
            if (slots[i].lifecycle == CheckpointLifecycle::EMPTY ||
                slots[i].lifecycle == CheckpointLifecycle::DISCARDED ||
                slots[i].lifecycle == CheckpointLifecycle::COMMITTED ||
                slots[i].lifecycle == CheckpointLifecycle::EXPIRED) {
                return -1; // Slot available; no eviction will occur
            }
        }
        int target_slot = 0;
        if (max_capacity > 1 && slots[1].checkpoint_id < slots[0].checkpoint_id) {
            target_slot = 1;
        }
        return target_slot;
    }

    // Save checkpoint into store with deterministic eviction
    // Returns index in [0, max_capacity-1], or -1 if capacity == 0
    int saveCheckpoint(const MacroPrefixCheckpoint& cp) {
        if (max_capacity == 0) {
            return -1; // K=0: Checkpointing disabled
        }

        auto t0 = std::chrono::steady_clock::now();
        MacroPrefixCheckpoint staged = cp; // allocate before mutating store

        // 1. Look for an empty or discarded slot
        int target_slot = -1;
        for (int i = 0; i < max_capacity; ++i) {
            if (slots[i].lifecycle == CheckpointLifecycle::EMPTY ||
                slots[i].lifecycle == CheckpointLifecycle::DISCARDED ||
                slots[i].lifecycle == CheckpointLifecycle::COMMITTED ||
                slots[i].lifecycle == CheckpointLifecycle::EXPIRED) {
                target_slot = i;
                break;
            }
        }

        // 2. If all slots full, apply deterministic FIFO / oldest-id eviction
        if (target_slot == -1) {
            target_slot = 0;
            if (max_capacity > 1 && slots[1].checkpoint_id < slots[0].checkpoint_id) {
                target_slot = 1;
            }
            slots[target_slot].lifecycle = CheckpointLifecycle::DISCARDED;
            total_evictions++;
        }

        // Copy checkpoint into slot
        static_assert(std::is_nothrow_swappable<MacroPrefixCheckpoint>::value, "Checkpoint swap must not throw");
        std::swap(slots[target_slot], staged);
        slots[target_slot].lifecycle = CheckpointLifecycle::SUSPENDED;

        auto t1 = std::chrono::steady_clock::now();
        double elapsed_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        slots[target_slot].save_time_us = elapsed_us;
        last_save_time_us = elapsed_us;

        return target_slot;
    }

    // Retrieve suspended checkpoint by slot index
    MacroPrefixCheckpoint* getCheckpoint(int slot_idx) {
        if (slot_idx < 0 || slot_idx >= max_capacity) return nullptr;
        if (slots[slot_idx].lifecycle != CheckpointLifecycle::SUSPENDED) return nullptr;
        return &slots[slot_idx];
    }

    const MacroPrefixCheckpoint* getCheckpoint(int slot_idx) const {
        if (slot_idx < 0 || slot_idx >= max_capacity) return nullptr;
        if (slots[slot_idx].lifecycle != CheckpointLifecycle::SUSPENDED) return nullptr;
        return &slots[slot_idx];
    }

    // Retrieve the most recent suspended checkpoint (or null if none)
    MacroPrefixCheckpoint* getLatestSuspended() {
        MacroPrefixCheckpoint* best = nullptr;
        for (int i = 0; i < max_capacity; ++i) {
            if (slots[i].lifecycle == CheckpointLifecycle::SUSPENDED) {
                if (best == nullptr || slots[i].checkpoint_id > best->checkpoint_id) {
                    best = &slots[i];
                }
            }
        }
        return best;
    }

    void markCommitted(int slot_idx, CheckpointRemoval reason = CheckpointRemoval::Consumption) {
        if (slot_idx >= 0 && slot_idx < max_capacity && slots[slot_idx].lifecycle == CheckpointLifecycle::SUSPENDED) {
            ++removals[static_cast<size_t>(reason)];
            slots[slot_idx].lifecycle = CheckpointLifecycle::COMMITTED;
        }
    }

    void markDiscarded(int slot_idx, CheckpointRemoval reason = CheckpointRemoval::Invalid) {
        if (slot_idx >= 0 && slot_idx < max_capacity && slots[slot_idx].lifecycle == CheckpointLifecycle::SUSPENDED) {
            ++removals[static_cast<size_t>(reason)];
            slots[slot_idx].lifecycle = CheckpointLifecycle::DISCARDED;
        }
    }

    void markExpired(int slot_idx) {
        if (slot_idx >= 0 && slot_idx < max_capacity) {
            if (slots[slot_idx].lifecycle == CheckpointLifecycle::SUSPENDED) ++deadline_removals;
            slots[slot_idx].lifecycle = CheckpointLifecycle::EXPIRED;
        }
    }

    void clear() {
        for (auto& s : slots) s.reset();
    }

    size_t getFixedBookkeepingBytes() const {
        return sizeof(BoundedCheckpointStore);
    }

    size_t getRetainedPayloadMemoryBytes() const {
        if (max_capacity == 0) return 0;
        size_t total = 0;
        for (int i = 0; i < max_capacity; ++i) {
            if (slots[i].lifecycle == CheckpointLifecycle::SUSPENDED) {
                total += slots[i].getMemoryFootprintBytes();
            }
        }
        return total;
    }

    size_t getTotalRetainedMemoryBytes() const {
        return getFixedBookkeepingBytes() + getRetainedPayloadMemoryBytes();
    }

    uint64_t getRemovalCount(CheckpointRemoval reason) const { return removals[static_cast<size_t>(reason)]; }
    uint64_t getDeadlineRemovals() const { return deadline_removals; }
    int getTotalEvictions() const { return total_evictions; }
    double getLastSaveTimeUs() const { return last_save_time_us; }

private:
    int max_capacity = 2;
    std::array<MacroPrefixCheckpoint, 2> slots;
    std::array<uint64_t,4> removals{};
    uint64_t deadline_removals = 0;
    int total_evictions = 0;
    double last_save_time_us = 0.0;
};

} // namespace rbl
