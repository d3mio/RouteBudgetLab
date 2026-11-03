// Unit tests for the independent validator. No framework: each CHECK prints on failure,
// and the process exit code is the number of failed checks (capped).
#include <cstdio>
#include <random>
#include "../src/validator/rbl_validator.hpp"

#ifndef FIXTURE_DIR
#define FIXTURE_DIR "tests/fixtures"
#endif

static int g_fail = 0, g_run = 0;
#define CHECK(cond, msg) do { ++g_run; if (!(cond)) { ++g_fail; std::printf("FAIL: %s  [%s:%d]\n", msg, __FILE__, __LINE__); } } while (0)

static rbl::Report run(const std::string& scen, int k, const std::string& paths) {
    rbl::ValidateOptions o;
    o.mapFile = std::string(FIXTURE_DIR) + "/tiny.map";
    o.scenFile = std::string(FIXTURE_DIR) + "/" + scen;
    o.pathsFile = std::string(FIXTURE_DIR) + "/" + paths;
    o.numAgents = k;
    o.pathsAreXY = true; // handwritten fixtures use (x=col, y=row)
    return rbl::validateFiles(o);
}

struct Case { const char* paths; const char* scen; int k; rbl::Status status; const char* code; };

static void fixtureTable() {
    using S = rbl::Status;
    const Case cases[] = {
        {"valid.paths",                "tiny.scen", 3, S::VALID,        nullptr},
        {"valid_no_trailing_arrow.paths","tiny.scen", 3, S::VALID,      nullptr},
        {"bad_vertex.paths",           "tiny.scen", 3, S::INVALID_PLAN, "VERTEX_CONFLICT"},
        {"bad_edge_swap.paths",        "pair.scen", 2, S::INVALID_PLAN, "EDGE_SWAP"},
        {"bad_parked_goal.paths",      "park.scen", 2, S::INVALID_PLAN, "VERTEX_CONFLICT"},
        {"bad_wrong_start.paths",      "tiny.scen", 3, S::INVALID_PLAN, "WRONG_START"},
        {"bad_goal_not_reached.paths", "tiny.scen", 3, S::INVALID_PLAN, "GOAL_NOT_REACHED"},
        {"bad_obstacle.paths",         "tiny.scen", 3, S::INVALID_PLAN, "OBSTACLE"},
        {"bad_diagonal.paths",         "tiny.scen", 3, S::INVALID_PLAN, "ILLEGAL_MOVE"},
        {"bad_jump.paths",             "tiny.scen", 3, S::INVALID_PLAN, "ILLEGAL_MOVE"},
        {"bad_out_of_bounds.paths",    "tiny.scen", 3, S::INVALID_PLAN, "OUT_OF_BOUNDS"},
        {"bad_missing_agent.paths",    "tiny.scen", 3, S::MALFORMED,    "PATHS_PARSE"},
        {"bad_extra_agent.paths",      "tiny.scen", 3, S::MALFORMED,    "PATHS_PARSE"},
        {"bad_duplicate_agent.paths",  "tiny.scen", 3, S::MALFORMED,    "PATHS_PARSE"},
        {"bad_empty_path.paths",       "tiny.scen", 3, S::MALFORMED,    "PATHS_PARSE"},
        {"bad_truncated_midcoord.paths","tiny.scen", 3, S::MALFORMED,   "PATHS_PARSE"},
        {"bad_truncated_nonewline.paths","tiny.scen", 3, S::MALFORMED,  "PATHS_PARSE"},
        {"bad_garbage.paths",          "tiny.scen", 1, S::MALFORMED,    "PATHS_PARSE"},
        {"unsolved_empty.paths",       "tiny.scen", 3, S::UNSOLVED,     "NO_PLAN"},
        {"does_not_exist.paths",       "tiny.scen", 3, S::MISSING,      "PATHS_MISSING"},
        {"valid.paths",                "tiny.scen", 4, S::MALFORMED,    "SCEN_PARSE"},  // only 3 agents in scenario
    };
    for (const Case& c : cases) {
        rbl::Report r = run(c.scen, c.k, c.paths);
        std::string name = std::string(c.paths) + " -> " + rbl::statusName(c.status);
        CHECK(r.status == c.status, name.c_str());
        if (c.code) CHECK(r.issueCounts.count(c.code) == 1, (name + " has issue " + c.code).c_str());
        else CHECK(r.issueCounts.empty(), (name + " has no issues").c_str());
    }
}

static void costChecks() {
    rbl::Report r = run("tiny.scen", 3, "valid.paths");
    CHECK(r.sumOfCosts == 12, "valid.paths sum of costs is 12 (5 + 6 + 1)");
    CHECK(r.arrival.size() == 3 && r.arrival[0] == 5 && r.arrival[1] == 6 && r.arrival[2] == 1, "per-agent arrival times 5,6,1");
    CHECK(r.sumOfPathLengths == 12, "path lengths agree when there are no trailing waits");
    CHECK(r.makespan == 6, "makespan 6");
}

// The parked-goal case must be caught *because* finished agents keep occupying their goal.
static void parkedGoalIsSpecific() {
    rbl::Report r = run("park.scen", 2, "bad_parked_goal.paths");
    bool noted = false;
    for (auto& i : r.issues) if (i.code == "VERTEX_CONFLICT" && i.detail.find("finished") != std::string::npos && i.detail.find("t=2") != std::string::npos) noted = true;
    CHECK(noted, "parked goal conflict reported at t=2 and flagged as involving a finished agent");
}

// Trailing waits at the goal: arrival time must ignore them, path length must not.
static void trailingWait() {
    rbl::Grid g; std::string err;
    CHECK(rbl::parseMap("type octile\nheight 1\nwidth 3\nmap\n...\n", g, err), "parse 1x3 map");
    std::vector<rbl::AgentSpec> a = {{{0,0},{2,0}}};
    std::vector<rbl::Path> p = {{{0,0},{1,0},{2,0},{2,0},{2,0}}};
    rbl::Report r; rbl::checkPlan(g, a, p, r);
    CHECK(r.status == rbl::Status::VALID, "trailing waits at goal are legal");
    CHECK(r.sumOfCosts == 2 && r.sumOfPathLengths == 4, "arrival=2 even though path has 4 steps");
    // leaving the goal and coming back counts the *final* arrival.
    std::vector<rbl::Path> q = {{{0,0},{1,0},{2,0},{1,0},{2,0}}};
    rbl::Report r2; rbl::checkPlan(g, a, q, r2);
    CHECK(r2.status == rbl::Status::VALID && r2.sumOfCosts == 4, "leave-and-return goal: arrival is the final arrival (4)");
}

// Start == goal agent has cost 0 and still blocks its cell.
static void zeroLength() {
    rbl::Grid g; std::string err;
    rbl::parseMap("type octile\nheight 1\nwidth 3\nmap\n...\n", g, err);
    std::vector<rbl::AgentSpec> a = {{{1,0},{1,0}}, {{0,0},{2,0}}};
    std::vector<rbl::Path> p = {{{1,0}}, {{0,0},{1,0},{2,0}}};
    rbl::Report r; rbl::checkPlan(g, a, p, r);
    CHECK(r.status == rbl::Status::INVALID_PLAN && r.issueCounts.count("VERTEX_CONFLICT"), "walking through a stationary agent is a conflict");
}

// Cross-check against a deliberately naive pairwise implementation on random plans.
static bool naiveValid(const rbl::Grid& g, const std::vector<rbl::AgentSpec>& a, const std::vector<rbl::Path>& p) {
    int m = (int)a.size(); size_t T = 0;
    for (auto& q : p) T = std::max(T, q.size());
    auto at = [&](int i, size_t t) { return p[i][std::min(t, p[i].size() - 1)]; };
    for (int i = 0; i < m; ++i) {
        if (p[i].front().x != a[i].start.x || p[i].front().y != a[i].start.y) return false;
        if (p[i].back().x != a[i].goal.x || p[i].back().y != a[i].goal.y) return false;
        for (size_t t = 0; t < p[i].size(); ++t) {
            if (!g.free(p[i][t].x, p[i][t].y)) return false;
            if (t && std::abs(p[i][t].x - p[i][t-1].x) + std::abs(p[i][t].y - p[i][t-1].y) > 1) return false;
        }
    }
    for (int i = 0; i < m; ++i) for (int j = i + 1; j < m; ++j)
        for (size_t t = 0; t < T; ++t) {
            auto ai = at(i, t), aj = at(j, t);
            if (ai.x == aj.x && ai.y == aj.y) return false;
            auto bi = at(i, t + 1), bj = at(j, t + 1);
            if (ai.x == bj.x && ai.y == bj.y && aj.x == bi.x && aj.y == bi.y) return false;
        }
    return true;
}

static rbl::Path bfsPath(const rbl::Grid& g, rbl::Cell s, rbl::Cell t) {
    std::vector<int> prev((size_t)g.width * g.height, -2); std::vector<int> q{s.y * g.width + s.x};
    prev[q[0]] = -1;
    for (size_t h = 0; h < q.size(); ++h) {
        int c = q[h], x = c % g.width, y = c / g.width;
        static const int dx[] = {1,-1,0,0}, dy[] = {0,0,1,-1};
        for (int d = 0; d < 4; ++d) { int nx = x + dx[d], ny = y + dy[d]; if (!g.free(nx, ny)) continue; int n = ny * g.width + nx; if (prev[n] == -2) { prev[n] = c; q.push_back(n); } }
    }
    rbl::Path p; for (int c = t.y * g.width + t.x; c != -1 && prev[c] != -2; c = prev[c]) p.push_back({c % g.width, c / g.width});
    std::reverse(p.begin(), p.end());
    return p.empty() ? rbl::Path{s} : p;
}

static void randomisedCrossCheck() {
    rbl::Grid g; std::string err;
    rbl::parseMap("type octile\nheight 4\nwidth 4\nmap\n....\n.@..\n....\n..@.\n", g, err);
    std::mt19937 rng(12345);
    int agree = 0, validSeen = 0, invalidSeen = 0;
    const int N = 4000;
    for (int it = 0; it < N; ++it) {
        int m = 2 + rng() % 3;
        std::vector<rbl::AgentSpec> a; std::vector<rbl::Path> p;
        std::set<std::pair<int,int>> ss, gs;
        auto pick = [&](std::set<std::pair<int,int>>& used) { while (true) { int x = rng() % 4, y = rng() % 4; if (g.free(x, y) && !used.count({x, y})) { used.insert({x, y}); return rbl::Cell{x, y}; } } };
        for (int i = 0; i < m; ++i) {
            rbl::AgentSpec s; s.start = pick(ss); s.goal = pick(gs); a.push_back(s);
            // Start from a real shortest path (BFS), insert random waits, then sometimes corrupt it.
            rbl::Path sp = bfsPath(g, s.start, s.goal), q;
            for (size_t k = 0; k < sp.size(); ++k) { q.push_back(sp[k]); while (rng() % 4 == 0) q.push_back(sp[k]); }
            if (rng() % 5 == 0 && q.size() > 1) {            // corruption: random teleport / wrong end / drop a step
                size_t k = rng() % q.size();
                switch (rng() % 3) { case 0: q[k] = {(int)(rng() % 4), (int)(rng() % 4)}; break; case 1: q.pop_back(); break; default: q.erase(q.begin() + k); }
                if (q.empty()) q.push_back(s.start);
            }
            p.push_back(q);
        }
        rbl::Report r; rbl::checkPlan(g, a, p, r);
        bool mine = (r.status == rbl::Status::VALID), naive = naiveValid(g, a, p);
        if (mine == naive) ++agree;
        (naive ? validSeen : invalidSeen)++;
    }
    std::printf("random cross-check: %d/%d agree (naive says valid in %d, invalid in %d)\n", agree, N, validSeen, invalidSeen);
    CHECK(agree == N, "validator matches naive pairwise checker on all random plans");
    CHECK(validSeen > 20, "random test actually produced some valid plans (otherwise the test proves little)");
}

int main() {
    fixtureTable(); costChecks(); parkedGoalIsSpecific(); trailingWait(); zeroLength(); randomisedCrossCheck();
    std::printf("%d checks run, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
