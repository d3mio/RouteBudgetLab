import json,subprocess,re,statistics,hashlib,importlib.util,sys
sys.dont_write_bytecode = True
from pathlib import Path
out=Path(__file__).resolve().parent;root=out.parent
# Convert internal linear-index init fixture serialization to validator row,column input.
records=json.loads((out/'initial_fixture_validation.json').read_text()); dest=out/'converted_initial_fixtures';dest.mkdir(exist_ok=True)
for r in records:
 cmd=r['command']; mp=Path(cmd[cmd.index('--map')+1]);width=int(re.search(r'^width (\d+)',mp.read_text(),re.M)[1]); source=root/r['path']; lines=[]
 for line in source.read_text().splitlines():
  a,rest=line.split(':',1);locs=[int(v) for v in re.findall(r'\((\d+)\)',rest)]
  lines.append('Agent '+a+':'+ '->'.join(f'({v//width},{v%width})' for v in locs))
 converted=dest/source.name;converted.write_text('\n'.join(lines)+'\n');cmd[cmd.index('--paths')+1]=str(converted)
 p=subprocess.run(cmd,capture_output=True,text=True);r.update(command=cmd,exit_code=p.returncode,stdout=p.stdout,stderr=p.stderr)
(out/'initial_fixture_converted_validation.json').write_text(json.dumps(records,indent=2));print('converted fixtures',[(r['path'],r['exit_code']) for r in records]);assert all(r['exit_code']==0 for r in records)
# Re-run report's exact current analysis function, divert outputs to audit directory.
spec=importlib.util.spec_from_file_location('analysis',root/'scripts/analyze_selective_recovery.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
mod.analyze_pilot(root/'results/2026-11-01_selective_recovery/all_runs.json',out/'repository_analysis_reexecuted.json',out/'repository_analysis_reexecuted.md')
# Comparable metrics, without assuming comparable experimental workload/hardware.
h=json.loads((root/'results/partial_acceptance_pilot/all_runs.json').read_text()); result={}
for policy in sorted({r['policy'] for r in h}):
 rr=[r for r in h if r['policy']==policy];n=sum(r['pa_attempts'] for r in rr);t=sum(r['pa_total_time_us'] for r in rr)
 result[policy]=dict(runs=len(rr),attempts=n,total_pa_us=t,mean_per_attempt_us=t/n if n else 0,median_of_per_run_medians_us=statistics.median(r['pa_telemetry']['total_pa_overhead_us']['median'] for r in rr),total_budget_s=sum(r['time_budget_s'] for r in rr))
e=json.loads((out/'RECOVERY_EVENTS.json').read_text())['PA_EVERY_INTERRUPTION_FILTERED'];n=e['filter_evaluations']
result['selective_filtered']=dict(filter_average_ns=e['filter_total_time_us']*1000/n,extraction_average_us=e['extraction_total_time_us']/n,prep_average_us=(e['filter_total_time_us']+e['extraction_total_time_us'])/n,pa_total_average_us=e['pa_total_time_us']/n,total_pa_s=e['pa_total_time_us']/1e6,per_run_pa_s=e['pa_total_time_us']/1e6/32,extraction_total_s=e['extraction_total_time_us']/1e6,total_planning_budget_s=96)
(out/'TIMING_RECOMPUTED.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
# Commit/tree identities and untouched source equivalence, preserve real diff artifacts.
commands={
'commit-identities':['git','log','-3','--format=fuller'],
'baseline-changes':['git','diff','917bcb7','b4d9f9e','--','src','tests','CMakeLists.txt'],
'completion-source-diff':['git','diff','07973d0','fb6ba90','--','src','CMakeLists.txt','third_party'],
'completion-raw-diff':['git','diff','07973d0','fb6ba90','--','results/2026-11-01_selective_recovery/raw','results/2026-11-01_selective_recovery/all_runs.json','results/2026-11-01_selective_recovery/paths'],
'vendor-trees':['git','ls-tree','HEAD','third_party/anytime-mapf','third_party/eigen'],
'submodules':['git','submodule','status'],
'old-log-history':['git','log','-1','--format=fuller','--','logs/audit_asan_test.log'],
}
for name,cmd in commands.items():
 p=subprocess.run(cmd,cwd=root,capture_output=True,text=True);(out/(name+'.log')).write_text(p.stdout+p.stderr)
 with (out/'commands.jsonl').open('a') as f:f.write(json.dumps(dict(name=name,command=cmd,exit_code=p.returncode))+'\n')
