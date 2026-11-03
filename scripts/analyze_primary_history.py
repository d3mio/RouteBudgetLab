#!/usr/bin/env python3
"""Reconstruct historical final-plan comparisons with one explicit sign convention."""
import argparse,csv,json,sys
from pathlib import Path
from analyze_selective_recovery import summarize
ap=argparse.ArgumentParser();ap.add_argument('--rescored',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);args=ap.parse_args()
if args.output.exists():raise SystemExit('Output exists')
rows={r['run_id']:r for r in csv.DictReader(args.rescored.open())}
m=json.loads(Path('results/phase7/verified_reference/verified_reference_manifest.json').read_text())['runs']
previous=json.loads(Path('results/phase7/verified_statistical_analysis.json').read_text())['comparisons']
comparisons={}
for label,definition in previous.items():
    control=definition['control'];treatment=definition['treatment'];groups={}
    for rid,r in m.items():
        if rid not in rows or rows[rid]['status']!='VALID':raise ValueError('Missing valid rescored plan')
        key=(r['map_name'],r['scenario_name'],r['agent_count'],r['budget_s'])
        groups.setdefault(key,{})[r['method']]=int(rows[rid]['authoritative_cost'])
    pairs=[dict(map=k[0],scenario=k[1],agents=k[2],budget=k[3],delta_cost=v[treatment]-v[control]) for k,v in groups.items() if control in v and treatment in v]
    comparisons[label]=dict(control=control,treatment=treatment,summary=summarize([r['delta_cost'] for r in pairs]),pairs=pairs)
args.output.write_text(json.dumps(dict(outcome='Historical retained FINAL cost; exact publication timing not reconstructed',delta='treatment minus control; negative favors treatment',limitations='Repeated budgets and nested agent sets are correlated; historical provenance incomplete. No new confirmation instances used.',comparisons=comparisons),indent=2)+'\n')
