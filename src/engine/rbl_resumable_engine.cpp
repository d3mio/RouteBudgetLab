#include "rbl_resumable_engine.hpp"
#include "rbl_constraint_validator.hpp"

#include <iostream>
#include <cassert>
#include <algorithm>
#include <unordered_set>
#include <type_traits>

namespace rbl {

// Local deterministic node structure for cooperative Space-Time A*
struct RBL_AStarNode : public LLNode {
    typedef boost::heap::pairing_heap<RBL_AStarNode*, boost::heap::compare<LLNode::secondary_compare_node>>::handle_type focal_handle_t;
    focal_handle_t focal_handle;

    RBL_AStarNode() : LLNode() {}
    RBL_AStarNode(const RBL_AStarNode& other) : LLNode(other) {}
    RBL_AStarNode(int loc, int g, int h, LLNode* p, int t, int conflicts)
        : LLNode(loc, g, h, p, t, conflicts) {}
    ~RBL_AStarNode() = default;

    struct Hasher {
        size_t operator()(const RBL_AStarNode* n) const {
            size_t loc_h = std::hash<int>()(n->location);
            size_t t_h = std::hash<int>()(n->timestep);
            return (loc_h ^ (t_h << 1));
        }
    };

    struct Equal {
        bool operator()(const RBL_AStarNode* s1, const RBL_AStarNode* s2) const {
            return (s1 == s2) || (s1 && s2 &&
                                  s1->location == s2->location &&
                                  s1->timestep == s2->timestep &&
                                  s1->wait_at_goal == s2->wait_at_goal &&
                                  s1->is_goal == s2->is_goal);
        }
    };
};

ResumableRepairEngine::ResumableRepairEngine(const Instance& instance, bool use_sipp, int checkpoint_capacity)
    : instance(instance), use_sipp(use_sipp), num_agents(instance.getDefaultNumberOfAgents()),
      path_table(instance.map_size), checkpoint_store(checkpoint_capacity) {
    // Instantiate single agent solvers
    single_agent_solvers.resize(num_agents, nullptr);
    for (int i = 0; i < num_agents; ++i) {
        if (use_sipp) {
            single_agent_solvers[i] = new SIPP(instance, i);
        } else {
            single_agent_solvers[i] = new SpaceTimeAStar(instance, i);
        }
    }
}

ResumableRepairEngine::~ResumableRepairEngine() {
    for (auto* s : single_agent_solvers) delete s;
    single_agent_solvers.clear();
}

namespace {
void swapReservations(PathTable& a, PathTable& b) noexcept {
    a.table.swap(b.table);
    a.goals.swap(b.goals);
    std::swap(a.makespan, b.makespan);
}
}

bool ResumableRepairEngine::publishCandidate(std::vector<Path> candidate,
        const std::vector<int>& changed, uint64_t expected_version,
        bool require_improvement, SearchBudget* budget) {
    const auto commit_start = std::chrono::steady_clock::now();
    try {
        auto cancelled = [&](int stage) {
            if (stage < 5 && publication_failure_stage == -stage) throw std::bad_alloc();
            return publication_failure_stage == stage ||
                (budget && budget->isGlobalBudgetExhausted(0.0));
        };
        if (cancelled(1)) return false;
        int64_t candidate_soc = 0;
        std::string error;
        if (!PartialAcceptanceOptimizer::validateCompletePlanIndependently(candidate, instance, candidate_soc, error, budget)) return false;
        if (cancelled(2)) return false;
        if (require_improvement && candidate_soc >= computeCost(incumbent_paths)) return false;
        PathTable reservations(instance.map_size);
        for (int a = 0; a < num_agents; ++a) {
            if ((budget && !budget->pollStage(RecoveryStage::Preparation)) || cancelled(3)) return false;
            reservations.insertPath(a, candidate[a]);
        }
        auto versions = agent_trajectory_versions;
        if (incumbent_version == std::numeric_limits<uint64_t>::max()) return false;
        for (int a : changed) {
            if (a < 0 || a >= num_agents || versions[a] == std::numeric_limits<uint64_t>::max()) return false;
            ++versions[a];
        }
        auto staged_publications = publications;
        staged_publications.push_back({commit_start, {}, candidate_soc});
        if ((budget && !budget->pollStage(RecoveryStage::BeforePublication)) || cancelled(4) || incumbent_version != expected_version) return false;
        const auto old_cost = incumbent_sum_of_costs;
        const auto old_version = incumbent_version;
        const auto old_cache_version = last_validated_incumbent_version;
        const auto old_cache_cost = last_validated_incumbent_cost;
        // Single-threaded transaction boundary: swaps/scalar stores cannot throw.
        incumbent_paths.swap(candidate);
        swapReservations(path_table, reservations);
        agent_trajectory_versions.swap(versions);
        incumbent_sum_of_costs = candidate_soc;
        incumbent_version = old_version + 1;
        last_validated_incumbent_version = incumbent_version;
        last_validated_incumbent_cost = candidate_soc;
        publications.swap(staged_publications);
        publications.back().complete = std::chrono::steady_clock::now();
        // A boundary-crossing publication must not replace the last on-time plan.
        if ((budget && !budget->pollStage(RecoveryStage::AfterPublication)) || cancelled(5)) {
            incumbent_paths.swap(candidate);
            swapReservations(path_table, reservations);
            agent_trajectory_versions.swap(versions);
            publications.swap(staged_publications);
            incumbent_sum_of_costs = old_cost;
            incumbent_version = old_version;
            last_validated_incumbent_version = old_cache_version;
            last_validated_incumbent_cost = old_cache_cost;
            return false;
        }
        return true;
    } catch (const std::exception&) {
        // Preparation failed; publication contains no throwing operations.
        return false;
    }
}

void ResumableRepairEngine::setInitialSolution(const std::vector<Path>& initial_paths) {
    int64_t soc = 0;
    std::string error;
    if (!PartialAcceptanceOptimizer::validateCompletePlanIndependently(initial_paths, instance, soc, error))
        throw std::invalid_argument("Invalid initial plan: " + error);
    auto paths = initial_paths;
    std::vector<uint64_t> versions(num_agents, 1);
    PathTable reservations(instance.map_size);
    for (int a = 0; a < num_agents; ++a) reservations.insertPath(a, paths[a]);
    incumbent_paths.swap(paths);
    agent_trajectory_versions.swap(versions);
    swapReservations(path_table, reservations);
    incumbent_sum_of_costs = soc;
    incumbent_version = last_validated_incumbent_version = 1;
    last_validated_incumbent_cost = soc;
    checkpoint_store.clear();
    publications.clear();
    recovery_events.clear();
}

int64_t ResumableRepairEngine::computeCost(const std::vector<Path>& paths) {
    // Each entry is a COMPLETE route in the completed-agent prefix, not an
    // unfinished search prefix. Its last position is its validated goal.
    return checkedSumOfCosts(paths, [](const Path& p) {
        return PartialAcceptanceOptimizer::computeArrivalCost(p, -1);
    });
}

bool ResumableRepairEngine::validateIncumbent(std::string& error_msg) const {
    if ((int)incumbent_paths.size() != num_agents) {
        error_msg = "Incumbent path count does not match agent count";
        return false;
    }
    if (incumbent_version > 0 && last_validated_incumbent_version == incumbent_version &&
        last_validated_incumbent_cost == incumbent_sum_of_costs) {
        return true;
    }
    int64_t dummy_soc = 0;
    if (!PartialAcceptanceOptimizer::validateCompletePlanIndependently(incumbent_paths, instance, dummy_soc, error_msg)) {
        return false;
    }
    if (dummy_soc != incumbent_sum_of_costs) {
        error_msg = "Stored incumbent cost differs from authoritative cost";
        return false;
    }
    last_validated_incumbent_version = incumbent_version;
    last_validated_incumbent_cost = dummy_soc;
    return true;
}

void ResumableRepairEngine::resetStats() {
    total_accepted_repairs = 0;
    total_non_improving_repairs = 0;
    total_failed_repairs = 0;
    total_interrupted_slices = 0;
    total_expansions = 0;
    total_repeated_expansions = 0;
    total_partial_acceptance_attempts = 0;
    total_partial_acceptance_positive_opps = 0;
    total_partial_acceptance_committed = 0;
    total_partial_acceptance_budget_skipped = 0;
    total_partial_acceptance_gain = 0;
    total_partial_acceptance_time_us = 0.0;
    last_partial_acceptance_result = PartialAcceptanceResult();
    partial_acceptance_history.clear();
    captured_snapshots.clear();
    total_interruptions_observed = 0;
    total_interruptions_with_completed_prefix = 0;
    total_repair_attempts_counter = 0;
    total_filter_evaluations = 0;
    total_filter_skips = 0;
    total_filter_time_us = 0.0;
    total_extraction_time_us = 0.0;
    total_eviction_pa_attempts = 0;
    total_eviction_pa_filter_skips = 0;
    total_eviction_pa_committed = 0;
    total_eviction_pa_gain = 0;
    total_eviction_pa_time_us = 0.0;
    total_terminal_recovery_calls = 0;
    total_terminal_pa_examined = 0;
    total_terminal_pa_filter_skips = 0;
    total_terminal_pa_committed = 0;
    total_terminal_pa_gain = 0;
    total_terminal_pa_time_us = 0.0;
}

// Low-level Space-Time A* search with cooperative budget checking and deterministic tie-breaking
Path ResumableRepairEngine::findPathWithBudget(int agent_id,
                                              const ConstraintTable& constraint_table,
                                              SearchBudget& budget,
                                              unsigned int rng_seed) {
    SingleAgentSolver* planner = single_agent_solvers[agent_id];
    planner->reset();
    Path path;

    // Check if start location is occupied at t=0 without triggering upstream PathTable buffer overflow at t=0
    if (constraint_table.path_table_for_CT &&
        !constraint_table.path_table_for_CT->table[planner->start_location].empty() &&
        constraint_table.path_table_for_CT->table[planner->start_location][0] != NO_AGENT) {
        return path;
    }

    // Deterministic tie-breaking RNG seed
    if (rng_seed != 0) {
        srand(rng_seed);
    }

    auto holding_time = constraint_table.getHoldingTime(planner->goal_location, constraint_table.length_min);
    auto static_timestep = constraint_table.getMaxTimestep() + 1;
    auto last_target_collision_time = constraint_table.getLastCollisionTimestep(planner->goal_location);

    auto h = std::max(std::max(planner->my_heuristic[planner->start_location], holding_time),
                      last_target_collision_time + 1);

    auto* start = new RBL_AStarNode(planner->start_location, 0, h, nullptr, 0, 0);
    start->in_openlist = true;

    boost::heap::pairing_heap<RBL_AStarNode*, boost::heap::compare<LLNode::secondary_compare_node>> focal_list;
    std::unordered_set<RBL_AStarNode*, RBL_AStarNode::Hasher, RBL_AStarNode::Equal> allNodes_table;

    start->focal_handle = focal_list.push(start);
    allNodes_table.insert(start);

    auto cleanup = [&]() {
        for (auto* n : allNodes_table) delete n;
        allNodes_table.clear();
        focal_list.clear();
    };

    while (!focal_list.empty()) {
        // COOPERATIVE INTERRUPTION CHECK INSIDE LOW-LEVEL SEARCH
        if (!budget.checkNodeExpansion()) {
            total_expansions += budget.current_expansions;
            cleanup();
            return {}; // cleanly aborted mid-search
        }

        auto* curr = focal_list.top();
        focal_list.pop();
        curr->in_openlist = false;

        auto updatePath = [&](const LLNode* goal_node) {
            const LLNode* p = goal_node;
            if (p->is_goal)
                p = p->parent;
            path.clear();
            path.reserve(p->g_val + 1);
            while (p != nullptr) {
                path.emplace_back(p->location);
                p = p->parent;
            }
            std::reverse(path.begin(), path.end());
        };

        if (curr->is_goal) {
            updatePath(curr);
            break;
        } else if (curr->location == planner->goal_location &&
                   !curr->wait_at_goal &&
                   curr->timestep >= holding_time) {
            int future_collisions = constraint_table.getFutureNumOfCollisions(curr->location, curr->timestep);
            if (future_collisions == 0) {
                updatePath(curr);
                break;
            }
            auto goal = new RBL_AStarNode(*curr);
            goal->is_goal = true;
            goal->parent = curr;
            goal->num_of_conflicts += future_collisions;
            goal->h_val = 0;

            auto it = allNodes_table.find(goal);
            if (it == allNodes_table.end()) {
                goal->focal_handle = focal_list.push(goal);
                goal->in_openlist = true;
                allNodes_table.insert(goal);
            } else {
                auto existing = *it;
                if (existing->num_of_conflicts > goal->num_of_conflicts ||
                    (existing->num_of_conflicts == goal->num_of_conflicts && existing->getFVal() > goal->getFVal())) {
                    assert(existing->in_openlist);
                    existing->copy(*goal);
                    focal_list.update(existing->focal_handle);
                }
                delete goal;
            }
        }

        if (curr->timestep >= constraint_table.length_max)
            continue;

        auto next_locations = instance.getNeighbors(curr->location);
        next_locations.emplace_back(curr->location);

        for (int next_loc : next_locations) {
            int next_timestep = curr->timestep + 1;
            if (static_timestep < next_timestep) {
                if (next_loc == curr->location) continue;
                next_timestep--;
            }

            if (constraint_table.constrained(next_loc, next_timestep) ||
                constraint_table.constrained(curr->location, next_loc, next_timestep)) {
                continue;
            }

            int next_g = curr->g_val + 1;
            int next_h = planner->my_heuristic[next_loc];
            if (next_g + next_h > constraint_table.length_max) continue;

            int num_conflicts = curr->num_of_conflicts +
                                constraint_table.getNumOfConflictsForStep(curr->location, next_loc, next_timestep);
            if (num_conflicts == 0) {
                next_h = std::max(next_h, curr->getFVal() - next_g);
            } else {
                next_h = std::max(next_h, holding_time - next_g);
            }

            auto next = new RBL_AStarNode(next_loc, next_g, next_h, curr, next_timestep, num_conflicts);
            if (next_loc == planner->goal_location && curr->location == planner->goal_location) {
                next->wait_at_goal = true;
            }

            auto it = allNodes_table.find(next);
            if (it == allNodes_table.end()) {
                next->focal_handle = focal_list.push(next);
                next->in_openlist = true;
                allNodes_table.insert(next);
            } else {
                auto existing = *it;
                if (existing->num_of_conflicts > next->num_of_conflicts ||
                    (existing->num_of_conflicts == next->num_of_conflicts && existing->getFVal() > next->getFVal())) {
                    if (!existing->in_openlist) {
                        existing->copy(*next);
                        existing->focal_handle = focal_list.push(existing);
                        existing->in_openlist = true;
                    } else {
                        existing->copy(*next);
                        focal_list.update(existing->focal_handle);
                    }
                }
                delete next;
            }
        }
    }

    cleanup();
    total_expansions += budget.current_expansions;
    return path;
}

RepairOutcome ResumableRepairEngine::attemptFreshRepair(const std::vector<int>& neighborhood,
                                                        SearchBudget& budget,
                                                        unsigned int rng_seed) {
    RepairOutcome outcome;
    outcome.neighborhood_size = (int)neighborhood.size();
    int cur_attempt_id = ++total_repair_attempts_counter;

    // Check global deadline before starting
    if (budget.deadline_exceeded || budget.isGlobalDeadlineExceeded() || budget.getElapsedSeconds() >= budget.max_time_seconds) {
        outcome.type = RepairOutcomeType::STOPPED_DEADLINE;
        return outcome;
    }

    PathTable path_table = this->path_table; // private search reservations
    // 1. Transaction Isolation: Extract old incumbent paths
    std::vector<Path> old_paths(neighborhood.size());
    int64_t old_cost = 0;
    for (size_t i = 0; i < neighborhood.size(); ++i) {
        int a = neighborhood[i];
        old_paths[i] = incumbent_paths[a];
        path_table.deletePath(a, incumbent_paths[a]);
        if (!checkedAdd(old_cost, computeCost({incumbent_paths[a]}), old_cost)) throw std::overflow_error("Neighborhood cost");
    }
    outcome.old_cost = old_cost;

    // 2. Setup priority permutation
    std::vector<int> priority_order = neighborhood;
    if (rng_seed != 0) {
        std::mt19937 g(rng_seed);
        std::shuffle(priority_order.begin(), priority_order.end(), g);
    }

    std::vector<Path> prefix_paths;
    prefix_paths.reserve(priority_order.size());

    int r_done = 0;
    ConstraintTable constraint_table(instance.num_of_cols, instance.map_size, &path_table);

    // Rollback helper on failure / non-improvement / interruption
    auto rollback = [&]() {
        // Delete candidate prefix paths from table
        for (int i = 0; i < r_done; ++i) {
            path_table.deletePath(priority_order[i], prefix_paths[i]);
        }
        // Reinsert old incumbent paths
        for (size_t i = 0; i < neighborhood.size(); ++i) {
            path_table.insertPath(neighborhood[i], old_paths[i]);
        }
    };

    auto t_start = std::chrono::steady_clock::now();

    while (r_done < (int)priority_order.size()) {
        // Cooperative check at agent boundary
        if (!budget.checkAgentBoundary(r_done)) {
            break;
        }

        int agent_id = priority_order[r_done];
        unsigned int agent_seed = rng_seed + r_done * 10007;

        uint64_t exp_before = budget.current_expansions;
        Path path = findPathWithBudget(agent_id, constraint_table, budget, agent_seed);

        if (budget.interrupted) {
            // Cutoff reached inside single-agent search
            outcome.interruption.current_agent_expansions = budget.current_expansions - exp_before;
            break;
        }

        if (path.empty()) {
            // Search failed to find a path
            rollback();
            auto t_end = std::chrono::steady_clock::now();
            outcome.duration_s = std::chrono::duration<double>(t_end - t_start).count();
            outcome.total_expansions = budget.current_expansions;
            outcome.type = RepairOutcomeType::FAILED_NO_PATH;
            total_failed_repairs++;
            return outcome;
        }

        prefix_paths.push_back(path);
        path_table.insertPath(agent_id, path);
        r_done++;

        // Pruning check: if accumulated prefix cost already >= old_cost, cannot improve
        if (computeCost(prefix_paths) >= old_cost) {
            rollback();
            auto t_end = std::chrono::steady_clock::now();
            outcome.duration_s = std::chrono::duration<double>(t_end - t_start).count();
            outcome.total_expansions = budget.current_expansions;
            outcome.type = RepairOutcomeType::COMPLETED_NOT_IMPROVING;
            total_non_improving_repairs++;
            return outcome;
        }
    }

    auto t_end = std::chrono::steady_clock::now();
    outcome.duration_s = std::chrono::duration<double>(t_end - t_start).count();
    outcome.total_expansions = budget.current_expansions;

    // Check if interrupted by slice cutoff
    if (budget.interrupted || r_done < (int)priority_order.size()) {
        outcome.type = RepairOutcomeType::INTERRUPTED_SLICE;
        outcome.interruption.neighborhood_size = (int)priority_order.size();
        outcome.interruption.completed_prefix_len = r_done;
        outcome.interruption.unfinished_agent_id = (r_done < (int)priority_order.size()) ? priority_order[r_done] : -1;
        outcome.interruption.has_reusable_prefix = (r_done > 0);

        // Macro-prefix contract:
        // Assemble checkpoint C_i
        MacroPrefixCheckpoint cp;
        cp.checkpoint_id = next_checkpoint_id++;
        cp.lifecycle = CheckpointLifecycle::ACTIVE;
        cp.neighborhood_agents = neighborhood;
        cp.priority_order = priority_order;
        cp.r_done = r_done;
        cp.prefix_paths = prefix_paths;
        cp.old_paths = old_paths;
        cp.old_neighborhood_cost = old_cost;
        cp.prefix_accumulated_cost = computeCost(prefix_paths);
        cp.incumbent_version = incumbent_version;
        cp.incumbent_sum_of_costs = incumbent_sum_of_costs;
        cp.agent_versions_at_save = agent_trajectory_versions;
        cp.base_rng_seed = rng_seed;
        cp.rng_seed_at_unfinished_agent = rng_seed + r_done * 10007;
        cp.accumulated_active_time_s = outcome.duration_s;
        outcome.repeated_expansions = outcome.interruption.current_agent_expansions;
        cp.repeated_expansions = outcome.interruption.current_agent_expansions;
        total_repeated_expansions += outcome.repeated_expansions;

        // Rollback candidate paths from path_table so incumbent is untouched
        rollback();

        if (budget.isGlobalDeadlineExceeded()) {
            outcome.type = RepairOutcomeType::STOPPED_DEADLINE;
            return outcome;
        }

        total_interruptions_observed++;
        if (r_done > 0) {
            total_interruptions_with_completed_prefix++;
            if (enable_snapshot_capture) {
                InterruptedRepairSnapshot snap;
                snap.snapshot_id = static_cast<int>(captured_snapshots.size());
                snap.attempt_id = cur_attempt_id;
                snap.is_resumed = false;
                snap.incumbent_version = incumbent_version;
                snap.incumbent_cost = incumbent_sum_of_costs;
                snap.incumbent_paths = incumbent_paths;
                snap.priority_order = priority_order;
                snap.candidate_agents.assign(priority_order.begin(), priority_order.begin() + r_done);
                snap.candidate_paths = prefix_paths;
                snap.slice_duration_s = budget.max_time_seconds;
                snap.remaining_budget_s = budget.getRemainingGlobalSeconds();
                snap.slice_expansions = budget.current_expansions;
                captured_snapshots.push_back(std::move(snap));
            }
        }

        // Check if partial acceptance is enabled on every interruption (PA_EVERY_INTERRUPTION_FILTERED)
        if (enable_partial_acceptance && selective_recovery_policy == SelectiveRecoveryPolicy::PA_EVERY_INTERRUPTION_FILTERED && r_done > 0) {
            std::vector<int> candidate_agents(priority_order.begin(), priority_order.begin() + r_done);
            bool pa_committed = processInterruptedPartialAcceptance(candidate_agents, prefix_paths, budget, outcome, cp.checkpoint_id, cp.incumbent_version);
            outcome.duration_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
            if (pa_committed || outcome.type == RepairOutcomeType::STOPPED_DEADLINE) {
                return outcome;
            }
        }

        // Natural eviction recovery hook (Section 8)
        if (checkpoint_store.getCapacity() > 0) {
            int victim_slot = checkpoint_store.findEvictionVictimSlot();
            if (victim_slot >= 0 && (selective_recovery_policy == SelectiveRecoveryPolicy::PA_EVICTION_ONLY ||
                                     selective_recovery_policy == SelectiveRecoveryPolicy::PA_EVICTION_AND_TERMINAL)) {
                attemptEvictionRecovery(victim_slot, budget);
            }
        }

        // Save into bounded checkpoint store (K in {0, 1, 2})
        checkpoint_store.saveCheckpoint(cp);
        total_interrupted_slices++;
        return outcome;
    }

    // All agents in neighborhood routed!
    int64_t new_cost = computeCost(prefix_paths);
    outcome.new_cost = new_cost;
    outcome.cost_delta = new_cost - old_cost;

    last_completed_candidate = incumbent_paths;
    for (size_t i = 0; i < priority_order.size(); ++i)
        last_completed_candidate[priority_order[i]] = prefix_paths[i];

    if (new_cost < old_cost) {
        if (!planIsCollisionFree(last_completed_candidate)) {
            rollback();
            outcome.type = RepairOutcomeType::REJECTED_INVALID;
            total_failed_repairs++;
            return outcome;
        }
        if (budget.isGlobalDeadlineExceeded()) {
            rollback();
            outcome.type = RepairOutcomeType::REJECTED_LATE_COMMIT;
            return outcome;
        }
        if (!publishCandidate(last_completed_candidate, priority_order, incumbent_version, true, &budget)) {
            outcome.type = budget.isGlobalDeadlineExceeded() ? RepairOutcomeType::REJECTED_LATE_COMMIT : RepairOutcomeType::REJECTED_INVALID;
            return outcome;
        }
        outcome.type = RepairOutcomeType::COMPLETED_ACCEPTED;
        total_accepted_repairs++;
    } else {
        rollback();
        outcome.type = RepairOutcomeType::COMPLETED_NOT_IMPROVING;
        total_non_improving_repairs++;
    }

    return outcome;
}

bool ResumableRepairEngine::planIsCollisionFree(const std::vector<Path>& plan) const {
    // Hash-per-timestep vertex/edge-swap check with stay-at-goal semantics (O(T * |A|)).
    int max_len = 0;
    for (const auto& p : plan) {
        if (p.empty()) return false;
        max_len = std::max(max_len, (int)p.size());
    }
    auto pos = [&](int a, int t) {
        const Path& p = plan[a];
        return p[std::min<size_t>((size_t)t, p.size() - 1)].location;
    };
    std::unordered_map<int, int> occ;
    occ.reserve(plan.size() * 2);
    for (int t = 0; t < max_len; ++t) {
        occ.clear();
        for (int a = 0; a < (int)plan.size(); ++a) {
            if (!occ.emplace(pos(a, t), a).second) return false;   // vertex conflict
        }
        if (t + 1 < max_len) {
            for (int a = 0; a < (int)plan.size(); ++a) {
                int u = pos(a, t), v = pos(a, t + 1);
                if (u == v) continue;
                auto it = occ.find(v);                              // who was at v at time t?
                if (it != occ.end() && it->second != a && pos(it->second, t + 1) == u) return false; // swap
            }
        }
    }
    return true;
}

bool ResumableRepairEngine::commitExternalTrajectory(int agent_id, const Path& new_path, bool allow_worsening) {
    if (agent_id < 0 || agent_id >= num_agents || new_path.empty()) return false;
    if (new_path.front().location != instance.getStarts()[agent_id]) return false;
    if (new_path.back().location != instance.getGoals()[agent_id]) return false;

    // Check grid-step validity (no obstacles, valid moves)
    for (size_t t = 0; t < new_path.size(); ++t) {
        int loc = new_path[t].location;
        if (loc < 0 || loc >= instance.map_size || instance.isObstacle(loc)) return false;
        if (t + 1 < new_path.size()) {
            int next_loc = new_path[t + 1].location;
            if (!instance.validMove(loc, next_loc)) return false;
        }
    }

    int64_t old_c = computeCost({incumbent_paths[agent_id]});
    int64_t new_c = computeCost({new_path});
    if (!allow_worsening && new_c >= old_c) {
        return false; // Reject worsening paths in production/enforced mode
    }

    // Verify candidate whole plan is collision-free
    std::vector<Path> cand = incumbent_paths;
    cand[agent_id] = new_path;
    if (!planIsCollisionFree(cand)) return false;

    return publishCandidate(std::move(cand), {agent_id}, incumbent_version, !allow_worsening);
}

void ResumableRepairEngine::setIncumbentPathForTest(int agent_id, const Path& new_path) {
    if (agent_id < 0 || agent_id >= num_agents || new_path.empty()) return;
    // Deliberately permits invalid geometry for compatibility fault injection.
    auto candidate = incumbent_paths;
    candidate[agent_id] = new_path;
    const auto soc = computeCost(candidate);
    PathTable reservations(instance.map_size);
    for (int a = 0; a < num_agents; ++a) reservations.insertPath(a, candidate[a]);
    incumbent_paths.swap(candidate);
    swapReservations(path_table, reservations);
    incumbent_sum_of_costs = soc;
    ++agent_trajectory_versions[agent_id];
    ++incumbent_version;
    last_validated_incumbent_version = 0;
    last_validated_incumbent_cost = -1;
}

CompatibilityReport ResumableRepairEngine::validateCheckpoint(int slot_idx, RetentionPolicy policy) const {
    const MacroPrefixCheckpoint* cp = checkpoint_store.getCheckpoint(slot_idx);
    if (!cp) {
        CompatibilityReport rep;
        rep.detail = "No checkpoint in slot";
        return rep;
    }
    return ConservativeConstraintValidator::validate(
        *cp, incumbent_paths, agent_trajectory_versions, incumbent_version, policy);
}

CompatibilityReport ResumableRepairEngine::validateCheckpoint(int slot_idx, bool selective_retention) const {
    return validateCheckpoint(slot_idx, selective_retention ? RetentionPolicy::WHOLE_CHECKPOINT_SELECTIVE : RetentionPolicy::DISCARD_ON_CHANGE);
}

RepairOutcome ResumableRepairEngine::resumeCheckpoint(int slot_idx, SearchBudget& budget, bool selective_retention) {
    return resumeCheckpoint(slot_idx, budget, selective_retention ? RetentionPolicy::WHOLE_CHECKPOINT_SELECTIVE : RetentionPolicy::DISCARD_ON_CHANGE);
}

RepairOutcome ResumableRepairEngine::resumeCheckpoint(int slot_idx, SearchBudget& budget, RetentionPolicy policy) {
    RepairOutcome outcome;
    int cur_attempt_id = ++total_repair_attempts_counter;

    // Check global deadline before resuming
    if (budget.deadline_exceeded || budget.isGlobalDeadlineExceeded() || budget.getElapsedSeconds() >= budget.max_time_seconds) {
        outcome.type = RepairOutcomeType::STOPPED_DEADLINE;
        return outcome;
    }

    MacroPrefixCheckpoint* cp = checkpoint_store.getCheckpoint(slot_idx);
    if (!cp) {
        outcome.type = RepairOutcomeType::FAILED_NO_PATH;
        return outcome;
    }

    // Conservative constraint compatibility validation (Phase 4: Component 2 / Partial Recovery)
    CompatibilityReport rep = ConservativeConstraintValidator::validate(
        *cp, incumbent_paths, agent_trajectory_versions, incumbent_version, policy);
    last_compat_report = rep;

    if (!rep.isCompatible()) {
        checkpoint_store.markDiscarded(slot_idx);
        outcome.type = RepairOutcomeType::DISCARDED_INCOMPATIBLE;
        return outcome;
    }

    // Prepare resumed checkpoint accounting privately until a new slice is saved.
    auto* stored_checkpoint = cp;
    MacroPrefixCheckpoint staged_checkpoint = *cp;
    cp = &staged_checkpoint;

    auto t0 = std::chrono::steady_clock::now();

    outcome.neighborhood_size = (int)cp->priority_order.size();
    outcome.repeated_expansions = cp->repeated_expansions;

    // Handle Partial Prefix Recovery truncation if applicable
    if (rep.verdict == CompatibilityVerdict::COMPATIBLE_PARTIAL_PREFIX) {
        int orig_r_done = cp->r_done;
        int k = rep.valid_prefix_len;
        outcome.was_partial_recovery = true;
        outcome.salvaged_prefix_len = k;
        outcome.original_prefix_len = orig_r_done;

        // Truncate checkpoint prefix to the valid k paths
        cp->r_done = k;
        cp->prefix_paths.resize(k);
        cp->prefix_accumulated_cost = computeCost(cp->prefix_paths);
        cp->rng_seed_at_unfinished_agent = cp->base_rng_seed + k * 10007;
        cp->consecutive_stagnant_attempts = 0;
    }

    std::vector<int> neighborhood = cp->neighborhood_agents;
    std::vector<int> priority_order = cp->priority_order;
    std::vector<Path> old_paths = cp->old_paths;
    std::vector<Path> prefix_paths = cp->prefix_paths;
    int r_done = cp->r_done;
    uint32_t agent_seed = cp->rng_seed_at_unfinished_agent;

    // Calculate CURRENT cost of the neighbourhood in the incumbent (acceptance baseline)
    int64_t current_neighborhood_cost = 0;
    for (int a : neighborhood) {
        if (!checkedAdd(current_neighborhood_cost, computeCost({incumbent_paths[a]}), current_neighborhood_cost)) throw std::overflow_error("Neighborhood cost");
    }
    outcome.old_cost = current_neighborhood_cost;

    PathTable path_table = this->path_table; // private search reservations
    // Remove old paths of neighborhood from path_table
    for (size_t i = 0; i < neighborhood.size(); ++i) {
        path_table.deletePath(neighborhood[i], incumbent_paths[neighborhood[i]]);
    }

    // Rebuild reservations from saved prefix paths P_{1..r_done}
    for (int i = 0; i < r_done; ++i) {
        path_table.insertPath(priority_order[i], prefix_paths[i]);
    }

    auto t1 = std::chrono::steady_clock::now();
    double restore_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    cp->restore_time_us = restore_us;

    ConstraintTable constraint_table(instance.num_of_cols, instance.map_size, &path_table);

    auto rollback = [&]() {
        for (size_t i = 0; i < prefix_paths.size(); ++i) {
            path_table.deletePath(priority_order[i], prefix_paths[i]);
        }
        for (size_t i = 0; i < neighborhood.size(); ++i) {
            path_table.insertPath(neighborhood[i], incumbent_paths[neighborhood[i]]);
        }
    };

    while (r_done < (int)priority_order.size()) {
        if (!budget.checkAgentBoundary(r_done)) {
            break;
        }

        int agent_id = priority_order[r_done];
        uint64_t exp_before = budget.current_expansions;
        Path path = findPathWithBudget(agent_id, constraint_table, budget, agent_seed);

        if (budget.interrupted) {
            outcome.interruption.current_agent_expansions = budget.current_expansions - exp_before;
            break;
        }

        if (path.empty()) {
            rollback();
            checkpoint_store.markDiscarded(slot_idx);
            auto t_end = std::chrono::steady_clock::now();
            outcome.duration_s = std::chrono::duration<double>(t_end - t0).count();
            outcome.total_expansions = budget.current_expansions;
            outcome.type = RepairOutcomeType::FAILED_NO_PATH;
            total_failed_repairs++;
            return outcome;
        }

        prefix_paths.push_back(path);
        path_table.insertPath(agent_id, path);
        r_done++;
        agent_seed += 10007;

        if (computeCost(prefix_paths) >= current_neighborhood_cost) {
            rollback();
            checkpoint_store.markDiscarded(slot_idx, CheckpointRemoval::Consumption);
            auto t_end = std::chrono::steady_clock::now();
            outcome.duration_s = std::chrono::duration<double>(t_end - t0).count();
            outcome.total_expansions = budget.current_expansions;
            outcome.type = RepairOutcomeType::COMPLETED_NOT_IMPROVING;
            total_non_improving_repairs++;
            return outcome;
        }
    }

    auto t_end = std::chrono::steady_clock::now();
    outcome.duration_s = std::chrono::duration<double>(t_end - t0).count();
    outcome.total_expansions = budget.current_expansions;

    if (budget.interrupted || r_done < (int)priority_order.size()) {
        outcome.type = RepairOutcomeType::INTERRUPTED_SLICE;
        outcome.interruption.neighborhood_size = (int)priority_order.size();
        outcome.interruption.completed_prefix_len = r_done;
        outcome.interruption.unfinished_agent_id = (r_done < (int)priority_order.size()) ? priority_order[r_done] : -1;
        outcome.interruption.has_reusable_prefix = (r_done > 0);

        // Update checkpoint
        if (r_done == cp->r_done) {
            cp->consecutive_stagnant_attempts++;
        } else {
            cp->consecutive_stagnant_attempts = 0;
        }
        cp->r_done = r_done;
        cp->prefix_paths = prefix_paths;
        cp->prefix_accumulated_cost = computeCost(prefix_paths);
        cp->rng_seed_at_unfinished_agent = agent_seed;
        cp->accumulated_active_time_s += outcome.duration_s;
        outcome.repeated_expansions = outcome.interruption.current_agent_expansions;
        cp->repeated_expansions += outcome.interruption.current_agent_expansions;
        total_repeated_expansions += outcome.repeated_expansions;

        rollback();
        if (budget.isGlobalDeadlineExceeded()) {
            checkpoint_store.markExpired(slot_idx);
            outcome.type = RepairOutcomeType::STOPPED_DEADLINE;
            return outcome;
        }

        total_interruptions_observed++;
        if (r_done > 0) {
            total_interruptions_with_completed_prefix++;
            if (enable_snapshot_capture) {
                InterruptedRepairSnapshot snap;
                snap.snapshot_id = static_cast<int>(captured_snapshots.size());
                snap.attempt_id = cur_attempt_id;
                snap.is_resumed = true;
                snap.incumbent_version = incumbent_version;
                snap.incumbent_cost = incumbent_sum_of_costs;
                snap.incumbent_paths = incumbent_paths;
                snap.priority_order = priority_order;
                snap.candidate_agents.assign(priority_order.begin(), priority_order.begin() + r_done);
                snap.candidate_paths = prefix_paths;
                snap.slice_duration_s = budget.max_time_seconds;
                snap.remaining_budget_s = budget.getRemainingGlobalSeconds();
                snap.slice_expansions = budget.current_expansions;
                captured_snapshots.push_back(std::move(snap));
            }
        }

        // Check if partial acceptance is enabled on every interruption (PA_EVERY_INTERRUPTION_FILTERED)
        if (enable_partial_acceptance && selective_recovery_policy == SelectiveRecoveryPolicy::PA_EVERY_INTERRUPTION_FILTERED && r_done > 0) {
            std::vector<int> candidate_agents(priority_order.begin(), priority_order.begin() + r_done);
            bool pa_committed = processInterruptedPartialAcceptance(candidate_agents, prefix_paths, budget, outcome, cp->checkpoint_id, cp->incumbent_version);
            outcome.duration_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (pa_committed) {
                checkpoint_store.markDiscarded(slot_idx, CheckpointRemoval::Recovery);
                return outcome;
            }
            if (outcome.type == RepairOutcomeType::STOPPED_DEADLINE) {
                checkpoint_store.markExpired(slot_idx);
                return outcome;
            }
        }

        static_assert(std::is_nothrow_swappable<MacroPrefixCheckpoint>::value,
                      "Checkpoint publication must not throw");
        std::swap(*stored_checkpoint, staged_checkpoint);
        total_interrupted_slices++;
        return outcome;
    }

    // All agents finished!
    int64_t new_cost = computeCost(prefix_paths);
    outcome.new_cost = new_cost;
    outcome.cost_delta = new_cost - current_neighborhood_cost;

    if (outcome.cost_delta < 0) {
        // Independent final-path validation before commitment
        std::vector<Path> candidate_paths = incumbent_paths;
        for (size_t i = 0; i < priority_order.size(); ++i) {
            candidate_paths[priority_order[i]] = prefix_paths[i];
        }

        bool valid = planIsCollisionFree(candidate_paths);

        if (valid) {
            if (budget.isGlobalDeadlineExceeded()) {
                checkpoint_store.markExpired(slot_idx);
                outcome.type = RepairOutcomeType::REJECTED_LATE_COMMIT;
                return outcome;
            }
            if (!publishCandidate(std::move(candidate_paths), priority_order, incumbent_version, true, &budget)) {
                outcome.type = budget.isGlobalDeadlineExceeded() ? RepairOutcomeType::REJECTED_LATE_COMMIT : RepairOutcomeType::REJECTED_INVALID;
                return outcome;
            }
            checkpoint_store.markCommitted(slot_idx);
            outcome.type = RepairOutcomeType::COMPLETED_ACCEPTED;
            total_accepted_repairs++;
        } else {
            rollback();
            checkpoint_store.markDiscarded(slot_idx);
            outcome.type = RepairOutcomeType::FAILED_NO_PATH;
            total_failed_repairs++;
        }
    } else {
        rollback();
        checkpoint_store.markDiscarded(slot_idx, CheckpointRemoval::Consumption);
        outcome.type = RepairOutcomeType::COMPLETED_NOT_IMPROVING;
        total_non_improving_repairs++;
    }

    return outcome;
}

void ResumableRepairEngine::recordRecovery(const std::string& trigger, uint64_t id,
        uint64_t creation_version, int completed, const PartialAcceptanceResult& result) {
    RecoveryEvent event{trigger, id, creation_version, result.source_incumbent_version, completed, result};
    event.result.selected_paths.clear();
    if (result.status == PartialAcceptanceStatus::COMMITTED && !publications.empty()) {
        event.commit_start = publications.back().start;
        event.commit_complete = publications.back().complete;
    }
    recovery_events.push_back(std::move(event));
}

bool ResumableRepairEngine::processInterruptedPartialAcceptance(
    const std::vector<int>& candidate_agents,
    const std::vector<Path>& candidate_paths,
    SearchBudget& budget,
    RepairOutcome& outcome, uint64_t checkpoint_id, uint64_t creation_version) {

    auto t_pa_start = std::chrono::steady_clock::now();
    total_partial_acceptance_attempts++;
    PartialAcceptanceResult res = evaluatePartialAcceptance(candidate_agents, candidate_paths, &budget);

    if (res.status == PartialAcceptanceStatus::BUDGET_SKIPPED) {
        total_partial_acceptance_budget_skipped++;
        if (budget.isGlobalBudgetExhausted(0.0)) {
            outcome.type = RepairOutcomeType::STOPPED_DEADLINE;
        }
        res.total_time_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t_pa_start).count();
        total_partial_acceptance_time_us += res.total_time_us;
        last_partial_acceptance_result = res;
        partial_acceptance_history.push_back(res);
        recordRecovery("interruption", checkpoint_id, creation_version, static_cast<int>(candidate_agents.size()), res);
        return false;
    }

    if (res.status == PartialAcceptanceStatus::OPTIMAL_SELECTION && res.predicted_gain > 0) {
        total_partial_acceptance_positive_opps++;

        if (budget.isGlobalBudgetExhausted(0.0)) {
            res.status = PartialAcceptanceStatus::BUDGET_SKIPPED;
            res.detail = "Global deadline reached prior to commit";
            total_partial_acceptance_budget_skipped++;
            outcome.type = RepairOutcomeType::STOPPED_DEADLINE;
            res.total_time_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t_pa_start).count();
            total_partial_acceptance_time_us += res.total_time_us;
            last_partial_acceptance_result = res;
            partial_acceptance_history.push_back(res);
        recordRecovery("interruption", checkpoint_id, creation_version, static_cast<int>(candidate_agents.size()), res);
            return false;
        }

        bool committed = commitPartialAcceptance(res.selected_agent_ids, res.selected_paths, incumbent_version, res, &budget);
        res.total_time_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t_pa_start).count();
        total_partial_acceptance_time_us += res.total_time_us;
        last_partial_acceptance_result = res;
        partial_acceptance_history.push_back(res);
        recordRecovery("interruption", checkpoint_id, creation_version, static_cast<int>(candidate_agents.size()), res);

        if (committed) {
            total_partial_acceptance_committed++;
            total_partial_acceptance_gain += res.verified_gain;
            outcome.type = RepairOutcomeType::COMPLETED_ACCEPTED;
            outcome.new_cost = incumbent_sum_of_costs;
            outcome.cost_delta = -res.verified_gain;
            total_accepted_repairs++;
            return true;
        }

        if (res.status == PartialAcceptanceStatus::BUDGET_SKIPPED) {
            total_partial_acceptance_budget_skipped++;
            if (budget.isGlobalBudgetExhausted(0.0)) {
                outcome.type = RepairOutcomeType::STOPPED_DEADLINE;
            }
        }
        return false;
    }

    res.total_time_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t_pa_start).count();
    total_partial_acceptance_time_us += res.total_time_us;
    last_partial_acceptance_result = res;
    partial_acceptance_history.push_back(res);
        recordRecovery("interruption", checkpoint_id, creation_version, static_cast<int>(candidate_agents.size()), res);
    return false;
}

PartialAcceptanceResult ResumableRepairEngine::evaluatePartialAcceptance(
    const std::vector<int>& candidate_agents,
    const std::vector<Path>& candidate_replacements,
    SearchBudget* budget) {

    auto t_start = std::chrono::steady_clock::now();
    PartialAcceptanceResult res;
    res.policy = partial_acceptance_policy;
    res.source_incumbent_version = incumbent_version;
    int active_stage = -1; // -1 bookkeeping, 0 extraction, 1 scalar filter, 2 graph, 3 selection
    auto stage_start = t_start;
    auto finish = [&]() {
        const auto now = std::chrono::steady_clock::now();
        const double stage_us = std::chrono::duration<double, std::micro>(now - stage_start).count();
        if (active_stage == 0 && res.extraction_time_us == 0) res.extraction_time_us = stage_us;
        if (active_stage == 1 && res.filter_time_us == 0) res.filter_time_us = stage_us;
        if (active_stage == 2 && res.graph_build_time_us == 0) res.graph_build_time_us = stage_us;
        if (active_stage == 3 && res.selection_time_us == 0) res.selection_time_us = stage_us;
        res.total_time_us = std::chrono::duration<double, std::micro>(now - t_start).count();
        return res;
    };

    if (!enable_partial_acceptance || partial_acceptance_policy == PartialAcceptancePolicy::OFF) {
        res.status = PartialAcceptanceStatus::DISABLED;
        res.detail = "Partial acceptance is disabled";
        return finish();
    }

    if (candidate_agents.empty() || candidate_agents.size() != candidate_replacements.size()) {
        res.status = candidate_agents.empty() ? PartialAcceptanceStatus::NO_CANDIDATES : PartialAcceptanceStatus::INVALID_INPUT;
        res.detail = candidate_agents.empty() ? "No candidate agents provided" : "Candidate count mismatch";
        return finish();
    }

    if (budget && budget->isGlobalBudgetExhausted(budget->partial_acceptance_reserve_s)) {
        res.status = PartialAcceptanceStatus::BUDGET_SKIPPED;
        res.detail = "Global deadline or safety reserve exceeded prior to partial acceptance";
        return finish();
    }

    active_stage = 0; stage_start = std::chrono::steady_clock::now();
    // 1. Validate incumbent solution validity (Assumption 1)
    std::string inc_err;
    if (!validateIncumbent(inc_err)) {
        res.status = PartialAcceptanceStatus::INVALID_INPUT;
        res.detail = "Incumbent solution is invalid: " + inc_err;
        return finish();
    }

    // 2. Assemble replacement candidates, verify uniqueness (Assumption 4) and kinematics (Assumption 2)
    std::vector<ReplacementCandidate> candidates;
    candidates.reserve(candidate_agents.size());
    std::unordered_set<int> seen_agents;
    for (size_t i = 0; i < candidate_agents.size(); ++i) {
        int a = candidate_agents[i];
        if (a < 0 || a >= num_agents) {
            res.status = PartialAcceptanceStatus::INVALID_INPUT;
            res.detail = "Candidate agent ID out of range: " + std::to_string(a);
            return finish();
        }
        if (!seen_agents.insert(a).second) {
            res.status = PartialAcceptanceStatus::INVALID_INPUT;
            res.detail = "Duplicate candidate replacement for agent " + std::to_string(a);
            return finish();
        }

        std::string kin_err;
        if (!PartialAcceptanceOptimizer::checkPathKinematics(candidate_replacements[i],
                                                            instance.getStarts()[a],
                                                            instance.getGoals()[a],
                                                            instance,
                                                            kin_err, budget)) {
            res.status = PartialAcceptanceStatus::INVALID_INPUT;
            if (budget && budget->isGlobalBudgetExhausted(0.0)) res.status = PartialAcceptanceStatus::BUDGET_SKIPPED;
            res.detail = "Kinematic violation for agent " + std::to_string(a) + ": " + kin_err;
            return finish();
        }

        ReplacementCandidate c;
        c.agent_id = a;
        c.replacement_path = candidate_replacements[i];
        c.old_path = incumbent_paths[a];
        c.cost_old = PartialAcceptanceOptimizer::computeArrivalCost(c.old_path, instance.getGoals()[a]);
        c.cost_new = PartialAcceptanceOptimizer::computeArrivalCost(c.replacement_path, instance.getGoals()[a]);
        if (!checkedSub(static_cast<int64_t>(c.cost_old), static_cast<int64_t>(c.cost_new), c.weight)) {
            res.status = PartialAcceptanceStatus::INVALID_INPUT;
            res.detail = "Arithmetic overflow computing weight for agent " + std::to_string(a);
            return finish();
        }
        candidates.push_back(c);
    }

    res.candidate_count = static_cast<int>(candidates.size());
    for (const auto& candidate : candidates) {
        if (candidate.weight > 0) ++res.positive_weight_count;
        else if (candidate.weight == 0) ++res.zero_weight_count;
        else ++res.negative_weight_count;
    }
    auto t_extract_end = std::chrono::steady_clock::now();
    res.extraction_time_us = std::chrono::duration<double, std::micro>(t_extract_end - t_start).count();
    total_extraction_time_us += res.extraction_time_us;

    // Cheap optimistic gain filter: U = sum(max(w_i, 0)) (Claude Prompt Section 5)
    auto t_filter_start = std::chrono::steady_clock::now();
    active_stage = 1; stage_start = t_filter_start;
    int64_t upper_bound_u = 0;
    bool filter_overflow = false;
    if (!PartialAcceptanceOptimizer::computeOptimisticUpperBound(candidates, upper_bound_u, filter_overflow) || filter_overflow) {
        res.status = PartialAcceptanceStatus::INVALID_INPUT;
        res.detail = "Arithmetic overflow computing optimistic upper bound U";
        return finish();
    }
    auto t_filter_end = std::chrono::steady_clock::now();
    res.filter_time_us = std::chrono::duration<double, std::micro>(t_filter_end - t_filter_start).count();
    res.optimistic_upper_bound_U = upper_bound_u;
    res.bound_computed = true;

    total_filter_evaluations++;
    total_filter_time_us += res.filter_time_us;

    if (upper_bound_u == 0) {
        total_filter_skips++;
        res.status = PartialAcceptanceStatus::NO_GAIN;
        res.filter_skipped = true;
        res.detail = "Skipped by cheap optimistic gain filter (U == 0)";
        res.prep_time_us = res.extraction_time_us + res.filter_time_us;
        res.total_time_us = res.prep_time_us;
        return finish();
    }

    // Pairwise conflicts constrain subsets; they do not invalidate all input.
    auto t_prep_end = std::chrono::steady_clock::now();
    double prep_us = std::chrono::duration<double, std::micro>(t_prep_end - t_start).count();

    if (budget && budget->isGlobalBudgetExhausted(0.0)) {
        res.status = PartialAcceptanceStatus::BUDGET_SKIPPED;
        res.prep_time_us = prep_us;
        res.total_time_us = prep_us;
        res.detail = "Global deadline exceeded after input preparation";
        return finish();
    }

    // 4. Build dependency graph against all incumbent agents (Assumption 6)
    auto t_graph_start = std::chrono::steady_clock::now();
    active_stage = 2; stage_start = t_graph_start;
    DependencyGraph graph = PartialAcceptanceOptimizer::buildDependencyGraph(candidates, incumbent_paths, instance, budget);
    if (graph.cancelled) {
        res.status = PartialAcceptanceStatus::BUDGET_SKIPPED;
        res.detail = "Deadline during dependency/conflict construction";
        return finish();
    }
    auto t_graph_end = std::chrono::steady_clock::now();
    double graph_us = std::chrono::duration<double, std::micro>(t_graph_end - t_graph_start).count();

    if (budget && budget->isGlobalBudgetExhausted(0.0)) {
        res.status = PartialAcceptanceStatus::BUDGET_SKIPPED;
        res.prep_time_us = prep_us;
        res.graph_build_time_us = graph_us;
        res.total_time_us = prep_us + graph_us;
        res.detail = "Global deadline exceeded after dependency graph construction";
        return finish();
    }

    const double extraction_us = res.extraction_time_us, filter_us = res.filter_time_us;
    active_stage = 3; stage_start = std::chrono::steady_clock::now();
    // 5. Solve according to policy
    if (partial_acceptance_policy == PartialAcceptancePolicy::EXACT_CLOSURE) {
        res = PartialAcceptanceOptimizer::solveExactClosure(graph, candidates, incumbent_version, budget);
    } else if (partial_acceptance_policy == PartialAcceptancePolicy::GREEDY_DEPENDENCY) {
        res = PartialAcceptanceOptimizer::solveGreedyDependencyAware(graph, candidates, incumbent_version, budget);
    } else {
        res.status = PartialAcceptanceStatus::DISABLED;
        return finish();
    }

    res.extraction_time_us = extraction_us;
    res.filter_time_us = filter_us;
    res.optimistic_upper_bound_U = upper_bound_u;
    res.bound_computed = true;
    res.prep_time_us = prep_us;
    res.graph_build_time_us = graph_us;
    res.total_time_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t_start).count();
    return finish();
}

bool ResumableRepairEngine::commitPartialAcceptance(const std::vector<int>& selected_agents,
                                                    const std::vector<Path>& replacement_paths,
                                                    uint64_t expected_incumbent_version,
                                                    PartialAcceptanceResult& out_result,
                                                    SearchBudget* budget) {
    auto t0 = std::chrono::steady_clock::now();
    if (incumbent_version != expected_incumbent_version) {
        out_result.status = PartialAcceptanceStatus::VERSION_CHANGED;
        out_result.detail = "Incumbent version changed: expected " + std::to_string(expected_incumbent_version) +
                            " but current is " + std::to_string(incumbent_version);
        return false;
    }
    if (budget && budget->isGlobalBudgetExhausted(0.0)) {
        out_result.status = PartialAcceptanceStatus::BUDGET_SKIPPED;
        out_result.detail = "Global deadline exceeded prior to commit validation";
        return false;
    }
    if (selected_agents.empty() || selected_agents.size() != replacement_paths.size()) {
        out_result.status = PartialAcceptanceStatus::INVALID_INPUT;
        out_result.detail = "Selected agent count does not match replacement path count";
        return false;
    }

    // Construct candidate complete plan and validate kinematics + uniqueness
    std::vector<Path> candidate_paths = incumbent_paths;
    std::unordered_set<int> seen_agents;
    const auto& starts = instance.getStarts();
    const auto& goals = instance.getGoals();
    for (size_t i = 0; i < selected_agents.size(); ++i) {
        int a = selected_agents[i];
        if (a < 0 || a >= num_agents) {
            out_result.status = PartialAcceptanceStatus::INVALID_INPUT;
            out_result.detail = "Agent ID out of range: " + std::to_string(a);
            return false;
        }
        if (!seen_agents.insert(a).second) {
            out_result.status = PartialAcceptanceStatus::INVALID_INPUT;
            out_result.detail = "Duplicate agent ID in commit batch: " + std::to_string(a);
            return false;
        }
        std::string kin_err;
        if (!PartialAcceptanceOptimizer::checkPathKinematics(replacement_paths[i], starts[a], goals[a], instance, kin_err, budget)) {
            out_result.status = PartialAcceptanceStatus::VALIDATION_FAILED;
            out_result.detail = "Replacement path failed kinematic check for agent " + std::to_string(a) + ": " + kin_err;
            return false;
        }
        candidate_paths[a] = replacement_paths[i];
    }

    // Validate independent-of-search collision freedom across whole plan
    auto t_val_start = std::chrono::steady_clock::now();
    int64_t candidate_soc = 0;
    std::string val_err;
    if (!PartialAcceptanceOptimizer::validateCompletePlanIndependently(candidate_paths, instance, candidate_soc, val_err, budget)) {
        out_result.validation_time_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t_val_start).count();
        out_result.status = PartialAcceptanceStatus::VALIDATION_FAILED;
        if (budget && budget->isGlobalBudgetExhausted(0.0)) out_result.status = PartialAcceptanceStatus::BUDGET_SKIPPED;
        out_result.detail = "Candidate plan failed whole-plan validation: " + val_err;
        return false;
    }
    auto t_val_end = std::chrono::steady_clock::now();
    out_result.validation_time_us = std::chrono::duration<double, std::micro>(t_val_end - t_val_start).count();

    // Independently compute authoritative arrival-cost Sum of Costs for current incumbent
    int64_t old_arrival_soc = 0;
    for (int a = 0; a < num_agents; ++a) {
        int64_t arr = PartialAcceptanceOptimizer::computeArrivalCost(incumbent_paths[a], goals[a]);
        if (!checkedAdd(old_arrival_soc, static_cast<int64_t>(arr), old_arrival_soc)) {
            out_result.status = PartialAcceptanceStatus::INVALID_INPUT;
            out_result.detail = "Overflow computing incumbent arrival cost";
            return false;
        }
    }

    int64_t gain = 0;
    if (!checkedSub(old_arrival_soc, candidate_soc, gain) || gain <= 0) {
        out_result.status = PartialAcceptanceStatus::NO_GAIN;
        out_result.detail = "Verified gain is non-positive: " + std::to_string(gain);
        return false;
    }

    // Final pre-commit rechecks: version, positive gain, and remaining global budget
    if (incumbent_version != expected_incumbent_version) {
        out_result.status = PartialAcceptanceStatus::VERSION_CHANGED;
        out_result.detail = "Incumbent version changed before atomic commit";
        return false;
    }
    if (budget && budget->isGlobalBudgetExhausted(0.0)) {
        out_result.status = PartialAcceptanceStatus::BUDGET_SKIPPED;
        out_result.detail = "Global deadline exceeded after validation; late commit rejected";
        return false;
    }

    std::string success_detail = "Successfully committed " + std::to_string(selected_agents.size()) +
        " agent paths with cost reduction " + std::to_string(gain);
    if (!publishCandidate(std::move(candidate_paths), selected_agents, expected_incumbent_version, true, budget)) {
        out_result.commit_time_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t_val_end).count();
        out_result.status = (budget && budget->isGlobalBudgetExhausted(0.0)) ?
            PartialAcceptanceStatus::BUDGET_SKIPPED : PartialAcceptanceStatus::VALIDATION_FAILED;
        out_result.detail = "Staged publication rejected; incumbent preserved";
        return false;
    }
    out_result.verified_gain = gain;

    auto t_commit_end = std::chrono::steady_clock::now();
    out_result.commit_time_us = std::chrono::duration<double, std::micro>(t_commit_end - t_val_end).count();
    out_result.total_time_us += std::chrono::duration<double, std::micro>(t_commit_end - t0).count();

    out_result.status = PartialAcceptanceStatus::COMMITTED;
    out_result.detail.swap(success_detail);
    return true;
}

bool ResumableRepairEngine::attemptEvictionRecovery(int victim_slot, SearchBudget& budget) {
    if (eviction_recovery_active) return false;
    const auto* victim = checkpoint_store.getCheckpoint(victim_slot);
    if (!victim) return false;
    eviction_recovery_active = true;
    struct Guard { bool& active; ~Guard() { active = false; } } guard{eviction_recovery_active};
    ++total_eviction_pa_attempts;
    const auto t0 = std::chrono::steady_clock::now();
    const int count = std::max(0, std::min(victim->r_done,
        static_cast<int>(std::min(victim->prefix_paths.size(), victim->priority_order.size()))));
    const auto id = victim->checkpoint_id, version = victim->incumbent_version;
    std::vector<int> agents(victim->priority_order.begin(), victim->priority_order.begin() + count);
    std::vector<Path> paths(victim->prefix_paths.begin(), victim->prefix_paths.begin() + count);
    auto result = evaluatePartialAcceptance(agents, paths, &budget);
    if (result.filter_skipped) ++total_eviction_pa_filter_skips;
    bool committed = false;
    if (result.status == PartialAcceptanceStatus::OPTIMAL_SELECTION && result.predicted_gain > 0)
        committed = commitPartialAcceptance(result.selected_agent_ids, result.selected_paths, incumbent_version, result, &budget);
    if (committed) {
        ++total_eviction_pa_committed;
        total_eviction_pa_gain += result.verified_gain;
        ++total_accepted_repairs;
    }
    result.total_time_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    recordRecovery("capacity_eviction", id, version, count, result);
    total_eviction_pa_time_us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    return committed;
}

bool ResumableRepairEngine::attemptTerminalRecovery(SearchBudget& budget) {
    total_terminal_recovery_calls++;
    auto t0 = std::chrono::steady_clock::now();
    bool any_committed = false;

    // Collect suspended slots in deterministic FIFO order (ascending checkpoint_id)
    std::vector<int> suspended_slots;
    for (int s = 0; s < checkpoint_store.getCapacity(); ++s) {
        const MacroPrefixCheckpoint* cp = checkpoint_store.getCheckpoint(s);
        if (cp && cp->lifecycle == CheckpointLifecycle::SUSPENDED && cp->r_done > 0 && !cp->prefix_paths.empty()) {
            suspended_slots.push_back(s);
        }
    }
    std::sort(suspended_slots.begin(), suspended_slots.end(), [&](int a, int b) {
        return checkpoint_store.getCheckpoint(a)->checkpoint_id < checkpoint_store.getCheckpoint(b)->checkpoint_id;
    });

    for (int s : suspended_slots) {
        if (budget.isGlobalBudgetExhausted(0.0)) {
            break;
        }

        const MacroPrefixCheckpoint* cp = checkpoint_store.getCheckpoint(s);
        if (!cp || cp->lifecycle != CheckpointLifecycle::SUSPENDED || cp->r_done <= 0 || cp->prefix_paths.empty()) {
            continue;
        }

        total_terminal_pa_examined++;
        int r_done = std::min(static_cast<int>(cp->r_done), static_cast<int>(std::min(cp->prefix_paths.size(), cp->priority_order.size())));
        std::vector<int> candidate_agents(cp->priority_order.begin(), cp->priority_order.begin() + r_done);
        std::vector<Path> candidate_paths(cp->prefix_paths.begin(), cp->prefix_paths.begin() + r_done);

        const auto checkpoint_id = cp->checkpoint_id, creation_version = cp->incumbent_version;
        // Evaluate against current incumbent (recomputed if a prior checkpoint committed!)
        PartialAcceptanceResult res = evaluatePartialAcceptance(candidate_agents, candidate_paths, &budget);

        if (res.filter_skipped) {
            total_terminal_pa_filter_skips++;
        }

        if (res.status == PartialAcceptanceStatus::OPTIMAL_SELECTION && res.predicted_gain > 0) {
            bool committed = commitPartialAcceptance(res.selected_agent_ids, res.selected_paths, incumbent_version, res, &budget);
            if (committed) {
                checkpoint_store.markCommitted(s, CheckpointRemoval::Recovery);
                total_terminal_pa_committed++;
                total_terminal_pa_gain += res.verified_gain;
                total_accepted_repairs++;
                any_committed = true;
            }
        }
        recordRecovery("terminal", checkpoint_id, creation_version, r_done, res);
    }

    double elapsed_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    total_terminal_pa_time_us += elapsed_us;
    return any_committed;
}

} // namespace rbl
