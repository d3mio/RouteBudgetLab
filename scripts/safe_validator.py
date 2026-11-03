#!/usr/bin/env python3
"""
RouteBudget Lab - Safe Independent Solution Validator
Provides fail-safe independent validation of MAPF paths using rbl_validate.

Strictly avoids substring matching ('VALID' in out) which erroneously passes 'INVALID_PLAN'.
Enforces:
  1. Validator process exit code == 0.
  2. Strict JSON structured parsing (no regex/substring on raw output).
  3. Explicit Boolean validity: status == "VALID".
  4. Reported cost matching against independent sum of costs when provided.
  5. Comprehensive error capture for malformed, missing, or process-error cases.
"""

import os
import sys
import json
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_VALIDATOR = Path(os.environ.get("RBL_VALIDATOR", str(REPO_ROOT / "build" / "bench-release" / "rbl_validate")))
if not DEFAULT_VALIDATOR.exists():
    DEFAULT_VALIDATOR = REPO_ROOT / "build" / "rbl-release" / "rbl_validate"


def parse_map_width(map_path):
    """Parse width (num_of_cols) from a Moving AI .map file."""
    with open(map_path, "r") as f:
        for line in f:
            line = line.strip()
            if line.startswith("width"):
                parts = line.split()
                return int(parts[1])
    raise ValueError(f"Could not find width in map {map_path}")


def convert_linear_paths_to_movingai(linear_paths_file, map_path, output_movingai_paths):
    """
    Convert RouteBudgetLab linear-index paths (i:(loc)->...) to
    standard Moving AI coordinate paths (Agent i:(col,row)->...).
    In Moving AI: x = col = loc % width, y = row = loc // width.
    """
    width = parse_map_width(map_path)
    with open(linear_paths_file, "r") as fin, open(output_movingai_paths, "w") as fout:
        for line in fin:
            line = line.strip()
            if not line:
                continue
            colon = line.find(":")
            if colon == -1:
                continue
            agent_id = line[:colon]
            rest = line[colon + 1:]
            
            fout.write(f"Agent {agent_id}:")
            # Parse tokens like (loc)->
            tokens = [t for t in rest.split("->") if t.strip()]
            for tok in tokens:
                tok = tok.strip()
                if tok.startswith("(") and tok.endswith(")"):
                    loc = int(tok[1:-1])
                    row = loc // width
                    col = loc % width
                    fout.write(f"({col},{row})->")
            fout.write("\n")


def validate_validator_output(returncode, stdout, stderr, expected_cost=None):
    """
    Pure validation decision function operating on subprocess return code and outputs.
    Guarantees that:
      - Nonzero returncode is ALWAYS invalid (even if stdout contains 'VALID').
      - String containing 'INVALID' or 'INVALID_PLAN' cannot pass.
      - Malformed JSON or empty stdout is classified as MALFORMED.
      - Only exit_code == 0 AND exact status == 'VALID' passes.
    """
    result = {
        "is_valid": False,
        "status": "UNKNOWN",
        "exit_code": returncode,
        "independent_sum_of_costs": None,
        "makespan": None,
        "issues": [],
        "cost_matches": None,
        "error_detail": ""
    }

    # Rule 1: Nonzero exit code is an immediate hard failure
    if returncode != 0:
        if returncode == 1:
            result["status"] = "INVALID_PLAN"
            result["error_detail"] = "Validator reported invalid plan (exit code 1)"
        elif returncode == 2:
            result["status"] = "MALFORMED"
            result["error_detail"] = "Validator reported malformed plan/instance (exit code 2)"
        elif returncode == 3:
            result["status"] = "MISSING_FILE"
            result["error_detail"] = "Validator reported missing file (exit code 3)"
        elif returncode == 4:
            result["status"] = "UNSOLVED"
            result["error_detail"] = "Validator reported unsolved / empty plan (exit code 4)"
        elif returncode == 64:
            result["status"] = "USAGE_ERROR"
            result["error_detail"] = "Validator command line usage error (exit code 64)"
        else:
            result["status"] = f"PROCESS_ERROR_{returncode}"
            result["error_detail"] = f"Validator process exited with unexpected code {returncode}: {stderr.strip()}"
        
        # Even if stdout had text, parse issues if possible for diagnostic context, but is_valid MUST remain False
        try:
            d = json.loads(stdout.strip())
            result["issues"] = d.get("issues", [])
        except Exception:
            pass
        return result

    # Rule 2: Structured output parsing
    if not stdout or not stdout.strip():
        result["status"] = "EMPTY_OUTPUT"
        result["error_detail"] = "Validator exited with 0 but produced empty stdout"
        return result

    try:
        data = json.loads(stdout.strip())
    except Exception as e:
        result["status"] = "JSON_PARSE_ERROR"
        result["error_detail"] = f"Failed to parse validator JSON: {e}"
        return result

    # Rule 3: Explicit exact status check - NEVER substring
    status_str = data.get("status")
    result["status"] = status_str
    result["independent_sum_of_costs"] = data.get("independent_sum_of_costs")
    result["makespan"] = data.get("makespan")
    result["issues"] = data.get("issues", [])

    if status_str != "VALID":
        result["is_valid"] = False
        result["error_detail"] = f"Validator explicit status is '{status_str}' (not 'VALID')"
        return result

    # Rule 4: Cost verification if reported cost is provided
    if expected_cost is not None:
        indep_cost = data.get("independent_sum_of_costs")
        if type(indep_cost) is int and indep_cost >= 0 and indep_cost == expected_cost:
            result["cost_matches"] = True
        else:
            result["cost_matches"] = False
            result["is_valid"] = False
            result["status"] = "COST_MISMATCH"
            result["error_detail"] = f"Reported cost {expected_cost} does not match validator independent cost {indep_cost}"
            return result

    # Both returncode == 0 and status == 'VALID' confirmed
    result["is_valid"] = True
    return result


def validate_plan_file(map_path, scen_path, num_agents, paths_file, reported_cost=None, coords_xy=True, validator_bin=None):
    """
    Execute rbl_validate safely on given plan file and return structured result.
    """
    val_bin = Path(validator_bin) if validator_bin else DEFAULT_VALIDATOR
    if not val_bin.exists():
        return {
            "is_valid": False,
            "status": "VALIDATOR_BINARY_MISSING",
            "exit_code": -1,
            "error_detail": f"Validator binary not found at {val_bin}"
        }

    if not Path(paths_file).exists() or Path(paths_file).stat().st_size == 0:
        return {
            "is_valid": False,
            "status": "PATHS_FILE_MISSING_OR_EMPTY",
            "exit_code": -1,
            "error_detail": f"Paths file {paths_file} does not exist or is empty"
        }

    cmd = [
        str(val_bin),
        "--map", str(map_path),
        "--scen", str(scen_path),
        "--agents", str(num_agents),
        "--paths", str(paths_file)
    ]
    if coords_xy:
        cmd.extend(["--coords", "xy"])
    else:
        cmd.extend(["--coords", "rc"])

    if reported_cost is not None and reported_cost >= 0:
        cmd.extend(["--reported-cost", str(reported_cost)])

    try:
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=60)
        return validate_validator_output(proc.returncode, proc.stdout, proc.stderr, expected_cost=reported_cost)
    except subprocess.TimeoutExpired:
        return {
            "is_valid": False,
            "status": "VALIDATOR_TIMEOUT",
            "exit_code": -1,
            "error_detail": "Validator subprocess timed out after 60s"
        }
    except Exception as e:
        return {
            "is_valid": False,
            "status": "SUBPROCESS_EXCEPTION",
            "exit_code": -1,
            "error_detail": str(e)
        }


if __name__ == "__main__":
    print("Safe validator module loaded successfully.")
