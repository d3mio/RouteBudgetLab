#!/usr/bin/env python3
"""
Regression Test Suite for Safe MAPF Independent Validation
Verifies the fix for the unsafe substring check ('VALID' in out).
Demonstrates:
  1. A valid result passes.
  2. An invalid result fails.
  3. The text 'INVALID' or 'INVALID_PLAN' cannot pass.
  4. Missing or malformed output fails.
  5. A validator process error fails.
  6. Integration with real rbl_validate binary on real and mutated fixtures.
"""

import sys
import unittest
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from safe_validator import (
    validate_validator_output,
    validate_plan_file,
    DEFAULT_VALIDATOR,
    convert_linear_paths_to_movingai
)


class TestSafeValidationRegression(unittest.TestCase):

    def test_1_valid_result_passes(self):
        """1. A valid result passes."""
        stdout = '{"status":"VALID","agents":50,"independent_sum_of_costs":1147,"sum_of_path_lengths":1147,"makespan":45,"issues":[]}'
        res = validate_validator_output(returncode=0, stdout=stdout, stderr="", expected_cost=1147)
        self.assertTrue(res["is_valid"], "Valid output must pass")
        self.assertEqual(res["status"], "VALID")
        self.assertEqual(res["exit_code"], 0)
        self.assertEqual(res["independent_sum_of_costs"], 1147)

    def test_2_invalid_result_fails(self):
        """2. An invalid result fails."""
        stdout = '{"status":"INVALID_PLAN","agents":50,"independent_sum_of_costs":1200,"sum_of_path_lengths":1200,"makespan":50,"issues":[{"code":"VERTEX_CONFLICT","detail":"collision at t=5"}]}'
        res = validate_validator_output(returncode=1, stdout=stdout, stderr="", expected_cost=None)
        self.assertFalse(res["is_valid"], "Invalid plan must fail")
        self.assertEqual(res["status"], "INVALID_PLAN")
        self.assertEqual(res["exit_code"], 1)
        self.assertEqual(len(res["issues"]), 1)

    def test_3_invalid_text_cannot_pass(self):
        """
        3. The text 'INVALID' cannot pass.
        Explicitly demonstrates the flaw of the naive check:
          naive_status = ('PASS' if 'VALID' in out else 'FAIL') -> returns 'PASS'!
        Whereas safe validation returns is_valid == False.
        """
        tricky_outputs = [
            # Substring 'VALID' is present inside 'INVALID_PLAN'
            '{"status":"INVALID_PLAN","agents":10,"issues":[{"code":"VERTEX_COLLISION","detail":"INVALID"}]}',
            # Substring 'VALID' is present in error message
            '{"status":"MALFORMED","issues":[{"code":"PARSE_ERROR","detail":"NOT A VALID PLAN"}]}',
            # Raw string containing INVALID
            'PLAN IS INVALID',
            'STATUS: INVALID'
        ]

        for out in tricky_outputs:
            # Demonstrate the flaw of naive check
            naive_result = ("PASS" if "VALID" in out else "FAIL")
            self.assertEqual(naive_result, "PASS", "Demonstrating that naive 'VALID in out' falsely passes")

            # Safe validator on returncode=1 (standard for invalid plan)
            safe_res_1 = validate_validator_output(returncode=1, stdout=out, stderr="")
            self.assertFalse(safe_res_1["is_valid"], f"Safe validator must reject output with code 1: {out}")

            # Even if exitcode were somehow 0 (e.g. malformed or spoofed), status is not exact 'VALID'
            safe_res_0 = validate_validator_output(returncode=0, stdout=out, stderr="")
            self.assertFalse(safe_res_0["is_valid"], f"Safe validator must reject non-'VALID' status even with code 0: {out}")

    def test_4_missing_or_malformed_output_fails(self):
        """4. Missing or malformed output fails."""
        # Empty stdout
        res_empty = validate_validator_output(returncode=0, stdout="", stderr="")
        self.assertFalse(res_empty["is_valid"], "Empty stdout must fail")
        self.assertEqual(res_empty["status"], "EMPTY_OUTPUT")

        # Whitespace-only stdout
        res_ws = validate_validator_output(returncode=0, stdout="   \n\t  ", stderr="")
        self.assertFalse(res_ws["is_valid"], "Whitespace stdout must fail")
        self.assertEqual(res_ws["status"], "EMPTY_OUTPUT")

        # Corrupted JSON
        res_corrupt = validate_validator_output(returncode=0, stdout="{\"status\":\"VALID\", unexpected_token", stderr="")
        self.assertFalse(res_corrupt["is_valid"], "Corrupted JSON must fail")
        self.assertEqual(res_corrupt["status"], "JSON_PARSE_ERROR")

        # Missing files reported by validator (code 3)
        res_missing = validate_validator_output(returncode=3, stdout='{"status":"MISSING"}', stderr="")
        self.assertFalse(res_missing["is_valid"], "Missing file must fail")
        self.assertEqual(res_missing["status"], "MISSING_FILE")

    def test_5_validator_process_error_fails(self):
        """5. A validator process error fails."""
        # Usage error (code 64)
        res_usage = validate_validator_output(returncode=64, stdout="", stderr="usage error")
        self.assertFalse(res_usage["is_valid"], "Exit code 64 must fail")
        self.assertEqual(res_usage["status"], "USAGE_ERROR")

        # Crash / SIGSEGV (e.g. -11 or 139)
        res_crash = validate_validator_output(returncode=-11, stdout="", stderr="Segmentation fault")
        self.assertFalse(res_crash["is_valid"], "Process crash must fail")
        self.assertTrue("PROCESS_ERROR" in res_crash["status"])

    def test_6_real_binary_integration_valid_and_invalid(self):
        """6. Real binary integration test using rbl_validate executable."""
        self.assertTrue(DEFAULT_VALIDATOR.exists(), f"Validator binary must exist at {DEFAULT_VALIDATOR}")

        # Real valid fixture
        map_path = REPO_ROOT / "tests" / "fixtures" / "real" / "random-32-32-20.map"
        scen_path = REPO_ROOT / "tests" / "fixtures" / "real" / "random-32-32-20-random-1.scen"
        fixture_paths = REPO_ROOT / "tests" / "fixtures" / "real" / "baseline_50agents_seed0.paths"

        if fixture_paths.exists() and map_path.exists():
            # Validate real fixture (coords are row, col in baseline_50agents_seed0.paths)
            res = validate_plan_file(map_path, scen_path, 50, fixture_paths, reported_cost=1147, coords_xy=False)
            self.assertTrue(res["is_valid"], f"Real baseline fixture must pass validation: {res}")
            self.assertEqual(res["status"], "VALID")

            # Mutate fixture to inject a vertex conflict
            with open(fixture_paths, "r") as f:
                lines = f.readlines()
            
            # Make agent 1 copy agent 0's path exactly -> guaranteed collision!
            self.assertGreater(len(lines), 2)
            lines[1] = "Agent 1:" + lines[0].split(":", 1)[1]

            with tempfile.NamedTemporaryFile("w", suffix=".paths", delete=False) as tmp:
                tmp.writelines(lines)
                tmp_path = tmp.name

            try:
                res_mut = validate_plan_file(map_path, scen_path, 50, tmp_path, coords_xy=False)
                self.assertFalse(res_mut["is_valid"], "Colliding plan must be rejected by validator")
                self.assertEqual(res_mut["status"], "INVALID_PLAN")
                self.assertEqual(res_mut["exit_code"], 1)
            finally:
                Path(tmp_path).unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()
