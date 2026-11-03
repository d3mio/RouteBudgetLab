#!/usr/bin/env python3
import copy
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from analyze_selective_recovery import analyze,exact_signed_rank,exact_binomial_sign_test,POLICIES
from run_selective_recovery_pilot import manifest,verify_manifest,reconcile

class ExperimentTests(unittest.TestCase):
    def fixture(self):
        return [dict(map='map',scen='scenario',num_agents=2,time_budget_s=1,seed=0,
                     experiment_policy=p,cost_at_deadline=10,final_cost=10,independent_validation='PASSED',
                     deadline_overshoot_s=0) for p in POLICIES]
    def test_authoritative_cost_only(self):
        from safe_validator import validate_validator_output
        raw=json.dumps(dict(status='VALID',independent_sum_of_costs=2,sum_of_path_lengths=4,cost_matches_path_length=True))
        self.assertFalse(validate_validator_output(0,raw,'',4)['is_valid'])
    def test_exact_four_losses(self):
        self.assertEqual(exact_signed_rank([287,2,72,73]+[0]*28),.125)
        self.assertEqual(exact_binomial_sign_test(0,4),.125)
        self.assertEqual(exact_signed_rank([1,-1]),1)
        self.assertEqual(exact_signed_rank([1,1]),.5)
    def test_holm_and_return_cost(self):
        runs=[]
        for seed,loss in enumerate([287,2,72,73]):
            for r in self.fixture():
                r['seed']=seed
                if r['experiment_policy']==POLICIES[1]:r['cost_at_deadline']+=loss
                runs.append(r)
        c=analyze(runs)['comparisons'][POLICIES[1]]
        self.assertEqual(c['sign_two_sided_holm'],.625)
        self.assertEqual(c['wilcoxon_two_sided_exact_holm'],.625)
        self.assertEqual(c['return_cost_comparison']['ties'],4)
    def test_reject_missing_duplicate_invalid(self):
        runs=self.fixture()
        for invalid in [[],runs[:-1],runs+[runs[0]],{}]:
            with self.assertRaises(ValueError):analyze(invalid)
        for key,value in [('cost_at_deadline',None),('final_cost',-1),('independent_validation','FAILED')]:
            bad=copy.deepcopy(runs);bad[0][key]=value
            with self.assertRaises(ValueError):analyze(bad)
    def test_manifest_after_serialization(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d); f=p/'raw.json';f.write_text('{"enriched":true}\n');manifest(p);verify_manifest(p)
            f.write_text('{}')
            with self.assertRaises(ValueError):verify_manifest(p)
    def test_telemetry_and_late_commit(self):
        r=dict(recovery_events=[],pa_attempts=0,pa_committed=0,eviction_attempts=0,eviction_committed=0,
               terminal_checkpoints_examined=0,terminal_committed=0,cost_at_deadline=1,final_cost=1,time_budget_s=1)
        reconcile(r);r['pa_attempts']=1
        with self.assertRaises(ValueError):reconcile(r)
        r['pa_attempts']=0;r['final_cost']=0
        with self.assertRaises(ValueError):reconcile(r)
    def test_real_cli_rejects_unknown_policy(self):
        binary=Path(sys.argv[0]).resolve() # use explicit environment-independent repository build
        binary=Path('build/corrected-release/rbl_profile_partial_acceptance')
        if not binary.exists():self.fail('Build corrected Release profiler before running CLI test')
        p=subprocess.run([str(binary),'--map','tests/fixtures/recovery_cross.map','--scen','tests/fixtures/recovery_cross.scen','--agents','2','--policy','TYPO'],capture_output=True,text=True)
        self.assertEqual(p.returncode,2)
        self.assertIn('Invalid policy',p.stderr)
if __name__=='__main__':unittest.main()
