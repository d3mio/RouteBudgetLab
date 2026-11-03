import pathlib,json,hashlib,csv,statistics,math,itertools,collections,subprocess
A=pathlib.Path(__file__).resolve().parent; R=A.parent; D=R/'results/2026-11-01_selective_recovery'
raw=[json.loads(p.read_text()) for p in sorted((D/'raw').glob('*.json'))]
key=lambda r:tuple(r[k] for k in ['map','scen','num_agents','time_budget_s','seed'])
cells=collections.defaultdict(dict); duplicates=[]
for r in raw:
 if r['experiment_policy'] in cells[key(r)]: duplicates.append(r['run_id'])
 cells[key(r)][r['experiment_policy']]=r
pols=list(json.loads((R/'configs/2026-11-01_selective_recovery/selective_recovery_pilot.json').read_text())['policies']); pols=[p['name'] for p in pols]
rows=[]; pairrows=[]; summary={'runs':len(raw),'cells':len(cells),'duplicates':duplicates,'missing':[(str(k),p) for k,v in cells.items() for p in pols if p not in v]}
for p in pols:
 diffs=[]; iterations=[]
 for k,v in sorted(cells.items()):
  d=v[p]['cost_at_deadline']-v['RESUMPTION_NO_PA']['cost_at_deadline']; diffs.append(d);iterations.append(v[p]['total_iterations']-v['RESUMPTION_NO_PA']['total_iterations'])
  pairrows.append(dict(map=pathlib.Path(k[0]).name,scenario=pathlib.Path(k[1]).name,agents=k[2],budget=k[3],seed=k[4],policy=p,delta_cost=d,baseline=v['RESUMPTION_NO_PA']['cost_at_deadline'],deadline_cost=v[p]['cost_at_deadline']))
 nz=[d for d in diffs if d]; w=sum(d<0 for d in diffs);l=sum(d>0 for d in diffs);n=len(nz)
 sign=min(1,2*sum(math.comb(n,i) for i in range(min(w,l)+1))/2**n) if n else 1
 # Enumerate signed-rank null distribution on nonzero differences, midranks for tied magnitudes.
 ranks=[1+sum(abs(x)<abs(d) for x in nz)+(sum(abs(x)==abs(d) for x in nz)-1)/2 for d in nz]
 observed=sum(r for r,d in zip(ranks,nz) if d>0)
 sums=[sum(r for r,b in zip(ranks,bits) if b) for bits in itertools.product([0,1],repeat=n)]
 p2=min(1,2*min(sum(s<=observed for s in sums),sum(s>=observed for s in sums))/len(sums)) if n else 1
 rows.append(dict(policy=p,pairs=len(diffs),wins=w,ties=len(diffs)-n,losses=l,mean_all=statistics.mean(diffs),mean_nonties=statistics.mean(nz) if n else '',sample_sd_ddof1=statistics.stdev(diffs),sem=statistics.stdev(diffs)/math.sqrt(len(diffs)),sign_two_sided=sign,wilcoxon_exact_two_sided=p2,wilcoxon_exact_less=sum(s<=observed for s in sums)/len(sums) if n else 1,mean_iteration_difference=statistics.mean(iterations),nonzero_deltas=str(nz)))
for name,rs in [('RECOMPUTED_RESULTS.csv',rows),('PAIRED_VALUES.csv',pairrows)]:
 with (A/name).open('w') as f:
  out=csv.DictWriter(f,fieldnames=rs[0].keys());out.writeheader();out.writerows(rs)
summary['policies']={}
for p in pols:
 rs=[r for r in raw if r['experiment_policy']==p]
 fields=['filter_evaluations','filter_skips','filter_total_time_us','extraction_total_time_us','pa_attempts','pa_positive_opportunities','pa_committed','pa_total_gain','pa_budget_skipped','pa_total_time_us','eviction_attempts','eviction_committed','terminal_recovery_calls','terminal_checkpoints_examined','terminal_filter_skips','terminal_committed','stagnant_checkpoints_evicted','total_iterations','total_expansions']
 summary['policies'][p]={k:sum(r.get(k,0) for r in rs) for k in fields}
 summary['policies'][p]['overruns']={str(b):{'n':16,'count':sum(r['deadline_overshoot_s']>0 for r in rs if r['time_budget_s']==b),'max_us':max(r['deadline_overshoot_s']*1e6 for r in rs if r['time_budget_s']==b)} for b in [1.,5.]}
 summary['policies'][p]['deadline_final_differences']=sum(r['final_cost']!=r['cost_at_deadline'] for r in rs)
summary['aggregate_matches_raw']=sorted(raw,key=lambda r:r['run_id'])==sorted(json.loads((D/'all_runs.json').read_text()),key=lambda r:r['run_id'])
manifest=[]
for line in (D/'manifest.sha256').read_text().splitlines():
 h,p=line.split(maxsplit=1); f=D/p; actual=hashlib.sha256(f.read_bytes()).hexdigest();manifest.append({'file':p,'match':h==actual})
summary['original_manifest']={kind:dict(total=sum(x['file'].startswith(kind) for x in manifest),matched=sum(x['file'].startswith(kind) and x['match'] for x in manifest)) for kind in ['raw','paths']}
summary['initial_costs_match']=all(len({r['initial_cost'] for r in v.values()})==1 for v in cells.values())
summary['configuration_values']={k:sorted({str(r.get(k)) for r in raw}) for k in ['slice_budget_s','neighborhood_size','terminal_reserve_s','initial_plan_source','partial_acceptance_policy']}
agg=json.loads((D/'all_runs.json').read_text()); summary['rotation_order_matches']=all([r['experiment_policy'] for r in agg[i*6:i*6+6]]==pols[i%6:]+pols[:i%6] for i in range(32))
summary['positions']={p:[sum(agg[i]['experiment_policy']==p for i in range(j,len(agg),6)) for j in range(6)] for p in pols}
snaps=[]
for f in (R/'results/partial_acceptance_offline/raw').glob('*.json'):snaps+=json.loads(f.read_text()).get('snapshots',[])
summary['offline']={'files':len(list((R/'results/partial_acceptance_offline/raw').glob('*.json'))),'snapshots':len(snaps),'negative_candidates':sum(sum(w<0 for w in s['weights']) for s in snaps),'any_negative_snapshots':sum(any(w<0 for w in s['weights']) for s in snaps),'selected_negative_snapshots':sum(s['exact_selected_negative_count']>0 for s in snaps),'selected_negative_net_gain':sum(s['exact_gain'] for s in snaps if s['exact_selected_negative_count']>0)}
(A/'raw_checks.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2)); print((A/'RECOMPUTED_RESULTS.csv').read_text())
