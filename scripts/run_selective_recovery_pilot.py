#!/usr/bin/env python3
"""Sequential, immutable-output development experiment; no historical overwrite."""
import argparse
import hashlib
import json
import platform
import subprocess
import sys
import time
from pathlib import Path
from analyze_selective_recovery import POLICIES

def sha256_file(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(1048576),b''): h.update(block)
    return h.hexdigest()

def dump(path,value):
    path.write_text(json.dumps(value,indent=2,allow_nan=False)+'\n')

def output(cmd):
    return subprocess.check_output(cmd,text=True).strip()

def manifest(directory):
    files=sorted(p for p in directory.rglob('*') if p.is_file() and p.name!='manifest.sha256')
    (directory/'manifest.sha256').write_text(''.join(f'{sha256_file(p)}  {p.relative_to(directory)}\n' for p in files))
    verify_manifest(directory)

def verify_manifest(directory):
    for line in (directory/'manifest.sha256').read_text().splitlines():
        digest,name=line.split('  ',1)
        if sha256_file(directory/name)!=digest: raise ValueError(f'Manifest mismatch: {name}')

def reconcile(r):
    events=r['recovery_events']
    for trigger,attempt_key,commit_key in [('interruption','pa_attempts','pa_committed'),
            ('capacity_eviction','eviction_attempts','eviction_committed'),('terminal','terminal_checkpoints_examined','terminal_committed')]:
        es=[e for e in events if e['trigger']==trigger]
        if len(es)!=r[attempt_key]: raise ValueError(f'Event count mismatch {trigger}')
        if sum(e['outcome']=='COMMITTED' for e in es)!=r[commit_key]: raise ValueError(f'Commit count mismatch {trigger}')
    for e in events:
        if e['outcome']=='COMMITTED' and (not e['commit_on_time'] or e['commit_complete_s']>r['time_budget_s']):
            raise ValueError('Late commit credited')
        if e['candidates'] != e['positive']+e['zero']+e['negative']: raise ValueError('Candidate signs mismatch')
    if r['cost_at_deadline'] != r['final_cost']: raise ValueError('Late incumbent escaped rollback')

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--config',type=Path,required=True)
    ap.add_argument('--output-dir',type=Path,required=True); ap.add_argument('--build-dir',type=Path,required=True)
    ap.add_argument('--primary',action='store_true'); ap.add_argument('--smoke-cells',type=int,default=0); ap.add_argument('--verify-manifest',type=Path)
    args=ap.parse_args(); root=Path.cwd(); out=args.output_dir
    if args.verify_manifest: verify_manifest(args.verify_manifest); return
    config=json.loads(args.config.read_text()); build=args.build_dir.resolve()
    profiler=build/'rbl_profile_partial_acceptance'; validator=build/'rbl_validate'
    if not profiler.exists() or not validator.exists(): raise ValueError('Missing binaries')
    out.mkdir(parents=True,exist_ok=False)
    for d in ('raw','paths','logs'): (out/d).mkdir()
    grid=config['grid']; cells=[]
    for m in grid['maps']:
        for scen in m['scenarios']:
            for agents in grid['agent_counts']:
                for budget in grid['budgets_seconds']:
                    for seed in grid['seeds']:
                        cells.append(dict(map=m['path'],scen=scen,agents=agents,budget=budget,seed=seed))
    if args.smoke_cells: cells=cells[:args.smoke_cells]
    policies=config['policies']
    if [p['name'] for p in policies]!=(['DISCARD_NO_PA','RESUMPTION_NO_PA'] if args.primary else POLICIES): raise ValueError('Protocol must compare all six predeclared policies')
    order=[dict(cell_index=i,cell=c,policies=[p['name'] for p in policies[i%len(policies):]+policies[:i%len(policies)]]) for i,c in enumerate(cells)]
    dump(out/'execution_order.json',order); dump(out/'frozen_config.json',config)
    source_files=sorted({p for d in ('src','tests','scripts','configs','third_party') for p in Path(d).rglob('*') if p.is_file() and '__pycache__' not in p.parts}|{Path('CMakeLists.txt')})
    source_hashes={str(p):sha256_file(p) for p in source_files}
    source_hash=hashlib.sha256(json.dumps(source_hashes,sort_keys=True).encode()).hexdigest()
    compile_commands=json.loads((build/'compile_commands.json').read_text())
    compiler=compile_commands[0]['command'].split()[0].strip('"')
    dynamic_command=['otool','-L',str(profiler)] if platform.system()=='Darwin' else ['ldd',str(profiler)]
    dynamic_listing=output(dynamic_command)
    dynamic_libraries={}
    for line in dynamic_listing.splitlines()[1:]:
        name=line.strip().split()[0]
        if name.startswith('/'):
            library=Path(name)
            dynamic_libraries[name]=sha256_file(library) if library.is_file() else 'OS shared cache; recorded OS identity'
    hardware={}
    if platform.system()=='Darwin':
        for key in ('hw.model','hw.ncpu','machdep.cpu.brand_string'):
            hardware[key]=output(['sysctl','-n',key])
    provenance=dict(git_commit=output(['git','rev-parse','HEAD']),dirty_status=output(['git','status','--porcelain']),
        source_tree_sha256=source_hash,executable_sha256=sha256_file(profiler),validator_sha256=sha256_file(validator),
        libraries={str(p):sha256_file(p) for p in build.glob('*.a')},dynamic_libraries=dynamic_libraries,dynamic_link_listing=dynamic_listing,hardware=hardware,compiler=output([compiler,'--version']),
        platform=platform.platform(),architecture=platform.machine(),python=sys.version,
        compile_commands=compile_commands,cmake_cache=(build/'CMakeCache.txt').read_text(),
        linker_commands={str(p.relative_to(build)):p.read_text() for p in build.glob('CMakeFiles/*/link.txt')},
        configuration_sha256=sha256_file(args.config),
        dataset_partition_sha256=hashlib.sha256(json.dumps(grid,sort_keys=True).encode()).hexdigest(),
        execution_mode='sequential single solver process; background OS activity not controlled',
        experiment_kind='SMOKE, NOT FINAL PILOT' if args.smoke_cells else 'matched development pilot',
        timing_definitions={'budget_start':'steady_clock immediately before refinement controller setup',
            'deadline':'budget_start + declared budget','publication':'staged swaps plus metadata, followed by clock check and rollback if late',
            'refinement_return':'after recovery and cleanup, before output serialization',
            'return_boundary':'after local destruction; excludes writing its own sidecar and runtime exit',
            'process_return':'parent perf_counter around subprocess; includes initialization and serialization',
            'initial_plan':'shared validated fixture outside refinement budget',
            'reserve':'heuristic, not a hard real-time guarantee'})
    dump(out/'provenance.json',provenance); dump(out/'source_hashes.json',source_hashes)
    runs=[]
    for i,c in enumerate(cells):
        for position,p in enumerate(policies[i%len(policies):]+policies[:i%len(policies)]):
            rid=f'cell{i:02d}_pos{position}_{p["name"]}'
            raw=out/'raw'/f'{rid}.json'; paths=out/'paths'/f'{rid}.paths'
            init=Path('results/partial_acceptance_pilot/init_paths')/f'{Path(c["map"]).stem}__{Path(c["scen"]).stem}__a{c["agents"]}.paths'
            parameters=config['base_parameters']
            cmd=[str(profiler),'--mode','online','--map',c['map'],'--scen',c['scen'],'--agents',str(c['agents']),
                 '--seed',str(c['seed']),'--budget',str(c['budget']),'--slice',str(parameters['slice_duration_seconds']),
                 '--neighborhood-size',str(parameters['neighborhood_size']),'--policy',p['policy_arg'],
                 '--terminal-reserve',str(parameters['terminal_reserve_seconds']),'--load-init-paths',str(init),
                 '--dump-final-paths',str(paths),'--output',str(raw)]
            if p.get('terminal_control'): cmd.append('--terminal-reserve-control')
            inputs={str(f):sha256_file(f) for f in (Path(c['map']),Path(c['scen']),init)}
            start=time.perf_counter(); proc=subprocess.run(cmd,capture_output=True,text=True); elapsed=time.perf_counter()-start
            (out/'logs'/f'{rid}.stdout').write_text(proc.stdout); (out/'logs'/f'{rid}.stderr').write_text(proc.stderr)
            dump(out/'logs'/f'{rid}.execution.json',dict(command=cmd,returncode=proc.returncode,process_elapsed_s=elapsed))
            if proc.returncode or not raw.exists():
                manifest(out); raise RuntimeError(f'Failed run preserved: {rid}')
            try:
                r=json.loads(raw.read_text()); reconcile(r)
            except Exception:
                manifest(out); raise
            timing_file=Path(str(raw)+'.timing.json')
            r['output_timing']=json.loads(timing_file.read_text())
            timing_file.rename(out/'logs'/f'{rid}.timing.json')
            return_file=Path(str(raw)+'.return.json')
            r['return_timing']=json.loads(return_file.read_text())
            return_file.rename(out/'logs'/f'{rid}.return.json')
            valcmd=[str(validator),'--map',c['map'],'--scen',c['scen'],'--agents',str(c['agents']),'--paths',str(paths),'--reported-cost',str(r['final_cost'])]
            val=subprocess.run(valcmd,capture_output=True,text=True)
            dump(out/'logs'/f'{rid}.validation.json',dict(command=valcmd,returncode=val.returncode,stdout=val.stdout,stderr=val.stderr))
            if val.returncode:
                manifest(out); raise RuntimeError(f'Validation failure preserved: {rid}')
            r.update(run_id=rid,experiment_policy=p['name'],cell_index=i,policy_position=position,rotation_shift=i%len(policies),
                     independent_validation='PASSED',input_sha256=inputs,initial_plan_sha256=inputs[str(init)],
                     paths_sha256=sha256_file(paths),provenance=provenance,process_return_elapsed_s=elapsed)
            dump(raw,r); runs.append(r)
            print(f'{len(runs)}/{len(cells)*len(policies)} {rid}: deadline={r["cost_at_deadline"]} overshoot={r["deadline_overshoot_s"]}',flush=True)
    dump(out/'all_runs.json',runs)
    dump(out/'completion.json',dict(expected=len(cells)*len(policies),completed=len(runs),validated=len(runs),kind=provenance['experiment_kind']))
    manifest(out)
    print('Completed output manifest verified:',out,flush=True)
if __name__=='__main__': main()
