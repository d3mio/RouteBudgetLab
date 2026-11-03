#!/usr/bin/env python3
"""Small natural snapshot replay on the same exposed fixtures; not a quality pilot."""
import argparse,json,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path('scripts').resolve()))
from run_selective_recovery_pilot import dump,manifest,sha256_file
parser=argparse.ArgumentParser();parser.add_argument('--output-dir',type=Path,default=Path('results/repair_20261101_corrected_01/offline_replay_01'))
out=parser.parse_args().output_dir
out.mkdir(exist_ok=False)
config=json.loads(Path('configs/2026-11-02_corrected/pilot.json').read_text())
binary=Path('build/corrected-release/rbl_profile_partial_acceptance')
summary=[]
for m in config['grid']['maps']:
    scen=m['scenarios'][0];stem=Path(m['path']).stem
    init=Path('results/partial_acceptance_pilot/init_paths')/f'{stem}__{Path(scen).stem}__a100.paths'
    raw=out/f'{stem}.json'
    command=[str(binary),'--mode','offline_snapshots','--map',m['path'],'--scen',scen,'--agents','100',
             '--seed','0','--budget','0.05','--slice','0.005','--neighborhood-size','8','--policy','RESUMPTION_NO_PA',
             '--load-init-paths',str(init),'--output',str(raw)]
    p=subprocess.run(command,text=True,capture_output=True)
    (out/f'{stem}.stdout').write_text(p.stdout);(out/f'{stem}.stderr').write_text(p.stderr)
    dump(out/f'{stem}.execution.json',dict(command=command,returncode=p.returncode,executable_sha256=sha256_file(binary),
        git_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        source_provenance='results/2026-11-02_corrected_selective_pilot_03/provenance.json',
        inputs={str(f):sha256_file(f) for f in (Path(m['path']),Path(scen),init)}))
    if p.returncode:
        manifest(out);raise RuntimeError('Replay failure retained')
    r=json.loads(raw.read_text());ss=r['snapshots']
    if not ss: manifest(out);raise ValueError('No snapshot evidence captured')
    for s in ss:
        if not (s['candidate_count']<=8 and s['oracle_ran'] and s['greedy_gain']<=s['oracle_gain']
                and s['exact_gain']==s['oracle_gain'] and s['exact_mixture_valid']):
            manifest(out);raise ValueError('Snapshot assertion failed')
    summary.append(dict(map=m['path'],snapshots=len(ss),greedy_below_oracle=sum(s['greedy_gain']<s['oracle_gain'] for s in ss),
                        positive_oracle=sum(s['oracle_gain']>0 for s in ss),**r['summary']))
dump(out/'summary.json',summary);manifest(out)
print(json.dumps(summary,indent=2))
