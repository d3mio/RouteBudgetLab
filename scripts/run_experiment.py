#!/usr/bin/env python3
"""
RouteBudget Lab - Unified Experiment Runner and Provenance Logger (O1, FR-01, FR-06, NFR-01, NFR-04).

Executes a MAPF solver run, records full environment & input provenance,
captures monotonic wall-clock execution time, and immediately verifies
the produced paths using the independent path validator (rbl_validate).

Outputs:
  - <output_dir>/<run_id>.json (machine-readable run record)
  - <output_dir>/<run_id>.log  (solver stdout/stderr)
  - Appends to <output_dir>/runs.jsonl
"""

import argparse
import hashlib
import json
import os
import platform
import subprocess
import sys
import time

def sha256_file(path):
    if not os.path.isfile(path):
        return None
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def get_git_commit(repo_root):
    # Try git command via CLT if available
    try:
        git_bin = "/Library/Developer/CommandLineTools/usr/bin/git" if os.path.exists("/Library/Developer/CommandLineTools/usr/bin/git") else "git"
        out = subprocess.check_output([git_bin, "rev-parse", "HEAD"], cwd=repo_root, stderr=subprocess.DEVNULL)
        return out.decode().strip()
    except Exception:
        return "uncommitted_or_git_unavailable"

def get_machine_info():
    info = {
        "platform": platform.platform(),
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
        "processor": platform.processor(),
        "python_version": platform.python_version()
    }
    # macOS specific
    if platform.system() == "Darwin":
        try:
            sw_vers = subprocess.check_output(["sw_vers", "-productVersion"]).decode().strip()
            info["macos_version"] = sw_vers
            cpu_brand = subprocess.check_output(["sysctl", "-n", "machdep.cpu.brand_string"]).decode().strip()
            info["cpu_brand"] = cpu_brand
            mem = subprocess.check_output(["sysctl", "-n", "hw.memsize"]).decode().strip()
            info["ram_bytes"] = int(mem)
        except Exception:
            pass
    return info

def main():
    parser = argparse.ArgumentParser(description="Execute a MAPF run with full provenance and independent validation.")
    parser.add_argument("--solver", required=True, help="Path to solver binary (e.g. build/baseline-release-cxx14/balance)")
    parser.add_argument("--map", required=True, help="Path to .map file")
    parser.add_argument("--scen", required=True, help="Path to .scen file")
    parser.add_argument("--agents", type=int, required=True, help="Number of agents")
    parser.add_argument("--cutoff", type=float, required=True, help="Global wall-clock time limit in seconds")
    parser.add_argument("--seed", type=int, default=0, help="Random seed")
    parser.add_argument("--outdir", default="results/runs", help="Output directory")
    parser.add_argument("--tag", default="baseline", help="Experiment or configuration tag")
    parser.add_argument("--validator", default="build/rbl-release/rbl_validate", help="Path to validator binary")
    parser.add_argument("--extra-args", nargs=argparse.REMAINDER, default=[], help="Additional arguments passed directly to the solver")
    args = parser.parse_args()

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    solver_bin = os.path.abspath(args.solver)
    map_path = os.path.abspath(args.map)
    scen_path = os.path.abspath(args.scen)
    validator_bin = os.path.abspath(args.validator)
    outdir = os.path.abspath(args.outdir)
    os.makedirs(outdir, exist_ok=True)

    if not os.path.isfile(solver_bin):
        sys.exit(f"Error: solver binary not found: {solver_bin}")
    if not os.path.isfile(map_path):
        sys.exit(f"Error: map file not found: {map_path}")
    if not os.path.isfile(scen_path):
        sys.exit(f"Error: scen file not found: {scen_path}")
    if not os.path.isfile(validator_bin):
        sys.exit(f"Error: validator binary not found: {validator_bin}")

    map_hash = sha256_file(map_path)
    scen_hash = sha256_file(scen_path)
    solver_hash = sha256_file(solver_bin)
    git_commit = get_git_commit(repo_root)

    map_stem = os.path.splitext(os.path.basename(map_path))[0]
    scen_stem = os.path.splitext(os.path.basename(scen_path))[0]
    timestamp = time.strftime("%Y%m%d_%H%M%S")
    run_id = f"{args.tag}_{map_stem}_{args.agents}a_{int(args.cutoff)}s_s{args.seed}_{timestamp}"

    paths_file = os.path.join(outdir, f"{run_id}_paths.txt")
    log_file = os.path.join(outdir, f"{run_id}.log")
    res_prefix = os.path.join(outdir, f"{run_id}_out")
    stats_file = os.path.join(outdir, f"{run_id}_stats.csv")

    cmd = [
        solver_bin,
        "-m", map_path,
        "-a", scen_path,
        "-k", str(args.agents),
        "-t", str(args.cutoff),
        "--seed", str(args.seed),
        "-o", res_prefix,
        "--outputPaths", paths_file,
        "--stats", stats_file,
        "--screen", "1",
        "--maxIterations", "1000000"
    ]
    if args.extra_args:
        cmd.extend(args.extra_args)

    # Monotonic timing around execution
    t0 = time.perf_counter()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    stdout, _ = p.communicate()
    t1 = time.perf_counter()
    elapsed_wall_clock = t1 - t0
    exit_code = p.returncode

    with open(log_file, "w") as f:
        f.write(stdout)

    # Parse reported metrics from solver output
    reported_solution_cost = None
    reported_init_cost = None
    reported_runtime = None
    reported_iterations = None
    import re
    # Match the summary line:
    # LNS(PP;PP): runtime = 2.00003, iterations = 28109, solution cost = 1155, initial solution cost = 1244, failed iterations = 28095
    for line in stdout.splitlines():
        if "LNS(PP;PP):" in line or ("solution cost =" in line and "initial solution cost =" in line):
            m_sol = re.search(r"(?<!initial\s)solution cost\s*=\s*(\d+)", line)
            if m_sol: reported_solution_cost = int(m_sol.group(1))
            m_init = re.search(r"initial solution cost\s*=\s*(\d+)", line)
            if m_init: reported_init_cost = int(m_init.group(1))
            m_rt = re.search(r"runtime\s*=\s*([\d\.]+)", line)
            if m_rt: reported_runtime = float(m_rt.group(1))
            m_it = re.search(r"iterations\s*=\s*(\d+)", line)
            if m_it: reported_iterations = int(m_it.group(1))

    # Run independent validation if paths file was produced
    val_record = None
    if os.path.isfile(paths_file) and os.path.getsize(paths_file) > 0:
        val_cmd = [
            validator_bin,
            "--map", map_path,
            "--scen", scen_path,
            "--agents", str(args.agents),
            "--paths", paths_file
        ]
        if reported_solution_cost is not None:
            val_cmd.extend(["--reported-cost", str(reported_solution_cost)])
        vp = subprocess.run(val_cmd, capture_output=True, text=True)
        try:
            val_record = json.loads(vp.stdout.strip())
            val_record["validator_exit_code"] = vp.returncode
        except Exception:
            val_record = {"status": "VALIDATOR_ERROR", "stdout": vp.stdout, "stderr": vp.stderr, "exit_code": vp.returncode}
    else:
        val_record = {"status": "UNSOLVED_OR_EMPTY", "validator_exit_code": 4}

    record = {
        "run_id": run_id,
        "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "git_commit": git_commit,
        "configuration": {
            "tag": args.tag,
            "solver_bin": solver_bin,
            "solver_sha256": solver_hash,
            "map_file": os.path.basename(map_path),
            "map_path": map_path,
            "map_sha256": map_hash,
            "scen_file": os.path.basename(scen_path),
            "scen_path": scen_path,
            "scen_sha256": scen_hash,
            "agents": args.agents,
            "cutoff_requested_sec": args.cutoff,
            "seed": args.seed,
            "extra_args": args.extra_args
        },
        "environment": get_machine_info(),
        "execution": {
            "exit_code": exit_code,
            "elapsed_wall_clock_sec": elapsed_wall_clock,
            "reported_runtime_sec": reported_runtime,
            "reported_iterations": reported_iterations,
            "reported_initial_cost": reported_init_cost,
            "reported_solution_cost": reported_solution_cost,
            "paths_file": paths_file,
            "log_file": log_file
        },
        "validation": val_record
    }

    # Save run record
    run_json = os.path.join(outdir, f"{run_id}.json")
    with open(run_json, "w") as f:
        json.dump(record, f, indent=2)

    master_jsonl = os.path.join(outdir, "runs.jsonl")
    with open(master_jsonl, "a") as f:
        f.write(json.dumps(record) + "\n")

    val_status = val_record.get("status", "UNKNOWN")
    indep_cost = val_record.get("independent_sum_of_costs", "N/A")
    print(f"[{run_id}] Exit: {exit_code} | Wall: {elapsed_wall_clock:.2f}s | Reported Cost: {reported_solution_cost} | Validated: {val_status} (Cost: {indep_cost})")
    return 0 if (exit_code == 0 and val_status == "VALID") else 1

if __name__ == "__main__":
    sys.exit(main())
