// RouteBudget Lab - Maximum-Gain Partial Acceptance of Interrupted MAPF-LNS Repairs
// Implementation of dependency-based partial acceptance, exact min-cut reduction,
// nonnegative backward-exclusion reference, greedy baseline, independent path-mixture
// exhaustive oracle, and trajectory conflict checkers.

#include "rbl_partial_acceptance.hpp"

#include <queue>
#include <cassert>
#include <iostream>

namespace rbl {

int64_t PartialAcceptanceOptimizer::computeArrivalCost(const Path& path, int goal_location) {
    if (path.empty()) return 0;
    const int goal = goal_location >= 0 ? goal_location : path.back().location;
    return authoritativeArrivalCost(path, [goal](const auto& p) { return p.location == goal; });
}

bool PartialAcceptanceOptimizer::computeOptimisticUpperBound(
    const std::vector<ReplacementCandidate>& candidates,
    int64_t& out_u,
    bool& out_overflow) {
    out_u = 0;
    out_overflow = false;
    for (const auto& c : candidates) {
        if (c.weight > 0) {
            if (!checkedAdd(out_u, c.weight, out_u)) {
                out_overflow = true;
                return false;
            }
        }
    }
    return true;
}

bool PartialAcceptanceOptimizer::checkPathKinematics(const Path& path, int start_loc, int goal_loc, const Instance& instance, std::string& err, SearchBudget* budget, RecoveryStage stage) {
    if (path.size() >= static_cast<size_t>(std::numeric_limits<int>::max())) {
        err = "Path exceeds supported reservation time range";
        return false;
    }
    if (path.empty()) {
        err = "Path is empty";
        return false;
    }
    if (path.front().location != start_loc) {
        err = "Path front " + std::to_string(path.front().location) + " does not match start " + std::to_string(start_loc);
        return false;
    }
    if (path.back().location != goal_loc) {
        err = "Path back " + std::to_string(path.back().location) + " does not match goal " + std::to_string(goal_loc);
        return false;
    }
    for (size_t t = 0; t < path.size(); ++t) {
        if (budget && !budget->pollStage(stage)) { err = "Deadline during route scan"; return false; }
        int loc = path[t].location;
        if (loc < 0 || loc >= instance.map_size) {
            err = "Location out of bounds at t=" + std::to_string(t);
            return false;
        }
        if (instance.isObstacle(loc)) {
            err = "Location is an obstacle at t=" + std::to_string(t);
            return false;
        }
        if (t + 1 < path.size()) {
            int next_loc = path[t + 1].location;
            if (!instance.validMove(loc, next_loc)) {
                err = "Invalid grid move from " + std::to_string(loc) + " to " + std::to_string(next_loc) + " at t=" + std::to_string(t);
                return false;
            }
        }
    }
    return true;
}

bool PartialAcceptanceOptimizer::checkTrajectoryConflict(const Path& path_a, const Path& path_b, std::string* reason, SearchBudget* budget, RecoveryStage stage) {
    if (path_a.empty() || path_b.empty()) return false;
    const size_t max_len = std::max(path_a.size(), path_b.size());

    auto pos_a = [&](size_t t) {
        return path_a[std::min<size_t>((size_t)t, path_a.size() - 1)].location;
    };
    auto pos_b = [&](size_t t) {
        return path_b[std::min<size_t>((size_t)t, path_b.size() - 1)].location;
    };

    // Check vertex conflicts for all timesteps through max_len (includes persistent goal occupancy)
    for (size_t t = 0; t < max_len; ++t) {
        if (budget && !budget->pollStage(stage)) return true;
        int loc_a = pos_a(t);
        int loc_b = pos_b(t);
        if (loc_a == loc_b) {
            if (reason) {
                *reason = "Vertex conflict at t=" + std::to_string(t) + " at loc=" + std::to_string(loc_a);
            }
            return true;
        }
    }

    // Check edge swap conflicts for all steps through max_len - 1
    for (size_t t = 0; t + 1 < max_len; ++t) {
        if (budget && !budget->pollStage(stage)) return true;
        int u_a = pos_a(t), v_a = pos_a(t + 1);
        int u_b = pos_b(t), v_b = pos_b(t + 1);
        if (u_a == v_b && v_a == u_b && u_a != v_a) {
            if (reason) {
                *reason = "Edge swap conflict at t=" + std::to_string(t) + "->" + std::to_string(t + 1) +
                          " between (" + std::to_string(u_a) + "," + std::to_string(v_a) + ") and (" +
                          std::to_string(u_b) + "," + std::to_string(v_b) + ")";
            }
            return true;
        }
    }

    return false;
}

bool PartialAcceptanceOptimizer::checkPairwiseCandidateCollisions(const std::vector<ReplacementCandidate>& candidates, std::string& err) {
    std::unordered_set<int> seen_agents;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!seen_agents.insert(candidates[i].agent_id).second) {
            err = "Duplicate replacement candidate for agent " + std::to_string(candidates[i].agent_id);
            return false;
        }
        for (size_t j = i + 1; j < candidates.size(); ++j) {
            std::string reason;
            if (checkTrajectoryConflict(candidates[i].replacement_path, candidates[j].replacement_path, &reason)) {
                err = "Pairwise candidate conflict between agent " + std::to_string(candidates[i].agent_id) +
                      " and agent " + std::to_string(candidates[j].agent_id) + ": " + reason;
                return false;
            }
        }
    }
    return true;
}

bool PartialAcceptanceOptimizer::validateCompletePlanIndependently(const std::vector<Path>& plan,
                                                                   const Instance& instance,
                                                                   int64_t& out_soc,
                                                                   std::string& out_err, SearchBudget* budget) {
    int m = instance.getDefaultNumberOfAgents();
    if ((int)plan.size() != m) {
        out_err = "Plan agent count " + std::to_string(plan.size()) + " does not match instance agent count " + std::to_string(m);
        return false;
    }

    const auto& starts = instance.getStarts();
    const auto& goals = instance.getGoals();
    int64_t total_soc = 0;
    int max_len = 0;

    for (int a = 0; a < m; ++a) {
        std::string kin_err;
        if (!checkPathKinematics(plan[a], starts[a], goals[a], instance, kin_err, budget, RecoveryStage::Validation)) {
            out_err = "Agent " + std::to_string(a) + " kinematic violation: " + kin_err;
            return false;
        }
        int64_t arr_cost = computeArrivalCost(plan[a], goals[a]);
        if (!checkedAdd(total_soc, static_cast<int64_t>(arr_cost), total_soc)) {
            out_err = "Sum of costs overflow";
            return false;
        }
        // Reservation/search timesteps are int; reject unsupported lengths before narrowing.
        int length = 0;
        if (!checkedCostToInt(static_cast<int64_t>(plan[a].size()), length) || length == std::numeric_limits<int>::max()) {
            out_err = "Path exceeds supported timestep range";
            return false;
        }
        max_len = std::max(max_len, length);
    }

    auto pos = [&](int a, int t) -> int {
        const Path& p = plan[a];
        return p[std::min<size_t>((size_t)t, p.size() - 1)].location;
    };

    // Independent whole-plan collision check across all timesteps through max_len (O(m * max_len))
    std::vector<int> occ_curr(instance.map_size, -1);
    for (size_t t = 0; t < max_len; ++t) {
        if (budget && !budget->pollStage(RecoveryStage::Validation)) { out_err = "Validation deadline"; return false; }
        for (int a = 0; a < m; ++a) {
            int loc = pos(a, t);
            if (occ_curr[loc] != -1) {
                int other = occ_curr[loc];
                out_err = "Vertex collision at t=" + std::to_string(t) +
                          " loc=" + std::to_string(loc) +
                          " between agents " + std::to_string(other) + " and " + std::to_string(a);
                return false;
            }
            occ_curr[loc] = a;
        }
        if (t < max_len) {
            for (int a = 0; a < m; ++a) {
                int u = pos(a, t);
                int v = pos(a, t + 1);
                if (u != v) {
                    int other = occ_curr[v];
                    if (other != -1 && other != a && pos(other, t + 1) == u) {
                        out_err = "Edge swap collision at t=" + std::to_string(t) + "->" + std::to_string(t + 1) +
                                  " between agents " + std::to_string(a) + " and " + std::to_string(other);
                        return false;
                    }
                }
            }
        }
        for (int a = 0; a < m; ++a) {
            occ_curr[pos(a, t)] = -1;
        }
    }

    out_soc = total_soc;
    return true;
}

void PartialAcceptanceOptimizer::computeGraphTelemetry(DependencyGraph& graph) {
    int r = graph.num_candidates;
    graph.positive_weight_count = 0;
    graph.zero_weight_count = 0;
    graph.negative_weight_count = 0;
    graph.forbidden_count = 0;
    graph.dependency_arc_count = 0;
    graph.transitively_excluded_count = 0;
    graph.feasible_negative_weight_count = 0;
    graph.transitively_excluded.assign(r, false);

    if (r <= 0 || (int)graph.weights.size() != r || (int)graph.forbidden.size() != r || (int)graph.adj.size() != r) {
        return;
    }

    std::vector<std::vector<int>> rev_adj(r);
    std::queue<int> q;

    for (int u = 0; u < r; ++u) {
        int64_t w = graph.weights[u];
        if (w > 0) graph.positive_weight_count++;
        else if (w == 0) graph.zero_weight_count++;
        else graph.negative_weight_count++;

        if (graph.forbidden[u]) {
            graph.forbidden_count++;
            graph.transitively_excluded[u] = true;
            q.push(u);
        }

        for (int v : graph.adj[u]) {
            if (v >= 0 && v < r) {
                graph.dependency_arc_count++;
                rev_adj[v].push_back(u);
            }
        }
    }

    // Propagate exclusion backward along reverse dependency arcs:
    // If u -> v and v is excluded, then u cannot be selected either.
    while (!q.empty()) {
        int v = q.front();
        q.pop();
        for (int u : rev_adj[v]) {
            if (!graph.transitively_excluded[u]) {
                graph.transitively_excluded[u] = true;
                if (!graph.forbidden[u]) {
                    graph.transitively_excluded_count++;
                }
                q.push(u);
            }
        }
    }

    for (int u = 0; u < r; ++u) {
        if (!graph.transitively_excluded[u] && graph.weights[u] < 0) {
            graph.feasible_negative_weight_count++;
        }
    }
}

DependencyGraph PartialAcceptanceOptimizer::buildDependencyGraph(const std::vector<ReplacementCandidate>& candidates,
                                                                 const std::vector<Path>& incumbent_paths,
                                                                 const Instance& instance, SearchBudget* budget) {
    (void)instance;
    DependencyGraph graph;
    int r = (int)candidates.size();
    graph.num_candidates = r;
    graph.candidate_agents.resize(r);
    graph.weights.resize(r);
    graph.adj.assign(r, {});
    graph.forbidden.assign(r, false);
    graph.external_blockers.assign(r, {});

    for (int i = 0; i < r; ++i) {
        int a_id = candidates[i].agent_id;
        graph.candidate_agents[i] = a_id;
        graph.agent_to_candidate[a_id] = i;
        graph.weights[i] = candidates[i].weight;
    }

    int num_incumbent = (int)incumbent_paths.size();

    // For every candidate i in R and every incumbent agent j in A (j != i):
    for (int u = 0; u < r; ++u) {
        int a_i = candidates[u].agent_id;
        const Path& q_i = candidates[u].replacement_path;

        for (int j = 0; j < num_incumbent; ++j) {
            if (budget && !budget->pollStage(RecoveryStage::Dependency)) { graph.cancelled = true; return graph; }
            if (j == a_i) continue; // Do not conflict against own old path

            std::string reason;
            if (checkTrajectoryConflict(q_i, incumbent_paths[j], &reason, budget, RecoveryStage::Dependency)) {
                auto it = graph.agent_to_candidate.find(j);
                if (it != graph.agent_to_candidate.end()) {
                    // Conflicting agent j is also in candidate set R -> dependency u -> v
                    int v = it->second;
                    if (std::find(graph.adj[u].begin(), graph.adj[u].end(), v) == graph.adj[u].end()) {
                        graph.adj[u].push_back(v);
                        graph.conflict_reasons.push_back("Agent " + std::to_string(a_i) +
                            " replacement conflicts with incumbent path of candidate agent " +
                            std::to_string(j) + " -> dependency " + std::to_string(a_i) + " requires " + std::to_string(j));
                    }
                } else {
                    // Conflicting agent j is NOT in candidate set R -> candidate u is forbidden
                    if (std::find(graph.external_blockers[u].begin(), graph.external_blockers[u].end(), j) == graph.external_blockers[u].end()) {
                        graph.external_blockers[u].push_back(j);
                    }
                    if (!graph.forbidden[u]) {
                        graph.forbidden[u] = true;
                        graph.conflict_reasons.push_back("Agent " + std::to_string(a_i) +
                            " replacement conflicts with incumbent path of non-candidate agent " +
                            std::to_string(j) + " (" + reason + ") -> agent " + std::to_string(a_i) + " is FORBIDDEN");
                    }
                }
            }
        }
    }

    {
        std::unordered_set<int> pos_blockers;
        for (int u = 0; u < r; ++u) {
            if (graph.weights[u] > 0) {
                for (int b : graph.external_blockers[u]) pos_blockers.insert(b);
            }
        }
        graph.positive_gain_external_blockers.assign(pos_blockers.begin(), pos_blockers.end());
        std::sort(graph.positive_gain_external_blockers.begin(), graph.positive_gain_external_blockers.end());
    }

    for (int u = 0; u < r; ++u)
        for (int v = u + 1; v < r; ++v)
            if (checkTrajectoryConflict(candidates[u].replacement_path, candidates[v].replacement_path, nullptr, budget, RecoveryStage::Conflict))
                graph.incompatibilities.emplace_back(u, v);
    if (budget && budget->isGlobalBudgetExhausted(0.0)) { graph.cancelled = true; return graph; }
    computeGraphTelemetry(graph);
    return graph;
}

// -----------------------------------------------------------------------------
// Dinic's Algorithm for Maximum-Weight Closure / Minimum Cut with Checked Math
// -----------------------------------------------------------------------------
namespace {

struct FlowEdge {
    int to;
    int rev;
    int64_t cap;
    int64_t flow;
};

class DinicMaxFlow {
public:
    int n;
    std::vector<std::vector<FlowEdge>> adj;
    std::vector<int> level;
    std::vector<int> ptr;
    int64_t total_added_cap = 0;

    explicit DinicMaxFlow(int num_nodes) : n(num_nodes), adj(num_nodes), level(num_nodes), ptr(num_nodes) {}

    // Add directed edge (from -> to) with capacity cap.
    // Accumulates duplicate forward edges between the same (from, to) pair using checked arithmetic.
    bool addEdge(int from, int to, int64_t cap) {
        if (from < 0 || from >= n || to < 0 || to >= n || cap < 0) return false;
        if (!checkedAdd(total_added_cap, cap, total_added_cap)) {
            return false;
        }
        // Check if a forward edge (from -> to) with non-zero initial capacity already exists
        for (auto& edge : adj[from]) {
            if (edge.to == to && edge.cap > 0) {
                if (!checkedAdd(edge.cap, cap, edge.cap)) {
                    return false;
                }
                return true;
            }
        }
        FlowEdge a{to, (int)adj[to].size(), cap, 0};
        FlowEdge b{from, (int)adj[from].size(), 0, 0};
        adj[from].push_back(a);
        adj[to].push_back(b);
        return true;
    }

    bool bfs(int s, int t, SearchBudget* budget, bool& aborted, bool& overflow_err) {
        std::fill(level.begin(), level.end(), -1);
        level[s] = 0;
        std::queue<int> q;
        q.push(s);
        while (!q.empty()) {
            if (budget && !budget->checkFlowStep()) {
                aborted = true;
                return false;
            }
            int v = q.front();
            q.pop();
            for (const auto& edge : adj[v]) {
                int64_t rem = 0;
                if (!checkedSub(edge.cap, edge.flow, rem)) {
                    overflow_err = true;
                    return false;
                }
                if (rem > 0 && level[edge.to] == -1) {
                    level[edge.to] = level[v] + 1;
                    q.push(edge.to);
                }
            }
        }
        return level[t] != -1;
    }

    int64_t dfs(int v, int t, int64_t pushed, SearchBudget* budget, bool& aborted, bool& overflow_err) {
        if (pushed == 0) return 0;
        if (v == t) return pushed;
        if (budget && !budget->checkFlowStep()) {
            aborted = true;
            return 0;
        }
        for (int& cid = ptr[v]; cid < (int)adj[v].size(); ++cid) {
            auto& edge = adj[v][cid];
            int tr = edge.to;
            int64_t rem = 0;
            if (!checkedSub(edge.cap, edge.flow, rem)) {
                overflow_err = true;
                return 0;
            }
            if (level[v] + 1 != level[tr] || rem <= 0) continue;
            int64_t tr_pushed = dfs(tr, t, std::min(pushed, rem), budget, aborted, overflow_err);
            if (aborted || overflow_err) return 0;
            if (tr_pushed == 0) continue;
            if (!checkedAdd(edge.flow, tr_pushed, edge.flow) ||
                !checkedSub(adj[tr][edge.rev].flow, tr_pushed, adj[tr][edge.rev].flow)) {
                overflow_err = true;
                return 0;
            }
            return tr_pushed;
        }
        return 0;
    }

    int64_t maxFlow(int s, int t, SearchBudget* budget, bool& aborted, bool& overflow_err) {
        aborted = false;
        overflow_err = false;
        int64_t flow = 0;
        while (bfs(s, t, budget, aborted, overflow_err)) {
            if (aborted || overflow_err) return 0;
            std::fill(ptr.begin(), ptr.end(), 0);
            while (int64_t pushed = dfs(s, t, std::numeric_limits<int64_t>::max(), budget, aborted, overflow_err)) {
                if (aborted || overflow_err) return 0;
                if (!checkedAdd(flow, pushed, flow)) {
                    overflow_err = true;
                    return 0;
                }
            }
            if (aborted || overflow_err) return 0;
        }
        return flow;
    }

    std::vector<bool> getReachableFromSource(int s) const {
        std::vector<bool> visited(n, false);
        std::queue<int> q;
        q.push(s);
        visited[s] = true;
        while (!q.empty()) {
            int u = q.front();
            q.pop();
            for (const auto& edge : adj[u]) {
                int64_t rem = 0;
                if (checkedSub(edge.cap, edge.flow, rem) && rem > 0 && !visited[edge.to]) {
                    visited[edge.to] = true;
                    q.push(edge.to);
                }
            }
        }
        return visited;
    }
};

bool validateGraphAndCandidates(const DependencyGraph& graph,
                                const std::vector<ReplacementCandidate>& candidates,
                                std::string& err) {
    int r = graph.num_candidates;
    if (r < 0 || r != (int)candidates.size() ||
        (int)graph.candidate_agents.size() != r ||
        (int)graph.weights.size() != r ||
        (int)graph.adj.size() != r ||
        (int)graph.forbidden.size() != r) {
        err = "DependencyGraph dimensions do not match candidate count";
        return false;
    }
    for (const auto& edge : graph.incompatibilities) {
        if (edge.first < 0 || edge.second < 0 || edge.first >= r || edge.second >= r || edge.first == edge.second) {
            err = "Invalid incompatibility endpoints"; return false;
        }
    }
    std::unordered_set<int> seen_agents;
    for (int i = 0; i < r; ++i) {
        int a_id = graph.candidate_agents[i];
        if (a_id < 0 || a_id != candidates[i].agent_id) {
            err = "Candidate agent ID mismatch at node " + std::to_string(i);
            return false;
        }
        if (!seen_agents.insert(a_id).second) {
            err = "Duplicate candidate agent ID " + std::to_string(a_id);
            return false;
        }
        for (int v : graph.adj[i]) {
            if (v < 0 || v >= r) {
                err = "Dependency arc target out of range at node " + std::to_string(i);
                return false;
            }
        }
    }
    return true;
}

void copyGraphTelemetryToResult(const DependencyGraph& g, PartialAcceptanceResult& res) {
    res.candidate_count = g.num_candidates;
    res.positive_weight_count = g.positive_weight_count;
    res.zero_weight_count = g.zero_weight_count;
    res.negative_weight_count = g.negative_weight_count;
    res.forbidden_count = g.forbidden_count;
    res.transitively_excluded_count = g.transitively_excluded_count;
    res.feasible_negative_weight_count = g.feasible_negative_weight_count;
    res.dependency_arc_count = g.dependency_arc_count;
    res.incompatibility_count = static_cast<int>(g.incompatibilities.size());
}

} // anonymous namespace

PartialAcceptanceResult PartialAcceptanceOptimizer::solveExactClosure(const DependencyGraph& input_graph,
                                                                     const std::vector<ReplacementCandidate>& candidates,
                                                                     uint64_t incumbent_version,
                                                                     SearchBudget* budget) {
    auto t_start = std::chrono::steady_clock::now();
    PartialAcceptanceResult res;
    res.policy = PartialAcceptancePolicy::EXACT_CLOSURE;
    res.source_incumbent_version = incumbent_version;

    if (budget && budget->isGlobalBudgetExhausted(0.0)) {
        res.status = PartialAcceptanceStatus::BUDGET_SKIPPED;
        res.detail = "Global deadline exceeded prior to closure selection";
        return res;
    }

    if (!input_graph.incompatibilities.empty()) {
        res.status = PartialAcceptanceStatus::INVALID_INPUT;
        res.detail = "Closure-only selector does not support pairwise incompatibilities";
        return res;
    }
    int r = input_graph.num_candidates;
    if (r == 0 && candidates.empty()) {
        res.status = PartialAcceptanceStatus::NO_CANDIDATES;
        res.detail = "Candidate set R is empty";
        return res;
    }

    std::string struct_err;
    if (!validateGraphAndCandidates(input_graph, candidates, struct_err)) {
        res.status = PartialAcceptanceStatus::INVALID_INPUT;
        res.detail = struct_err;
        return res;
    }

    DependencyGraph graph = input_graph;
    computeGraphTelemetry(graph);
    copyGraphTelemetryToResult(graph, res);
    res.used_nonnegative_fast_path = (graph.feasible_negative_weight_count == 0);

    // Checked arithmetic for total_pos_weight, sum_abs_weights, and M = 1 + sum(|w_i|)
    int64_t total_pos_weight = 0;
    int64_t sum_abs_weights = 0;

    for (int i = 0; i < r; ++i) {
        int64_t w = graph.weights[i];
        int64_t abs_w = 0;
        if (!checkedAbs(w, abs_w)) {
            res.status = PartialAcceptanceStatus::INVALID_INPUT;
            res.detail = "Arithmetic overflow computing |w_i| at node " + std::to_string(i);
            return res;
        }
        if (w > 0) {
            if (!checkedAdd(total_pos_weight, w, total_pos_weight)) {
                res.status = PartialAcceptanceStatus::INVALID_INPUT;
                res.detail = "Arithmetic overflow accumulating positive weights";
                return res;
            }
        }
        if (!checkedAdd(sum_abs_weights, abs_w, sum_abs_weights)) {
            res.status = PartialAcceptanceStatus::INVALID_INPUT;
            res.detail = "Arithmetic overflow accumulating sum(|w_i|)";
            return res;
        }
    }

    int64_t M = 0;
    if (!checkedAdd(1, sum_abs_weights, M) || M <= total_pos_weight) {
        res.status = PartialAcceptanceStatus::INVALID_INPUT;
        res.detail = "Arithmetic overflow computing capacity M = 1 + sum(|w_i|)";
        return res;
    }

    // Source s = r, Sink t = r + 1
    int s = r;
    int t = r + 1;
    DinicMaxFlow flow_network(r + 2);

    // Build network with checked capacity accumulation:
    for (int i = 0; i < r; ++i) {
        int64_t w = graph.weights[i];
        if (w > 0) {
            if (!flow_network.addEdge(s, i, w)) {
                res.status = PartialAcceptanceStatus::INVALID_INPUT;
                res.detail = "Capacity overflow adding source edge";
                return res;
            }
        } else if (w < 0) {
            int64_t neg_w = 0;
            if (!checkedAbs(w, neg_w) || !flow_network.addEdge(i, t, neg_w)) {
                res.status = PartialAcceptanceStatus::INVALID_INPUT;
                res.detail = "Capacity overflow adding sink edge";
                return res;
            }
        }

        // Forbidden nodes must not be selected: add edge i -> t with capacity M
        // (accumulates with -w_i if w_i < 0)
        if (graph.forbidden[i]) {
            if (!flow_network.addEdge(i, t, M)) {
                res.status = PartialAcceptanceStatus::INVALID_INPUT;
                res.detail = "Capacity overflow adding forbidden sink edge";
                return res;
            }
        }

        // Dependency constraints: if i is selected, j must be selected -> edge i -> j with capacity M
        for (int j : graph.adj[i]) {
            if (j == i) continue;
            if (!flow_network.addEdge(i, j, M)) {
                res.status = PartialAcceptanceStatus::INVALID_INPUT;
                res.detail = "Capacity overflow adding dependency edge";
                return res;
            }
        }
    }

    bool aborted = false;
    bool overflow_err = false;
    int64_t flow = flow_network.maxFlow(s, t, budget, aborted, overflow_err);

    if (overflow_err) {
        res.status = PartialAcceptanceStatus::INVALID_INPUT;
        res.detail = "Arithmetic overflow during max-flow residual updates";
        return res;
    }
    if (aborted) {
        res.status = PartialAcceptanceStatus::BUDGET_SKIPPED;
        res.detail = "Flow computation interrupted by budget/deadline expiry; incumbent preserved";
        return res;
    }

    std::vector<bool> reachable = flow_network.getReachableFromSource(s);

    // Nodes in candidate set reachable from s in residual graph
    std::vector<int> selected_nodes;
    int64_t selected_gain = 0;
    for (int i = 0; i < r; ++i) {
        if (reachable[i]) {
            selected_nodes.push_back(i);
            if (!checkedAdd(selected_gain, graph.weights[i], selected_gain)) {
                res.status = PartialAcceptanceStatus::INVALID_INPUT;
                res.detail = "Arithmetic overflow summing selected closure gain";
                return res;
            }
            if (graph.weights[i] > 0) res.selected_positive_weight_count++;
            else if (graph.weights[i] == 0) res.selected_zero_weight_count++;
            else res.selected_negative_weight_count++;
        }
    }

    // Verify mathematical invariants:
    // 1. No forbidden or transitively excluded node is selected
    for (int u : selected_nodes) {
        if (graph.forbidden[u] || graph.transitively_excluded[u]) {
            res.status = PartialAcceptanceStatus::INVALID_INPUT;
            res.detail = "Invariant violation: forbidden candidate was selected by max-flow cut";
            return res;
        }
    }

    // 2. Closure condition: for every u in S, every dependency v in adj[u] must be in S
    for (int u : selected_nodes) {
        for (int v : graph.adj[u]) {
            if (!reachable[v]) {
                res.status = PartialAcceptanceStatus::INVALID_INPUT;
                res.detail = "Invariant violation: dependency arc was violated in residual cut";
                return res;
            }
        }
    }

    // 3. Min-cut capacity equals total_pos_weight - selected_gain
    int64_t expected_gain = 0;
    if (!checkedSub(total_pos_weight, flow, expected_gain) || expected_gain != selected_gain) {
        res.status = PartialAcceptanceStatus::INVALID_INPUT;
        res.detail = "Invariant violation: min-cut capacity does not match selected closure weight";
        return res;
    }

    auto t_end = std::chrono::steady_clock::now();
    res.selection_time_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
    res.total_time_us = res.selection_time_us;

    if (selected_gain <= 0 || selected_nodes.empty()) {
        res.status = PartialAcceptanceStatus::NO_GAIN;
        res.predicted_gain = 0;
        res.verified_gain = 0;
        res.selected_positive_weight_count = 0;
        res.selected_zero_weight_count = 0;
        res.selected_negative_weight_count = 0;
        res.detail = "No positive feasible closure exists; keeping incumbent";
        return res;
    }

    res.status = PartialAcceptanceStatus::OPTIMAL_SELECTION;
    res.predicted_gain = selected_gain;
    res.verified_gain = selected_gain;

    for (int u : selected_nodes) {
        res.selected_agent_ids.push_back(graph.candidate_agents[u]);
        res.selected_paths.push_back(candidates[u].replacement_path);
    }

    std::ostringstream ss;
    ss << "Optimal closure selected " << selected_nodes.size() << "/" << r << " candidates with gain +" << selected_gain;
    res.detail = ss.str();
    return res;
}

// -----------------------------------------------------------------------------
// Simple Nonnegative-Case Reference (Backward Exclusion Propagation, Task 7)
// -----------------------------------------------------------------------------
PartialAcceptanceResult PartialAcceptanceOptimizer::solveNonnegativeClosureReference(
    const DependencyGraph& input_graph,
    const std::vector<ReplacementCandidate>& candidates,
    uint64_t incumbent_version,
    SearchBudget* budget) {

    auto t_start = std::chrono::steady_clock::now();
    PartialAcceptanceResult res;
    res.policy = PartialAcceptancePolicy::EXACT_CLOSURE;
    res.source_incumbent_version = incumbent_version;

    if (budget && budget->isGlobalBudgetExhausted(0.0)) {
        res.status = PartialAcceptanceStatus::BUDGET_SKIPPED;
        res.detail = "Global deadline exceeded prior to nonnegative reference selection";
        return res;
    }

    if (!input_graph.incompatibilities.empty()) {
        res.status = PartialAcceptanceStatus::INVALID_INPUT;
        res.detail = "Closure-only selector does not support pairwise incompatibilities";
        return res;
    }
    int r = input_graph.num_candidates;
    if (r == 0 && candidates.empty()) {
        res.status = PartialAcceptanceStatus::NO_CANDIDATES;
        res.detail = "Candidate set R is empty";
        return res;
    }

    std::string struct_err;
    if (!validateGraphAndCandidates(input_graph, candidates, struct_err)) {
        res.status = PartialAcceptanceStatus::INVALID_INPUT;
        res.detail = struct_err;
        return res;
    }

    DependencyGraph graph = input_graph;
    computeGraphTelemetry(graph);
    copyGraphTelemetryToResult(graph, res);

    // Precondition: all remaining selectable (non-excluded) weights must be >= 0
    if (graph.feasible_negative_weight_count > 0) {
        res.status = PartialAcceptanceStatus::INVALID_INPUT;
        res.detail = "Nonnegative reference precondition violated: selectable negative-weight candidate exists";
        return res;
    }

    res.used_nonnegative_fast_path = true;

    std::vector<int> selected_nodes;
    int64_t total_gain = 0;
    for (int u = 0; u < r; ++u) {
        if (!graph.transitively_excluded[u]) {
            selected_nodes.push_back(u);
            if (!checkedAdd(total_gain, graph.weights[u], total_gain)) {
                res.status = PartialAcceptanceStatus::INVALID_INPUT;
                res.detail = "Arithmetic overflow in nonnegative closure sum";
                return res;
            }
            if (graph.weights[u] > 0) res.selected_positive_weight_count++;
            else if (graph.weights[u] == 0) res.selected_zero_weight_count++;
        }
    }

    auto t_end = std::chrono::steady_clock::now();
    res.selection_time_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
    res.total_time_us = res.selection_time_us;

    if (total_gain <= 0 || selected_nodes.empty()) {
        res.status = PartialAcceptanceStatus::NO_GAIN;
        res.predicted_gain = 0;
        res.verified_gain = 0;
        res.selected_positive_weight_count = 0;
        res.selected_zero_weight_count = 0;
        res.detail = "Nonnegative closure reference: no positive feasible gain";
        return res;
    }

    res.status = PartialAcceptanceStatus::OPTIMAL_SELECTION;
    res.predicted_gain = total_gain;
    res.verified_gain = total_gain;
    for (int u : selected_nodes) {
        res.selected_agent_ids.push_back(graph.candidate_agents[u]);
        res.selected_paths.push_back(candidates[u].replacement_path);
    }
    res.detail = "Nonnegative backward-exclusion selected " + std::to_string(selected_nodes.size()) +
                 "/" + std::to_string(r) + " candidates with gain +" + std::to_string(total_gain);
    return res;
}

// -----------------------------------------------------------------------------
// Transitive Dependency-Aware Greedy Baseline
// -----------------------------------------------------------------------------
PartialAcceptanceResult PartialAcceptanceOptimizer::solveGreedyDependencyAware(const DependencyGraph& input_graph,
                                                                               const std::vector<ReplacementCandidate>& candidates,
                                                                               uint64_t incumbent_version,
                                                                               SearchBudget* budget) {
    auto t_start = std::chrono::steady_clock::now();
    PartialAcceptanceResult res;
    res.policy = PartialAcceptancePolicy::GREEDY_DEPENDENCY;
    res.source_incumbent_version = incumbent_version;

    if (budget && budget->isGlobalBudgetExhausted(0.0)) {
        res.status = PartialAcceptanceStatus::BUDGET_SKIPPED;
        res.detail = "Global deadline exceeded prior to greedy selection";
        return res;
    }

    int r = input_graph.num_candidates;
    if (r == 0 && candidates.empty()) {
        res.status = PartialAcceptanceStatus::NO_CANDIDATES;
        res.detail = "Candidate set R is empty";
        return res;
    }

    std::string struct_err;
    if (!validateGraphAndCandidates(input_graph, candidates, struct_err)) {
        res.status = PartialAcceptanceStatus::INVALID_INPUT;
        res.detail = struct_err;
        return res;
    }

    DependencyGraph graph = input_graph;
    computeGraphTelemetry(graph);
    copyGraphTelemetryToResult(graph, res);

    // Compute transitive closure of dependencies for each candidate
    auto getTransitiveClosure = [&](int start_node) -> std::pair<std::vector<int>, bool> {
        std::vector<bool> vis(r, false);
        std::queue<int> q;
        q.push(start_node);
        vis[start_node] = true;
        bool has_forbidden = false;

        while (!q.empty()) {
            if (budget && !budget->pollStage(RecoveryStage::Selection)) return {{}, true};
            int u = q.front();
            q.pop();
            if (graph.forbidden[u]) {
                has_forbidden = true;
            }
            for (int v : graph.adj[u]) {
                if (!vis[v]) {
                    vis[v] = true;
                    q.push(v);
                }
            }
        }
        std::vector<int> closure;
        for (int i = 0; i < r; ++i) {
            if (vis[i]) closure.push_back(i);
        }
        return {closure, has_forbidden};
    };

    // Candidate order: sort nodes with positive weight descending
    std::vector<int> pos_candidates;
    for (int i = 0; i < r; ++i) {
        if (graph.weights[i] > 0 && !graph.forbidden[i]) {
            pos_candidates.push_back(i);
        }
    }
    std::sort(pos_candidates.begin(), pos_candidates.end(), [&](int a, int b) {
        if (graph.weights[a] != graph.weights[b]) return graph.weights[a] > graph.weights[b];
        return a < b;
    });

    std::vector<bool> in_selected(r, false);
    std::vector<int> selected_nodes;
    int64_t current_gain = 0;

    for (int candidate : pos_candidates) {
        if (budget && (!budget->pollStage(RecoveryStage::Selection) || !budget->checkFlowStep())) {
            res.status = PartialAcceptanceStatus::BUDGET_SKIPPED;
            res.detail = "Greedy selection interrupted by budget/deadline expiry";
            return res;
        }
        if (in_selected[candidate]) continue;

        auto [closure, has_forbidden] = getTransitiveClosure(candidate);
        if (has_forbidden) continue;
        auto proposed = in_selected;
        for (int node : closure) proposed[node] = true;
        bool conflict = false;
        for (const auto& edge : graph.incompatibilities)
            if (proposed[edge.first] && proposed[edge.second]) { conflict = true; break; }
        if (conflict) continue;

        // Calculate marginal gain of adding unselected closure members
        int64_t marginal_gain = 0;
        std::vector<int> unselected_members;
        for (int node : closure) {
            if (!in_selected[node]) {
                unselected_members.push_back(node);
                if (!checkedAdd(marginal_gain, graph.weights[node], marginal_gain)) {
                    res.status = PartialAcceptanceStatus::INVALID_INPUT;
                    res.detail = "Arithmetic overflow in greedy marginal gain";
                    return res;
                }
            }
        }

        if (marginal_gain > 0) {
            if (!checkedAdd(current_gain, marginal_gain, current_gain)) {
                res.status = PartialAcceptanceStatus::INVALID_INPUT;
                res.detail = "Arithmetic overflow in greedy total gain";
                return res;
            }
            for (int node : unselected_members) {
                in_selected[node] = true;
                selected_nodes.push_back(node);
                if (graph.weights[node] > 0) res.selected_positive_weight_count++;
                else if (graph.weights[node] == 0) res.selected_zero_weight_count++;
                else res.selected_negative_weight_count++;
            }
        }
    }

    if (budget && budget->isGlobalBudgetExhausted(0.0)) {
        res.status = PartialAcceptanceStatus::BUDGET_SKIPPED; return res;
    }
    auto t_end = std::chrono::steady_clock::now();
    res.selection_time_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
    res.total_time_us = res.selection_time_us;

    if (current_gain <= 0 || selected_nodes.empty()) {
        res.status = PartialAcceptanceStatus::NO_GAIN;
        res.predicted_gain = 0;
        res.verified_gain = 0;
        res.selected_positive_weight_count = 0;
        res.selected_zero_weight_count = 0;
        res.selected_negative_weight_count = 0;
        res.detail = "Greedy baseline found no positive feasible group";
        return res;
    }

    res.status = PartialAcceptanceStatus::OPTIMAL_SELECTION;
    res.predicted_gain = current_gain;
    res.verified_gain = current_gain;

    for (int u : selected_nodes) {
        res.selected_agent_ids.push_back(graph.candidate_agents[u]);
        res.selected_paths.push_back(candidates[u].replacement_path);
    }

    std::ostringstream ss;
    ss << "Greedy baseline selected " << selected_nodes.size() << "/" << r << " candidates with gain +" << current_gain;
    res.detail = ss.str();
    return res;
}

// -----------------------------------------------------------------------------
// Independent Exhaustive 2^r Path-Mixture Oracle (Task 2)
// -----------------------------------------------------------------------------
PartialAcceptanceResult PartialAcceptanceOptimizer::solveExhaustiveOracle(
    const std::vector<ReplacementCandidate>& candidates,
    const std::vector<Path>& incumbent_paths,
    const Instance& instance,
    uint64_t incumbent_version, SearchBudget* budget) {

    auto t_start = std::chrono::steady_clock::now();
    PartialAcceptanceResult res;
    res.policy = PartialAcceptancePolicy::EXACT_CLOSURE;
    res.source_incumbent_version = incumbent_version;
    res.candidate_count = (int)candidates.size();

    int r = (int)candidates.size();
    if (r == 0) {
        res.status = PartialAcceptanceStatus::NO_CANDIDATES;
        res.detail = "Candidate set R is empty";
        return res;
    }
    if (r > 20) {
        res.status = PartialAcceptanceStatus::INVALID_INPUT;
        res.detail = "Exhaustive oracle requires r <= 20";
        return res;
    }

    // 1. Independently validate the incumbent solution and compute its authoritative SoC
    int64_t incumbent_soc = 0;
    std::string inc_err;
    if (!validateCompletePlanIndependently(incumbent_paths, instance, incumbent_soc, inc_err)) {
        res.status = PartialAcceptanceStatus::INVALID_INPUT;
        res.detail = "Incumbent plan is invalid: " + inc_err;
        return res;
    }

    // 2. Validate candidate assumptions (unique agent IDs, valid kinematics, pairwise collision-free)
    const auto& starts = instance.getStarts();
    const auto& goals = instance.getGoals();
    int m = instance.getDefaultNumberOfAgents();
    for (int i = 0; i < r; ++i) {
        int a = candidates[i].agent_id;
        if (a < 0 || a >= m) {
            res.status = PartialAcceptanceStatus::INVALID_INPUT;
            res.detail = "Candidate agent ID out of range: " + std::to_string(a);
            return res;
        }
        std::string kin_err;
        if (!checkPathKinematics(candidates[i].replacement_path, starts[a], goals[a], instance, kin_err)) {
            res.status = PartialAcceptanceStatus::INVALID_INPUT;
            res.detail = "Candidate replacement kinematic error for agent " + std::to_string(a) + ": " + kin_err;
            return res;
        }
    }
    std::string pair_err;
    if (!checkPairwiseCandidateCollisions(candidates, pair_err)) {
        res.status = PartialAcceptanceStatus::INVALID_INPUT;
        res.detail = pair_err;
        return res;
    }

    // 3. Enumerate all 2^r subsets, construct the complete path mixture, and validate independently
    int64_t best_gain = 0; // mask = 0 (empty subset) is always valid with gain = 0
    int best_mask = 0;
    int total_masks = 1 << r;

    for (int mask = 0; mask < total_masks; ++mask) {
        if (budget && !budget->pollStage(RecoveryStage::Selection)) {
            res.status = PartialAcceptanceStatus::BUDGET_SKIPPED; return res;
        }
        std::vector<Path> mixed_paths = incumbent_paths;
        for (int i = 0; i < r; ++i) {
            if (mask & (1 << i)) {
                mixed_paths[candidates[i].agent_id] = candidates[i].replacement_path;
            }
        }

        int64_t mixed_soc = 0;
        std::string mix_err;
        if (!validateCompletePlanIndependently(mixed_paths, instance, mixed_soc, mix_err, budget)) {
            continue; // Invalid mixture (vertex conflict, edge swap, or goal occupancy conflict)
        }

        int64_t gain = 0;
        if (!checkedSub(incumbent_soc, mixed_soc, gain)) {
            res.status = PartialAcceptanceStatus::INVALID_INPUT;
            res.detail = "Arithmetic overflow computing mixture gain";
            return res;
        }

        if (gain > best_gain) {
            best_gain = gain;
            best_mask = mask;
        }
    }

    auto t_end = std::chrono::steady_clock::now();
    res.selection_time_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
    res.total_time_us = res.selection_time_us;

    if (best_gain <= 0 || best_mask == 0) {
        res.status = PartialAcceptanceStatus::NO_GAIN;
        res.predicted_gain = 0;
        res.verified_gain = 0;
        res.detail = "Independent exhaustive oracle: no positive feasible path mixture exists";
        return res;
    }

    res.status = PartialAcceptanceStatus::OPTIMAL_SELECTION;
    res.predicted_gain = best_gain;
    res.verified_gain = best_gain;

    for (int i = 0; i < r; ++i) {
        if (best_mask & (1 << i)) {
            res.selected_agent_ids.push_back(candidates[i].agent_id);
            res.selected_paths.push_back(candidates[i].replacement_path);
        }
    }
    res.detail = "Independent exhaustive oracle selected " + std::to_string(res.selected_agent_ids.size()) +
                 "/" + std::to_string(r) + " candidates with gain +" + std::to_string(best_gain);
    return res;
}

// -----------------------------------------------------------------------------
// Abstract Graph-Level 2^r Closure Enumerator
// -----------------------------------------------------------------------------
PartialAcceptanceResult PartialAcceptanceOptimizer::solveExhaustiveGraphClosureOracle(
    const DependencyGraph& graph,
    const std::vector<ReplacementCandidate>& candidates,
    uint64_t incumbent_version, SearchBudget* budget) {

    PartialAcceptanceResult res;
    res.policy = PartialAcceptancePolicy::EXACT_CLOSURE;
    res.source_incumbent_version = incumbent_version;

    int r = graph.num_candidates;
    if (r == 0 && candidates.empty()) {
        res.status = PartialAcceptanceStatus::NO_CANDIDATES;
        return res;
    }
    std::string struct_err;
    if (r > 20 || !validateGraphAndCandidates(graph, candidates, struct_err)) {
        res.status = PartialAcceptanceStatus::INVALID_INPUT;
        res.detail = struct_err;
        return res;
    }

    int64_t best_gain = 0;
    int best_mask = 0;
    int total_masks = 1 << r;

    for (int mask = 0; mask < total_masks; ++mask) {
        if (budget && !budget->pollStage(RecoveryStage::Selection)) {
            res.status = PartialAcceptanceStatus::BUDGET_SKIPPED; return res;
        }
        bool valid = true;
        int64_t gain = 0;

        for (int i = 0; i < r; ++i) {
            if ((mask & (1 << i)) && graph.forbidden[i]) {
                valid = false;
                break;
            }
        }
        if (!valid) continue;

        for (int i = 0; i < r; ++i) {
            if (mask & (1 << i)) {
                if (!checkedAdd(gain, graph.weights[i], gain)) {
                    res.status = PartialAcceptanceStatus::INVALID_INPUT;
                    res.detail = "Arithmetic overflow in exhaustive graph oracle";
                    return res;
                }
                for (int j : graph.adj[i]) {
                    if (!(mask & (1 << j))) {
                        valid = false;
                        break;
                    }
                }
                if (!valid) break;
            }
        }
        for (const auto& edge : graph.incompatibilities)
            if ((mask & (1 << edge.first)) && (mask & (1 << edge.second))) valid = false;
        if (!valid) continue;

        if (gain > best_gain) {
            best_gain = gain;
            best_mask = mask;
        }
    }

    if (best_gain <= 0 || best_mask == 0) {
        res.status = PartialAcceptanceStatus::NO_GAIN;
        res.predicted_gain = 0;
        res.verified_gain = 0;
        res.detail = "Exhaustive graph oracle: no positive feasible closure exists";
        return res;
    }

    res.status = PartialAcceptanceStatus::OPTIMAL_SELECTION;
    res.predicted_gain = best_gain;
    res.verified_gain = best_gain;

    for (int i = 0; i < r; ++i) {
        if (best_mask & (1 << i)) {
            res.selected_agent_ids.push_back(graph.candidate_agents[i]);
            res.selected_paths.push_back(candidates[i].replacement_path);
        }
    }
    return res;
}

} // namespace rbl
