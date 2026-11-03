#!/usr/bin/env python3
"""Strict matched development analysis. Never modifies historical input files."""
import argparse
import csv
import hashlib
import json
import math
import statistics
from pathlib import Path

POLICIES = ['RESUMPTION_NO_PA', 'PA_EVERY_INTERRUPTION_FILTERED', 'PA_EVICTION_ONLY',
            'PA_TERMINAL_ONLY', 'PA_EVICTION_AND_TERMINAL', 'TERMINAL_RESERVE_CONTROL']
KEYS = ('map', 'scen', 'num_agents', 'time_budget_s', 'seed')

def exact_binomial_sign_test(wins, losses):
    n = wins + losses
    return min(1., 2*sum(math.comb(n,i) for i in range(min(wins,losses)+1))/2**n) if n else 1.

def exact_signed_rank(deltas):
    """Exact random-sign distribution with average ranks for tied magnitudes.
    Zero differences removed; doubled ranks keep every DP index integral.
    All-zero convention p=1 conveys no evidence of equivalence.
    """
    values = sorted((abs(d), d) for d in deltas if d)
    ranks = []; observed = 0; i = 0
    while i < len(values):
        j = i+1
        while j < len(values) and values[j][0] == values[i][0]: j += 1
        doubled_rank = i+1+j
        for _, d in values[i:j]:
            ranks.append(doubled_rank)
            if d > 0: observed += doubled_rank
        i = j
    distribution = {0: 1}
    for rank in ranks:
        updated = dict(distribution)
        for score, count in distribution.items(): updated[score+rank] = updated.get(score+rank,0)+count
        distribution = updated
    total_rank = sum(ranks); tail = min(observed,total_rank-observed)
    return min(1., 2*sum(c for s,c in distribution.items() if s <= tail)/2**len(ranks))

def summarize(deltas):
    nonzero = [d for d in deltas if d]
    n = len(deltas); wins = sum(d<0 for d in deltas); losses = sum(d>0 for d in deltas)
    sd = statistics.stdev(deltas) if n>1 else 0.
    return dict(n_pairs=n,wins=wins,ties=n-wins-losses,losses=losses,
                mean_delta_cost=statistics.mean(deltas),
                mean_non_ties=statistics.mean(nonzero) if nonzero else None,
                sample_sd=sd,standard_error=sd/math.sqrt(n),
                sign_two_sided=exact_binomial_sign_test(wins,losses),
                wilcoxon_two_sided_exact=exact_signed_rank(deltas))

def holm(comparisons, key):
    ordered = sorted(comparisons, key=lambda p: comparisons[p][key]); previous = 0.
    for i,p in enumerate(ordered):
        previous = max(previous,min(1.,(len(ordered)-i)*comparisons[p][key]))
        comparisons[p][key+'_holm'] = previous

def analyze(runs, policies=POLICIES):
    if not isinstance(runs,list) or not runs: raise ValueError('No raw runs')
    cells = {}
    for r in runs:
        for k in (*KEYS,'experiment_policy','cost_at_deadline','final_cost'):
            if k not in r: raise ValueError(f'Missing required field {k}')
        for k in ('cost_at_deadline','final_cost'):
            if type(r[k]) is not int or r[k]<0: raise ValueError(f'Invalid cost {k}')
        if r.get('independent_validation') != 'PASSED': raise ValueError('Unvalidated run')
        key = tuple(r[k] for k in KEYS); p=r['experiment_policy']
        if p not in policies: raise ValueError(f'Unknown policy {p}')
        if p in cells.setdefault(key,{}): raise ValueError(f'Duplicate cell/policy {key} {p}')
        cells[key][p]=r
    if any(set(c)!=set(policies) for c in cells.values()): raise ValueError('Incomplete matched cell')
    pairs=[]; comparisons={}; strata={}
    for p in policies[1:]:
        ds=[]; returns=[]
        for key,c in sorted(cells.items()):
            base=c[policies[0]]; candidate=c[p]
            delta=candidate['cost_at_deadline']-base['cost_at_deadline']; ds.append(delta)
            returns.append(candidate['final_cost']-base['final_cost'])
            pairs.append(dict(zip(KEYS,key),policy=p,baseline_deadline_cost=base['cost_at_deadline'],
                              policy_deadline_cost=candidate['cost_at_deadline'],delta_cost=delta,
                              baseline_return_cost=base['final_cost'],policy_return_cost=candidate['final_cost']))
        comparisons[p]=summarize(ds); comparisons[p]['return_cost_comparison']=summarize(returns)
        strata[p]={}
        for dimension in ('map','scen','num_agents','time_budget_s'):
            strata[p][dimension]={str(value):summarize([row['delta_cost'] for row in pairs if row['policy']==p and row[dimension]==value])
                                  for value in sorted({r[dimension] for r in runs})}
    for key in ('sign_two_sided','wilcoxon_two_sided_exact'): holm(comparisons,key)
    counters=('filter_evaluations','filter_skips','pa_committed','pa_total_gain','eviction_attempts','eviction_committed',
              'terminal_recovery_calls','terminal_checkpoints_examined','terminal_committed','total_iterations','total_expansions')
    metrics={}
    for p in policies:
        rr=[r for r in runs if r['experiment_policy']==p]
        metrics[p]={k:sum(r.get(k,0) for r in rr) for k in counters}
        over=[r['deadline_overshoot_s'] for r in rr]
        metrics[p].update(n_runs=len(rr),overrun_count=sum(x>0 for x in over),max_recorded_refinement_overshoot_s=max(over))
    return dict(metadata=dict(total_runs=len(runs),matched_cells=len(cells),
                distinct_instances=len({k[:3] for k in cells}),distinct_layouts=len({k[:2] for k in cells}),
                baseline=policies[0],delta_definition='policy deadline cost minus baseline deadline cost; negative is better',
                inference='Descriptive development evidence. Seeds, budgets and nested agent sets are correlated. Few independent layouts limit inference. Non-significance is not equivalence.',
                multiple_comparison=f'Holm adjustment across {len(policies)-1} predeclared policy comparisons, separately for sign and signed-rank tests.'),
                comparisons=comparisons,strata=strata,metrics=metrics,pairs=pairs)

def render(report):
    lines=['# Corrected selective recovery analysis','',json.dumps(report['metadata'],indent=2),'',
           '| Policy | Pairs | W/T/L | Mean delta | Non-tie mean | Sample SD | SE | Exact sign p | Exact rank p | Holm sign p | Holm rank p |',
           '|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|']
    for p,c in report['comparisons'].items():
        lines.append(f"| {p} | {c['n_pairs']} | {c['wins']}/{c['ties']}/{c['losses']} | {c['mean_delta_cost']:.6g} | {c['mean_non_ties']} | {c['sample_sd']:.6g} | {c['standard_error']:.6g} | {c['sign_two_sided']:.6g} | {c['wilcoxon_two_sided_exact']:.6g} | {c['sign_two_sided_holm']:.6g} | {c['wilcoxon_two_sided_exact_holm']:.6g} |")
    lines += ['', 'All-tie p=1 is a convention. Recovery gain within a run is not improvement over resumption.',
              'See analysis.json for return-cost comparisons, map/scenario/scale/budget strata and recovery counters.','']
    return '\n'.join(lines)

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--primary',action='store_true'); parser.add_argument('--input',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,required=True); args=parser.parse_args()
    if args.input.is_dir():
        files=sorted(args.input.glob('*.json'))
        runs=[json.loads(p.read_text()) for p in files]
    else:
        files=[args.input]; runs=json.loads(args.input.read_text())
    report=analyze(runs, ['DISCARD_NO_PA','RESUMPTION_NO_PA'] if args.primary else POLICIES)
    report['input_sha256']={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    args.output_dir.mkdir(parents=True,exist_ok=False)
    (args.output_dir/'analysis.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    for name,rows in [('pairs.csv',report['pairs']),('comparisons.csv',[dict(policy=p,**{k:v for k,v in c.items() if not isinstance(v,dict)}) for p,c in report['comparisons'].items()])]:
        with (args.output_dir/name).open('w',newline='') as f:
            w=csv.DictWriter(f,fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
    (args.output_dir/'summary.md').write_text(render(report))
    print(args.output_dir/'analysis.json')
if __name__=='__main__': main()
