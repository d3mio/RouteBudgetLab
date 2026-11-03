#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstdint>

#include "rbl_resumable_engine.hpp"
#include "rbl_checkpoint.hpp"

namespace rbl {

// Feature Contract for RouteBudget Lab Remaining-Work Estimator (Component 3 / FR-04, O4)
//
// Features are extracted at decision-time BEFORE executing a fresh repair action or resuming a checkpoint.
// All features are deterministic, non-negative, and finite.
//
// Note on Open List: Macro-prefix mode discards the unfinished search frontier upon suspension.
// In strict adherence to the project contract, live open-list size is omitted from the feature vector.
//
// Note on Global Budget: remaining_global_budget_ratio is tracked when a global deadline is enforced;
// defaults to 1.0 when unconstrained.
struct SearchProgressFeatures {
    static constexpr size_t NUM_FEATURES = 13;

    double action_type = 0.0;                   // 0.0 = Fresh repair, 1.0 = Resumed checkpoint [unitless, binary]
    double neighborhood_size = 0.0;             // Total agents in neighborhood |A_c| [count]
    double completed_prefix_ratio = 0.0;        // r_done / |A_c| [ratio in 0.0 - 1.0]
    double cumulative_expansions = 0.0;         // Node expansions previously spent on this repair [count]
    double cumulative_active_time_s = 0.0;      // Active low-level search time previously spent [seconds]
    double repeated_expansions = 0.0;           // Expansions discarded at cutoff and needing repeat [count]
    double remaining_agents_count = 0.0;        // Number of agents left to route |A_c| - r_done [count]
    double remaining_agents_lowerbound_cost = 0.0;// Sum of Manhattan distance lower bounds for remaining agents [grid steps]
    double current_neighborhood_cost = 0.0;     // Sum of current incumbent costs for agents in A_c [grid steps]
    double prefix_cost_ratio = 0.0;             // prefix_accumulated_cost / current_neighborhood_cost [ratio in 0.0 - 1.0]
    double local_congestion_density = 0.0;      // External agent paths intersecting bounding box / area [agents / cell]
    double requested_slice_duration_s = 0.0;    // Allocated active search time Delta [seconds]
    double remaining_global_budget_ratio = 1.0; // Remaining global budget b_k / B [ratio in 0.0 - 1.0]

    std::vector<double> toVector() const {
        return {
            action_type,
            neighborhood_size,
            completed_prefix_ratio,
            cumulative_expansions,
            cumulative_active_time_s,
            repeated_expansions,
            remaining_agents_count,
            remaining_agents_lowerbound_cost,
            current_neighborhood_cost,
            prefix_cost_ratio,
            local_congestion_density,
            requested_slice_duration_s,
            remaining_global_budget_ratio
        };
    }

    static std::vector<std::string> featureNames() {
        return {
            "action_type",
            "neighborhood_size",
            "completed_prefix_ratio",
            "cumulative_expansions",
            "cumulative_active_time_s",
            "repeated_expansions",
            "remaining_agents_count",
            "remaining_agents_lowerbound_cost",
            "current_neighborhood_cost",
            "prefix_cost_ratio",
            "local_congestion_density",
            "requested_slice_duration_s",
            "remaining_global_budget_ratio"
        };
    }
};

class FeatureExtractor {
public:
    // Extract decision-time features for a fresh repair on neighborhood N_j
    static SearchProgressFeatures extractFresh(
        const ResumableRepairEngine& engine,
        const std::vector<int>& neighborhood,
        double slice_duration_s,
        double global_budget_ratio = 1.0)
    {
        SearchProgressFeatures f;
        f.action_type = 0.0;
        f.neighborhood_size = (double)neighborhood.size();
        f.completed_prefix_ratio = 0.0;
        f.cumulative_expansions = 0.0;
        f.cumulative_active_time_s = 0.0;
        f.repeated_expansions = 0.0;
        f.remaining_agents_count = (double)neighborhood.size();
        f.requested_slice_duration_s = slice_duration_s;
        f.remaining_global_budget_ratio = std::max(0.0, std::min(1.0, global_budget_ratio));

        const auto& instance = engine.getInstance();
        const auto& incumbent = engine.getIncumbentPaths();

        double lb_cost = 0.0;
        double cur_cost = 0.0;
        for (int a : neighborhood) {
            lb_cost += (double)instance.getManhattanDistance(instance.getStarts()[a], instance.getGoals()[a]);
            if (a >= 0 && a < (int)incumbent.size()) {
                cur_cost += (double)std::max(0, (int)incumbent[a].size() - 1);
            }
        }
        f.remaining_agents_lowerbound_cost = lb_cost;
        f.current_neighborhood_cost = cur_cost;
        f.prefix_cost_ratio = 0.0;
        f.local_congestion_density = computeCongestionDensity(instance, incumbent, neighborhood, neighborhood);

        return f;
    }

    // Extract decision-time features for resuming a suspended checkpoint in slot_idx
    static SearchProgressFeatures extractResumed(
        const ResumableRepairEngine& engine,
        int slot_idx,
        double slice_duration_s,
        double global_budget_ratio = 1.0)
    {
        SearchProgressFeatures f;
        const auto* cp = engine.getCheckpointStore().getCheckpoint(slot_idx);
        if (!cp) return f;

        f.action_type = 1.0;
        f.neighborhood_size = (double)cp->priority_order.size();
        f.completed_prefix_ratio = (cp->priority_order.empty()) ? 0.0 :
            ((double)cp->r_done / (double)cp->priority_order.size());
        f.cumulative_expansions = (double)cp->accumulated_expansions;
        f.cumulative_active_time_s = cp->accumulated_active_time_s;
        f.repeated_expansions = (double)cp->repeated_expansions;
        f.remaining_agents_count = (double)std::max(0, (int)cp->priority_order.size() - cp->r_done);
        f.requested_slice_duration_s = slice_duration_s;
        f.remaining_global_budget_ratio = std::max(0.0, std::min(1.0, global_budget_ratio));

        const auto& instance = engine.getInstance();
        const auto& incumbent = engine.getIncumbentPaths();

        // Remaining agents in priority order
        std::vector<int> remaining_agents;
        for (size_t i = cp->r_done; i < cp->priority_order.size(); ++i) {
            remaining_agents.push_back(cp->priority_order[i]);
        }

        double lb_cost = 0.0;
        for (int a : remaining_agents) {
            lb_cost += (double)instance.getManhattanDistance(instance.getStarts()[a], instance.getGoals()[a]);
        }
        f.remaining_agents_lowerbound_cost = lb_cost;

        double cur_cost = 0.0;
        for (int a : cp->neighborhood_agents) {
            if (a >= 0 && a < (int)incumbent.size()) {
                cur_cost += (double)std::max(0, (int)incumbent[a].size() - 1);
            }
        }
        f.current_neighborhood_cost = cur_cost;

        if (cur_cost > 0.0) {
            f.prefix_cost_ratio = (double)cp->prefix_accumulated_cost / cur_cost;
        } else {
            f.prefix_cost_ratio = 0.0;
        }

        f.local_congestion_density = computeCongestionDensity(instance, incumbent, cp->neighborhood_agents, remaining_agents);

        return f;
    }

private:
    // Spatial congestion density: external agents intersecting the bounding box of remaining agents / bounding box area
    static double computeCongestionDensity(
        const Instance& instance,
        const std::vector<Path>& incumbent,
        const std::vector<int>& all_neighborhood_agents,
        const std::vector<int>& target_agents)
    {
        if (target_agents.empty() || incumbent.empty()) return 0.0;

        int cols = instance.getCols();
        int min_x = cols, max_x = 0;
        int min_y = instance.map_size / cols, max_y = 0;

        for (int a : target_agents) {
            int s = instance.getStarts()[a];
            int g = instance.getGoals()[a];
            min_x = std::min({min_x, s % cols, g % cols});
            max_x = std::max({max_x, s % cols, g % cols});
            min_y = std::min({min_y, s / cols, g / cols});
            max_y = std::max({max_y, s / cols, g / cols});
        }

        // Buffer bounding box by 2 cells
        min_x = std::max(0, min_x - 2);
        max_x = std::min(cols - 1, max_x + 2);
        min_y = std::max(0, min_y - 2);
        max_y = std::min((instance.map_size / cols) - 1, max_y + 2);

        int area = (max_x - min_x + 1) * (max_y - min_y + 1);
        if (area <= 0) return 0.0;

        int external_intersecting = 0;
        for (size_t a = 0; a < incumbent.size(); ++a) {
            if (std::find(all_neighborhood_agents.begin(), all_neighborhood_agents.end(), (int)a) != all_neighborhood_agents.end()) {
                continue; // Skip agents within the neighborhood itself
            }
            const Path& p = incumbent[a];
            bool intersects = false;
            for (const auto& step : p) {
                int px = step.location % cols;
                int py = step.location / cols;
                if (px >= min_x && px <= max_x && py >= min_y && py <= max_y) {
                    intersects = true;
                    break;
                }
            }
            if (intersects) {
                external_intersecting++;
            }
        }

        return (double)external_intersecting / (double)area;
    }
};

} // namespace rbl
