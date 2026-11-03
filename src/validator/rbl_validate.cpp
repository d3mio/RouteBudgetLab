// rbl_validate: command line front-end for the independent validator.
//
//   rbl_validate --map M.map --scen S.scen --agents K --paths P.txt
//                [--reported-cost N] [--json out.json] [--coords xy|rc]
//
// Exit code: 0 VALID, 1 INVALID_PLAN, 2 MALFORMED, 3 MISSING, 4 UNSOLVED, 64 usage error.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include "rbl_validator.hpp"

static std::string jesc(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if (c == '\n') o += "\\n"; else if ((unsigned char)c < 0x20) o += ' '; else o += c; }
    return o;
}

int main(int argc, char** argv) {
    rbl::ValidateOptions o; long long reported = -1; std::string jsonOut;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* name) -> const char* { if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", name); std::exit(64); } return argv[++i]; };
        if (a == "--map") o.mapFile = need("--map");
        else if (a == "--scen") o.scenFile = need("--scen");
        else if (a == "--paths") o.pathsFile = need("--paths");
        else if (a == "--agents") o.numAgents = std::atoi(need("--agents"));
        else if (a == "--reported-cost") {
            const std::string value = need("--reported-cost");
            try { size_t used = 0; reported = std::stoll(value, &used);
                if (used != value.size() || reported < 0) return 64;
            } catch (const std::exception&) { return 64; }
        }
        else if (a == "--json") jsonOut = need("--json");
        else if (a == "--coords") o.pathsAreXY = (std::string(need("--coords")) == "xy");
        else if (a == "--help" || a == "-h") {
            std::puts("rbl_validate --map M --scen S --agents K --paths P [--reported-cost N] [--json F] [--coords xy|rc]\n"
                      "Independent MAPF plan validator. SIMULATION ONLY: discrete grid plans, not certified for physical robots.");
            return 0;
        } else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 64; }
    }
    if (o.mapFile.empty() || o.scenFile.empty() || o.pathsFile.empty() || o.numAgents <= 0) { std::fprintf(stderr, "usage: see --help\n"); return 64; }

    rbl::Report r = rbl::validateFiles(o);

    if (r.status == rbl::Status::VALID && reported >= 0 && reported != r.sumOfCosts) {
        r.add("COST_MISMATCH", "Reported cost differs from authoritative arrival SoC");
        r.status = rbl::Status::INVALID_PLAN;
    }
    std::ostringstream js;
    js << "{\"status\":\"" << rbl::statusName(r.status) << "\",\"agents\":" << o.numAgents
       << ",\"independent_sum_of_costs\":" << r.sumOfCosts
       << ",\"sum_of_path_lengths\":" << r.sumOfPathLengths
       << ",\"makespan\":" << r.makespan;
    if (reported >= 0) {
        js << ",\"reported_cost\":" << reported
           << ",\"cost_matches_independent_arrival\":" << ((r.sumOfCosts == reported) ? "true" : "false")
           << ",\"cost_matches_path_length\":" << ((r.sumOfPathLengths == reported) ? "true" : "false");
    }
    js << ",\"issue_counts\":{";
    bool first = true;
    for (auto& kv : r.issueCounts) { js << (first ? "" : ",") << "\"" << kv.first << "\":" << kv.second; first = false; }
    js << "},\"issues\":[";
    for (size_t i = 0; i < r.issues.size(); ++i) js << (i ? "," : "") << "{\"code\":\"" << r.issues[i].code << "\",\"detail\":\"" << jesc(r.issues[i].detail) << "\"}";
    js << "]}";
    std::cout << js.str() << std::endl;
    if (!jsonOut.empty()) { std::ofstream f(jsonOut); f << js.str() << "\n"; }

    switch (r.status) {
        case rbl::Status::VALID: return 0;
        case rbl::Status::INVALID_PLAN: return 1;
        case rbl::Status::MALFORMED: return 2;
        case rbl::Status::MISSING: return 3;
        case rbl::Status::UNSOLVED: return 4;
    }
    return 2;
}
