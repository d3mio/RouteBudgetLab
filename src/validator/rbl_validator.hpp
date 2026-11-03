// RouteBudget Lab - independent MAPF plan validator (FR-06).
//
// Deliberately does NOT reuse any upstream (BALANCE / MAPF-LNS2) code: it has its
// own map, scenario and path parsers and its own conflict checks. It never trusts a
// solver's "success" flag or reported cost.
//
// Model (report section 4.1): static 4-connected grid, discrete steps, wait allowed,
// vertex conflicts and opposite-direction edge swaps forbidden, an agent that has
// reached the end of its path stays on that last cell forever (stay-at-goal).
#pragma once
#include "../engine/rbl_cost.hpp"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace rbl {

// ---------------------------------------------------------------- data types
struct Cell { int x = 0, y = 0; };            // x = column, y = row (MovingAI convention)

struct Grid {
    int width = 0, height = 0;
    std::vector<char> passable;               // row-major, y*width+x
    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
    bool free(int x, int y) const { return inBounds(x, y) && passable[(size_t)y * width + x]; }
};

struct AgentSpec { Cell start, goal; };

using Path = std::vector<Cell>;

// Overall verdict. Anything other than VALID means the plan must not be accepted.
enum class Status {
    VALID,          // complete plan, every check passed
    INVALID_PLAN,   // parsed fine but breaks at least one rule
    MALFORMED,      // plan/instance file cannot be interpreted (incl. suspected truncation)
    MISSING,        // file does not exist / cannot be opened
    UNSOLVED        // file exists but contains no plan at all (solver found nothing)
};

inline const char* statusName(Status s) {
    switch (s) {
        case Status::VALID: return "VALID";
        case Status::INVALID_PLAN: return "INVALID_PLAN";
        case Status::MALFORMED: return "MALFORMED";
        case Status::MISSING: return "MISSING";
        case Status::UNSOLVED: return "UNSOLVED";
    }
    return "?";
}

inline std::ostream& operator<<(std::ostream& os, Status s) {
    return os << statusName(s);
}

struct Issue { std::string code; std::string detail; };

struct Report {
    Status status = Status::MALFORMED;
    std::vector<Issue> issues;                 // first few per code (capped)
    std::map<std::string, int> issueCounts;    // full count per code
    int numAgents = 0;
    int makespan = 0;                          // longest path length in steps
    int64_t sumOfCosts = -1;                 // sum of independently computed arrival times
    int64_t sumOfPathLengths = -1;           // sum of (path.size()-1); differs if trailing waits
    std::vector<int64_t> arrival;                  // per agent arrival time T_i
    void add(const std::string& code, const std::string& detail) {
        if (++issueCounts[code] <= 5) issues.push_back({code, detail});
    }
};

// ------------------------------------------------------------------ helpers
inline std::string stripCR(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}
inline bool readWholeFile(const std::string& fn, std::string& out) {
    std::ifstream f(fn, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}
inline std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> v; std::string cur;
    for (char c : text) { if (c == '\n') { v.push_back(stripCR(cur)); cur.clear(); } else cur += c; }
    if (!cur.empty()) v.push_back(stripCR(cur));
    return v;
}
inline bool toInt(const std::string& s, int& v) {
    if (s.empty()) return false;
    size_t i = 0; if (s[0] == '-' || s[0] == '+') i = 1;
    if (i == s.size()) return false;
    long long r = 0;
    for (; i < s.size(); ++i) { if (s[i] < '0' || s[i] > '9') return false; r = r * 10 + (s[i] - '0'); if (r > 1000000000LL) return false; }
    v = (int)(s[0] == '-' ? -r : r);
    return true;
}

// -------------------------------------------------------------- map parsing
// MovingAI .map: "type octile / height H / width W / map" then H rows of W chars.
// '.', 'G', 'S' are traversable; '@','O','T','W' (and anything else) are not.
inline bool parseMap(const std::string& text, Grid& g, std::string& err) {
    auto lines = splitLines(text);
    if (lines.size() < 4) { err = "map has fewer than 4 lines"; return false; }
    if (lines[0].rfind("type", 0) != 0) { err = "first line must start with 'type'"; return false; }
    std::istringstream h(lines[1]), w(lines[2]); std::string k1, k2, v1, v2;
    h >> k1 >> v1; w >> k2 >> v2;
    if (k1 != "height" || k2 != "width" || !toInt(v1, g.height) || !toInt(v2, g.width) || g.height <= 0 || g.width <= 0) {
        err = "bad height/width header"; return false;
    }
    if (lines[3] != "map") { err = "line 4 must be 'map'"; return false; }
    if ((int)lines.size() - 4 != g.height) {
        err = "expected " + std::to_string(g.height) + " map rows, found " + std::to_string(lines.size() - 4); return false;
    }
    g.passable.assign((size_t)g.width * g.height, 0);
    for (int y = 0; y < g.height; ++y) {
        const std::string& row = lines[4 + y];
        if ((int)row.size() != g.width) { err = "row " + std::to_string(y) + " has length " + std::to_string(row.size()) + ", expected " + std::to_string(g.width); return false; }
        for (int x = 0; x < g.width; ++x) { char c = row[x]; g.passable[(size_t)y * g.width + x] = (c == '.' || c == 'G' || c == 'S'); }
    }
    return true;
}

// --------------------------------------------------------- scenario parsing
// MovingAI .scen: "version N" then tab-separated: bucket map width height sx sy gx gy optimal.
struct Scenario { std::vector<AgentSpec> agents; std::string mapName; int mapW = 0, mapH = 0; };

inline bool parseScenario(const std::string& text, int numAgents, Scenario& sc, std::string& err) {
    auto lines = splitLines(text);
    if (lines.empty() || lines[0].rfind("version", 0) != 0) { err = "scenario must start with 'version'"; return false; }
    int count = 0;
    for (size_t i = 1; i < lines.size() && count < numAgents; ++i) {
        if (lines[i].empty()) continue;
        std::vector<std::string> f; std::string cur;
        for (char c : lines[i]) { if (c == '\t' || c == ' ') { if (!cur.empty()) { f.push_back(cur); cur.clear(); } } else cur += c; }
        if (!cur.empty()) f.push_back(cur);
        if (f.size() != 9) { err = "scenario line " + std::to_string(i + 1) + " has " + std::to_string(f.size()) + " fields, expected 9"; return false; }
        int w, h; AgentSpec a;
        if (!toInt(f[2], w) || !toInt(f[3], h) || !toInt(f[4], a.start.x) || !toInt(f[5], a.start.y) ||
            !toInt(f[6], a.goal.x) || !toInt(f[7], a.goal.y)) { err = "non-numeric field on scenario line " + std::to_string(i + 1); return false; }
        if (count == 0) { sc.mapName = f[1]; sc.mapW = w; sc.mapH = h; }
        else if (f[1] != sc.mapName || w != sc.mapW || h != sc.mapH) { err = "scenario mixes maps/sizes at line " + std::to_string(i + 1); return false; }
        sc.agents.push_back(a); ++count;
    }
    if (count < numAgents) { err = "scenario has only " + std::to_string(count) + " agents, " + std::to_string(numAgents) + " requested"; return false; }
    return true;
}

// ---------------------------------------------------------- path-file parsing
// Solver format (verified in upstream Instance::savePaths): one line per agent,
//   Agent <i>:(a,b)->(a,b)->...->(a,b)->
// For MovingAI instances upstream prints (column,row) = (x,y).  'swapXY' flips this.
struct ParsedPaths { std::vector<Path> paths; bool anyContent = false; };

inline bool parsePaths(const std::string& text, int numAgents, bool pairIsXY, ParsedPaths& out, std::string& err, bool& unsolved) {
    unsolved = false;
    bool blank = true;
    for (char c : text) if (!(c == ' ' || c == '\n' || c == '\r' || c == '\t')) { blank = false; break; }
    if (blank) { unsolved = true; err = "path file is empty (no plan)"; return false; }
    if (text.back() != '\n') { err = "file does not end with a newline: possibly truncated"; return false; }
    auto lines = splitLines(text);
    out.paths.assign(numAgents, {});
    std::vector<char> seen(numAgents, 0);
    int next = 0;
    for (size_t ln = 0; ln < lines.size(); ++ln) {
        const std::string& L = lines[ln];
        if (L.empty()) continue;
        std::string where = "line " + std::to_string(ln + 1);
        if (L.rfind("Agent ", 0) != 0) { err = where + ": expected 'Agent <id>:'"; return false; }
        size_t colon = L.find(':');
        if (colon == std::string::npos) { err = where + ": missing ':'"; return false; }
        int id;
        if (!toInt(L.substr(6, colon - 6), id)) { err = where + ": bad agent id"; return false; }
        if (id < 0 || id >= numAgents) { err = where + ": agent id " + std::to_string(id) + " outside 0.." + std::to_string(numAgents - 1); return false; }
        if (seen[id]) { err = where + ": duplicate agent " + std::to_string(id); return false; }
        if (id != next) { err = where + ": agent ids out of order (expected " + std::to_string(next) + ")"; return false; }
        seen[id] = 1; ++next;
        size_t p = colon + 1; Path path;
        while (p < L.size()) {
            if (L[p] != '(') { err = where + ": expected '(' at column " + std::to_string(p + 1); return false; }
            size_t close = L.find(')', p);
            if (close == std::string::npos) { err = where + ": unterminated coordinate (truncated?)"; return false; }
            std::string inner = L.substr(p + 1, close - p - 1);
            size_t comma = inner.find(',');
            int a, b;
            if (comma == std::string::npos || !toInt(inner.substr(0, comma), a) || !toInt(inner.substr(comma + 1), b)) { err = where + ": bad coordinate '(" + inner + ")'"; return false; }
            Cell c = pairIsXY ? Cell{a, b} : Cell{b, a};
            path.push_back(c);
            p = close + 1;
            if (p < L.size()) {
                if (L.compare(p, 2, "->") != 0) { err = where + ": expected '->' after coordinate"; return false; }
                p += 2;
            }
        }
        if (path.empty()) { err = where + ": agent " + std::to_string(id) + " has an empty path"; return false; }
        out.paths[id] = std::move(path);
    }
    if (next != numAgents) { err = "found " + std::to_string(next) + " agent lines, expected " + std::to_string(numAgents); return false; }
    out.anyContent = true;
    return true;
}

// ------------------------------------------------------------- plan checking
// Core check. All data already parsed; returns the verdict in `r`.
inline void checkPlan(const Grid& g, const std::vector<AgentSpec>& agents, const std::vector<Path>& paths, Report& r) {
    const int m = (int)agents.size();
    r.numAgents = m;
    if ((int)paths.size() != m) { r.add("AGENT_COUNT", "paths for " + std::to_string(paths.size()) + " agents, expected " + std::to_string(m)); r.status = Status::INVALID_PLAN; return; }

    // Instance sanity (distinct starts / goals, traversable cells).
    {
        std::set<std::pair<int,int>> ss, gs;
        for (int i = 0; i < m; ++i) {
            const auto& a = agents[i];
            if (!g.free(a.start.x, a.start.y)) r.add("START_BLOCKED_OR_OOB", "agent " + std::to_string(i));
            if (!g.free(a.goal.x, a.goal.y)) r.add("GOAL_BLOCKED_OR_OOB", "agent " + std::to_string(i));
            if (!ss.insert({a.start.x, a.start.y}).second) r.add("DUPLICATE_START", "agent " + std::to_string(i));
            if (!gs.insert({a.goal.x, a.goal.y}).second) r.add("DUPLICATE_GOAL", "agent " + std::to_string(i));
        }
    }

    r.arrival.assign(m, 0);
    r.sumOfCosts = 0; r.sumOfPathLengths = 0; r.makespan = 0;
    for (int i = 0; i < m; ++i) {
        const Path& p = paths[i]; const auto& a = agents[i];
        std::string who = "agent " + std::to_string(i);
        if (p.empty()) { r.add("EMPTY_PATH", who); continue; }
        if (p.front().x != a.start.x || p.front().y != a.start.y) r.add("WRONG_START", who);
        if (p.back().x != a.goal.x || p.back().y != a.goal.y) r.add("GOAL_NOT_REACHED", who + " ends at (" + std::to_string(p.back().x) + "," + std::to_string(p.back().y) + ")");
        for (size_t t = 0; t < p.size(); ++t) {
            if (!g.inBounds(p[t].x, p[t].y)) { r.add("OUT_OF_BOUNDS", who + " t=" + std::to_string(t)); continue; }
            if (!g.free(p[t].x, p[t].y)) r.add("OBSTACLE", who + " t=" + std::to_string(t));
            if (t > 0) {
                int d = std::abs(p[t].x - p[t-1].x) + std::abs(p[t].y - p[t-1].y);
                if (d > 1) r.add("ILLEGAL_MOVE", who + " t=" + std::to_string(t - 1) + "->" + std::to_string(t) + " jumps " + std::to_string(d) + " cells");
            }
        }
        // Arrival time: one step after the last time the agent is NOT on its goal (0 if never off it).
        const int64_t arr = authoritativeArrivalCost(p, [&](const Cell& c) {
            return c.x == a.goal.x && c.y == a.goal.y;
        });
        // If the path never reaches the goal at the end, GOAL_NOT_REACHED was already recorded.
        r.arrival[i] = arr;
        int length = 0;
        if (!checkedAdd(r.sumOfCosts, arr, r.sumOfCosts) ||
            !checkedAdd(r.sumOfPathLengths, static_cast<int64_t>(p.size()) - 1, r.sumOfPathLengths) ||
            !checkedCostToInt(static_cast<int64_t>(p.size()), length) || length == std::numeric_limits<int>::max()) {
            r.add("COST_OVERFLOW", who); r.status = Status::INVALID_PLAN; return;
        }
        r.makespan = std::max(r.makespan, length - 1);
    }

    // Conflicts: positions beyond a path's end = its last cell (stay-at-goal occupancy).
    auto pos = [&](int i, int t) -> const Cell& { const Path& p = paths[i]; return p[std::min<size_t>((size_t)t, p.size() - 1)]; };
    bool pathsUsable = true;
    for (int i = 0; i < m; ++i) if (paths[i].empty()) pathsUsable = false;
    if (pathsUsable) {
        for (int t = 0; t <= r.makespan; ++t) {
            std::unordered_map<long long, int> occ;
            occ.reserve(m * 2);
            for (int i = 0; i < m; ++i) {
                const Cell& c = pos(i, t);
                long long key = (long long)c.y * 1000003LL + c.x;
                auto ins = occ.emplace(key, i);
                if (!ins.second) {
                    int j = ins.first->second;
                    bool j_fin = t >= (int)paths[j].size(), i_fin = t >= (int)paths[i].size();
                    std::string note = (j_fin || i_fin) ? " (involves an agent that already finished and is parked there)" : "";
                    r.add("VERTEX_CONFLICT", "agents " + std::to_string(j) + "," + std::to_string(i) + " at t=" + std::to_string(t) + " cell (" + std::to_string(c.x) + "," + std::to_string(c.y) + ")" + note);
                }
            }
            if (t < r.makespan) {
                for (int i = 0; i < m; ++i) {
                    const Cell& a0 = pos(i, t); const Cell& a1 = pos(i, t + 1);
                    if (a0.x == a1.x && a0.y == a1.y) continue;       // waiting cannot swap
                    long long key = (long long)a1.y * 1000003LL + a1.x;
                    auto it = occ.find(key);                          // who is at a1 at time t?
                    if (it == occ.end() || it->second <= i) continue; // each pair reported once
                    int j = it->second; const Cell& b1 = pos(j, t + 1);
                    if (b1.x == a0.x && b1.y == a0.y)
                        r.add("EDGE_SWAP", "agents " + std::to_string(i) + "," + std::to_string(j) + " swap between t=" + std::to_string(t) + " and " + std::to_string(t + 1));
                }
            }
        }
    }
    r.status = r.issueCounts.empty() ? Status::VALID : Status::INVALID_PLAN;
}

// ------------------------------------------------------------ file-level API
struct ValidateOptions { std::string mapFile, scenFile, pathsFile; int numAgents = 0;
    // The solver's --outputPaths file (LNS::writePathsToFile) prints (row,col), so that is the default.
    // Scenario files and our hand-written fixtures use (x=col,y=row); pass pathsAreXY=true for those.
    bool pathsAreXY = false; };

inline Report validateFiles(const ValidateOptions& o) {
    Report r;
    std::string mapText, scenText, pathText, err;
    if (!readWholeFile(o.mapFile, mapText)) { r.status = Status::MISSING; r.add("MAP_MISSING", o.mapFile); return r; }
    if (!readWholeFile(o.scenFile, scenText)) { r.status = Status::MISSING; r.add("SCEN_MISSING", o.scenFile); return r; }
    if (!readWholeFile(o.pathsFile, pathText)) { r.status = Status::MISSING; r.add("PATHS_MISSING", o.pathsFile); return r; }
    Grid g; Scenario sc; ParsedPaths pp; bool unsolved = false;
    if (!parseMap(mapText, g, err)) { r.status = Status::MALFORMED; r.add("MAP_PARSE", err); return r; }
    if (o.numAgents <= 0) { r.status = Status::MALFORMED; r.add("BAD_ARGS", "agent count must be > 0"); return r; }
    if (!parseScenario(scenText, o.numAgents, sc, err)) { r.status = Status::MALFORMED; r.add("SCEN_PARSE", err); return r; }
    if (sc.mapW != g.width || sc.mapH != g.height) { r.status = Status::MALFORMED; r.add("SCEN_MAP_MISMATCH", "scenario says " + std::to_string(sc.mapW) + "x" + std::to_string(sc.mapH) + ", map is " + std::to_string(g.width) + "x" + std::to_string(g.height)); return r; }
    if (!parsePaths(pathText, o.numAgents, o.pathsAreXY, pp, err, unsolved)) {
        r.status = unsolved ? Status::UNSOLVED : Status::MALFORMED; r.add(unsolved ? "NO_PLAN" : "PATHS_PARSE", err); return r;
    }
    checkPlan(g, sc.agents, pp.paths, r);
    return r;
}

} // namespace rbl
