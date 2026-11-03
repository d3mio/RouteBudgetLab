#!/usr/bin/env python3
"""Read-only final package checks. Run from repository root after rendering documents."""
import hashlib,json,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path('scripts').resolve()))
from analyze_selective_recovery import analyze,POLICIES,render
from run_selective_recovery_pilot import reconcile,sha256_file

def read(p): return json.loads(p.read_text())
def require(ok, message):
    if not ok: raise ValueError(message)
def complete_manifest(directory):
    listed={}
    for line in (directory/'manifest.sha256').read_text().splitlines():
        digest,name=line.split('  ',1)
        require(name not in listed,'Duplicate manifest name')
        require(sha256_file(directory/name)==digest,f'Checksum mismatch: {directory/name}')
        listed[name]=digest
    actual={str(p.relative_to(directory)) for p in directory.rglob('*') if p.is_file() and p.name!='manifest.sha256'}
    require(actual==set(listed),f'Incomplete manifest: {directory}')
    return len(listed)

def main():
    repair=Path('results/repair_20261101_corrected_01');result={}
    preserved=read(repair/'preservation.json')
    for path,digest in preserved.items(): require(sha256_file(path)==digest,f'Historical mutation: {path}')
    result['historical_files_unchanged']=len(preserved)
    result['manifests']={};result['experiments']={}
    for repeat in ('01','02','03'):
        for kind,policies in [('selective',POLICIES),('primary',['DISCARD_NO_PA','RESUMPTION_NO_PA'])]:
            pilot=Path(f'results/2026-11-02_corrected_{kind}_pilot_{repeat}')
            result['manifests'][str(pilot)]=complete_manifest(pilot)
            rawfiles=sorted((pilot/'raw').glob('*.json'));runs=[read(p) for p in rawfiles]
            require(len(runs)==32*len(policies),'Run count')
            for r in runs: reconcile(r)
            require(len({r['provenance']['executable_sha256'] for r in runs})==1,'Mixed binaries')
            cells={}
            for r in runs:
                key=tuple(r[k] for k in ('map','scen','num_agents','time_budget_s','seed'))
                cells.setdefault(key,[]).append(r)
                validation=read(pilot/'logs'/f"{r['run_id']}.validation.json")
                require(validation['returncode']==0,'Independent validation failed')
                require(sha256_file(pilot/'paths'/f"{r['run_id']}.paths")==r['paths_sha256'],'Plan hash mismatch')
            for rs in cells.values():
                require(len({r['initial_plan_sha256'] for r in rs})==1,'Unmatched initial plan')
                require(len({json.dumps(r['input_sha256'],sort_keys=True) for r in rs})==1,'Unmatched map/scenario')
            analysis=Path(f'results/2026-11-02_corrected_{kind}_analysis_{repeat}')
            saved=read(analysis/'analysis.json');fresh=analyze(runs,policies)
            fresh['input_sha256']={str(p):sha256_file(p) for p in rawfiles}
            require(saved==fresh,'Analysis does not reproduce from raw files')
            require((analysis/'summary.md').read_text()==render(fresh),'Summary mismatch')
            if repeat=='03':
                for p,digest in read(pilot/'source_hashes.json').items(): require(sha256_file(p)==digest,f'Current source differs from experiment: {p}')
                require(sha256_file('build/corrected-release/rbl_profile_partial_acceptance')==runs[0]['provenance']['executable_sha256'],'Current executable differs')
            result['experiments'][str(pilot)]={'runs':len(runs),'validated':len(runs),'analysis_reproduces':True}
    result['manifests'][str(repair/'smoke_01')]=complete_manifest(repair/'smoke_01')
    result['manifests'][str(repair/'offline_replay_01')]=complete_manifest(repair/'offline_replay_01')
    result['offline_snapshot_summary']=read(repair/'offline_replay_01/summary.json')
    compile_commands=read(Path('build/corrected-asan/compile_commands.json'))
    require(all(all(flag in c['command'] for flag in ('-fsanitize=address,undefined','-fno-omit-frame-pointer','-g')) for c in compile_commands),'Uninstrumented unit')
    links=list(Path('build/corrected-asan/CMakeFiles').glob('*/link.txt'))
    require(all('-fsanitize=address,undefined' in p.read_text() for p in links if ' -o ' in p.read_text()),'Uninstrumented executable link')
    symbols=subprocess.check_output(['nm','-u','build/corrected-asan/rbl_recovery_correctness_tests'],text=True)
    require('___asan_init' in symbols and '___ubsan_handle' in symbols,'Missing sanitizer symbols')
    result['instrumented_compile_units']=len(compile_commands)
    result['linked_asan_ubsan_symbols']=True
    require(subprocess.run(['python3','docs/render_corrected_package.py','--verify'],stdout=subprocess.PIPE).returncode==0,'Document tables mismatch')
    result['document_tables_match']=True
    result['vendor_check']=subprocess.check_output(['bash','scripts/verify_upstream.sh'],text=True).strip()
    print(json.dumps(result,indent=2))
if __name__=='__main__': main()
