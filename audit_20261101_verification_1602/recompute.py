"""Read-only analysis of raw artifacts, with separate new audit outputs."""
import json,csv,math,statistics,hashlib,itertools,collections,subprocess
from pathlib import Path
from scipy import stats
import scipy
ROOT=Path(__file__).resolve().parent.parent; OUT=Path(__file__).resolve().parent
D=ROOT/'results/2026-11-01_selective_recovery'
files=sorted((D/'raw').glob('*.json')); runs=[json.loads(f.read_text()) for f in files]
key=lambda r:(Path(r['map']).name,Path(r['scen']).name,r['num_agents'],r['time_budget_s'],r['seed'])
cells=collections.defaultdict(dict); problems=[]
for r in runs:
 k=key(r); p=r['experiment_policy']
 if p in cells[k]: problems.append('duplicate '+r['run_id'])
 cells[k][p]=r
policies=[p['name'] for p in json.loads((ROOT/'configs/2026-11-01_selective_recovery/selective_recovery_pilot.json').read_text())['policies']]
for k,c in cells.items():
 if set(c)!=set(policies): problems.append('missing policies '+str(k))
 if len({r['initial_cost'] for r in c.values()})!=1: problems.append('initial cost mismatch '+str(k))
for r in runs:
 if r['slice_budget_s']!=.005 or r['neighborhood_size']!=8 or r['terminal_reserve_s']!=.002: problems.append('settings mismatch '+r['run_id'])
 expected=min(pt['cost'] for pt in r['anytime_cost_trajectory'] if pt['timestamp_s']<=r['time_budget_s'])
 if expected!=r['cost_at_deadline']: problems.append('trajectory mismatch '+r['run_id'])
 if hashlib.sha256((D/'paths'/(r['run_id']+'.paths')).read_bytes()).hexdigest()!=r['paths_sha256']: problems.append('path hash mismatch '+r['run_id'])
 if r['cell_index']%6!=r['rotation_shift']: problems.append('rotation mismatch '+r['run_id'])
allruns=json.loads((D/'all_runs.json').read_text())
assert {r['run_id']:r for r in runs}=={r['run_id']:r for r in allruns}
# Recorded execution order in aggregate: confirm cyclic schedule.
for idx in range(32):
 order=[r['experiment_policy'] for r in allruns if r['cell_index']==idx]
 assert order==policies[idx%6:]+policies[:idx%6]
manifest=[]
for line in (D/'manifest.sha256').read_text().splitlines():
 h,p=line.split(maxsplit=1); f=D/p
 manifest.append(dict(path=p,match=hashlib.sha256(f.read_bytes()).hexdigest()==h))
rows=[]; pairs=[]; events={}; budgets=[]
for p in policies:
 rr=[r for r in runs if r['experiment_policy']==p]
 ds=[]; its=[]
 for k,c in sorted(cells.items()):
  r=c[p]; base=c['RESUMPTION_NO_PA']; d=r['cost_at_deadline']-base['cost_at_deadline'];ds.append(d);its.append(r['total_iterations']-base['total_iterations'])
  pairs.append(dict(map=k[0],scenario=k[1],agents=k[2],budget=k[3],seed=k[4],policy=p,baseline_deadline_cost=base['cost_at_deadline'],policy_deadline_cost=r['cost_at_deadline'],delta_cost=d,iteration_difference=its[-1],raw_path=str((D/'raw'/(r['run_id']+'.json')).relative_to(ROOT))))
 nz=[d for d in ds if d];w=sum(d<0 for d in ds);l=sum(d>0 for d in ds);n=len(nz)
 sign=min(1,2*sum(math.comb(n,i) for i in range(min(w,l)+1))/2**n) if n else 1
 # Exact signed-rank sign permutation on nonzero differences, including averaged tied ranks.
 ranks=list(stats.rankdata([abs(x) for x in nz])); observed=sum(rank for rank,d in zip(ranks,nz) if d>0)
 if n:
  perms=[sum(rank for rank,flag in zip(ranks,flags) if flag) for flags in itertools.product([0,1],repeat=n)]
  less=sum(x<=observed for x in perms)/len(perms); greater=sum(x>=observed for x in perms)/len(perms); exact=min(1,2*min(less,greater))
  scipy_p=float(stats.wilcoxon(nz,alternative='two-sided',zero_method='wilcox',method='exact').pvalue)
 else: exact=less=greater=scipy_p=1
 rows.append(dict(policy=p,pairs=len(ds),wins=w,ties=len(ds)-n,losses=l,mean_delta_all=statistics.mean(ds),mean_delta_non_ties=statistics.mean(nz) if nz else '',sample_sd=statistics.stdev(ds),sd_denominator=len(ds)-1,sem=statistics.stdev(ds)/math.sqrt(len(ds)),exact_sign_two_sided=sign,wilcoxon_exact_two_sided=exact,wilcoxon_less_improvement=less,wilcoxon_greater_worse=greater,scipy_exact_nonzero=scipy_p,mean_iteration_difference=statistics.mean(its),total_iteration_difference=sum(its)))
 fields=['filter_evaluations','filter_skips','filter_total_time_us','extraction_total_time_us','pa_attempts','pa_positive_opportunities','pa_committed','pa_budget_skipped','pa_total_gain','pa_total_time_us','eviction_attempts','eviction_filter_skips','eviction_committed','eviction_total_gain','terminal_recovery_calls','terminal_checkpoints_examined','terminal_filter_skips','terminal_committed','terminal_total_gain','terminal_total_time_us','stagnant_checkpoints_evicted','total_iterations','total_expansions']
 events[p]={k:sum(r[k] for r in rr) for k in fields}
 for field in ['graph_construction_time_us','selection_time_us','validation_time_us','commit_time_us','total_pa_overhead_us']:
  events[p][field]=sum(r['pa_telemetry'][field]['total'] for r in rr)
 for b in [1.,5.]:
  br=[r for r in rr if r['time_budget_s']==b]; overs=[r['deadline_overshoot_s']*1e6 for r in br]
  budgets.append(dict(policy=p,budget=b,n=len(br),overrun_count=sum(x>0 for x in overs),max_overrun_us=max(overs),mean_overrun_us=statistics.mean(overs),late_final_difference_count=sum(r['final_cost']!=r['cost_at_deadline'] for r in br)))
# Holm correction across the five non-baseline comparisons (not used in repository).
comp=sorted([r for r in rows if r['policy']!='RESUMPTION_NO_PA'],key=lambda r:r['wilcoxon_exact_two_sided']);prev=0
for i,r in enumerate(comp):
 prev=max(prev,min(1,(len(comp)-i)*r['wilcoxon_exact_two_sided']));r['wilcoxon_holm_five']=prev
rows[0]['wilcoxon_holm_five']=''
def csvout(name,rs):
 with (OUT/name).open('w') as f:
  wr=csv.DictWriter(f,fieldnames=list(rs[0]));wr.writeheader();wr.writerows(rs)
csvout('RECOMPUTED_RESULTS.csv',rows);csvout('PAIRED_RAW_VALUES.csv',pairs);csvout('DEADLINE_OVERRUNS.csv',budgets);csvout('original_manifest_check.csv',manifest)
(OUT/'RECOVERY_EVENTS.json').write_text(json.dumps(events,indent=2))
# Historical snapshots: raw weights and recorded selections (paths themselves not present).
offs=[json.loads(f.read_text()) for f in sorted((ROOT/'results/partial_acceptance_offline/raw').glob('*.json'))]; snaps=[s for o in offs for s in o['snapshots']]
off=dict(files=len(offs),snapshots=len(snaps),candidates=sum(len(s['weights']) for s in snaps),positive=sum(w>0 for s in snaps for w in s['weights']),zero=sum(w==0 for s in snaps for w in s['weights']),negative=sum(w<0 for s in snaps for w in s['weights']),any_negative_snapshots=sum(any(w<0 for w in s['weights']) for s in snaps),selected_negative_snapshots=sum(s['exact_selected_negative_count']>0 for s in snaps),gain_over_nonnegative=sum(s['exact_gain']-s['nonnegative_ref_gain'] for s in snaps),positive_closures=sum(s['exact_gain']>0 for s in snaps),exact_greedy_mismatch=sum(s['exact_gain']!=s['greedy_gain'] for s in snaps),exact_oracle_mismatch=sum(s['exact_gain']!=s['oracle_gain'] for s in snaps),by_map={m:sum(len(o['snapshots']) for o in offs if Path(o['map']).name==m) for m in {Path(o['map']).name for o in offs}})
(OUT/'OFFLINE_RECOMPUTED.json').write_text(json.dumps(off,indent=2))
summary=dict(raw_files=len(files),cells=len(cells),policies=len(policies),problems=problems,aggregate_matches_raw=True,original_manifest_matches=collections.Counter(('raw' if x['path'].startswith('raw/') else 'paths',x['match']) for x in manifest).__repr__(),scipy_version=scipy.__version__,initial_fixture_count=len({(Path(r['map']).stem,Path(r['scen']).stem,r['num_agents']) for r in runs}),rotation_frequencies=dict(collections.Counter(r['rotation_shift'] for r in runs)),raw_revision_fields=[k for k in runs[0] if any(x in k for x in ['git','revision','build','compiler'])])
(OUT/'RAW_INTEGRITY.json').write_text(json.dumps(summary,indent=2)); print(json.dumps(summary,indent=2)); print(json.dumps(rows,indent=2)); print(json.dumps(events,indent=2));print(json.dumps(off,indent=2))
# Re-execute independent validator on every saved final path and shared initialization fixture.
validation=[]
for r in runs:
 cmd=[str(OUT/'build-release/rbl_validate'),'--map',r['map'],'--scen',r['scen'],'--agents',str(r['num_agents']),'--paths',str(D/'paths'/(r['run_id']+'.paths')),'--reported-cost',str(r['final_cost'])]
 proc=subprocess.run(cmd,capture_output=True,text=True);validation.append(dict(run_id=r['run_id'],command=cmd,exit_code=proc.returncode,stdout=proc.stdout,stderr=proc.stderr))
seen=set(); fixtures=[]
for r in runs:
 k=(Path(r['map']).stem,Path(r['scen']).stem,r['num_agents'])
 if k in seen: continue
 seen.add(k);path=ROOT/'results/partial_acceptance_pilot/init_paths'/f'{k[0]}__{k[1]}__a{k[2]}.paths'
 cmd=[str(OUT/'build-release/rbl_validate'),'--map',r['map'],'--scen',r['scen'],'--agents',str(r['num_agents']),'--paths',str(path),'--reported-cost',str(r['initial_cost'])]
 proc=subprocess.run(cmd,capture_output=True,text=True);fixtures.append(dict(path=str(path.relative_to(ROOT)),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),command=cmd,exit_code=proc.returncode,stdout=proc.stdout,stderr=proc.stderr))
(OUT/'independent_validation.json').write_text(json.dumps(validation,indent=2));(OUT/'initial_fixture_validation.json').write_text(json.dumps(fixtures,indent=2))
print('validation',collections.Counter(v['exit_code'] for v in validation),'fixtures',collections.Counter(v['exit_code'] for v in fixtures))
