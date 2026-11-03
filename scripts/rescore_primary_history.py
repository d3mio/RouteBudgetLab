#!/usr/bin/env python3
"""Read-only revalidation of exposed historical Phase 7 saved plans."""
import argparse,csv,json,subprocess
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--validator',type=Path,required=True);ap.add_argument('--output-dir',type=Path,required=True);args=ap.parse_args()
args.output_dir.mkdir(parents=True,exist_ok=False)
m=json.loads(Path('results/phase7/verified_reference/verified_reference_manifest.json').read_text())
rows=[]
for rid,r in m['runs'].items():
    cmd=[str(args.validator),'--map',f'data/test/maps/{r["map_name"]}.map','--scen',f'data/test/scenarios/{r["scenario_name"]}.scen',
         '--agents',str(r['agent_count']),'--paths',r['retained_paths_file'],'--coords','rc' if r['method']=='BASELINE_BALANCE' else 'xy','--reported-cost',str(r['final_cost'])]
    p=subprocess.run(cmd,capture_output=True,text=True)
    try:d=json.loads(p.stdout)
    except ValueError:d={'status':'MALFORMED','independent_sum_of_costs':None,'error':p.stderr}
    rows.append(dict(run_id=rid,method=r['method'],reported_cost=r['final_cost'],authoritative_cost=d.get('independent_sum_of_costs'),status=d['status'],exit_code=p.returncode))
with (args.output_dir/'rescored.csv').open('w',newline='') as f:
    w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
summary=dict(runs=len(rows),valid=sum(r['exit_code']==0 for r in rows),cost_matches=sum(r['reported_cost']==r['authoritative_cost'] for r in rows),
             scope='Previously exposed saved final plans only; no untouched confirmation data or new solver run. Historical deadline-publication timing/provenance is not repaired by rescoring.')
(args.output_dir/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(summary)
if summary['valid']!=len(rows):raise SystemExit(1)
