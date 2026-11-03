import hashlib,json,subprocess,zipfile,csv,shutil,re
from pathlib import Path
out=Path(__file__).resolve().parent;root=out.parent
# Keep exact generated build configuration and verbose test logs without build objects.
prov=out/'build_evidence';prov.mkdir(exist_ok=True)
for mode in ['release','asan','documented-option']:
 b=out/('build-'+mode)
 for rel in ['CMakeCache.txt','compile_commands.json','CMakeFiles/rbl_engine.dir/flags.make','CMakeFiles/rbl_selective_recovery_tests.dir/flags.make','CMakeFiles/rbl_selective_recovery_tests.dir/link.txt','Testing/Temporary/LastTest.log']:
  p=b/rel
  if p.exists():
   dest=prov/(mode+'__'+rel.replace('/','__'));shutil.copyfile(p,dest)
# Validate all preservation-document checksums against files; report missing honestly.
rows=[]
for line in (root/'docs/selective_recovery_audit_and_preservation.md').read_text().splitlines():
 matches=re.findall(r'`([^`]+)`',line)
 if len(matches)>=2 and re.fullmatch('[0-9a-f]{64}',matches[-1]):
  p=root/matches[0];actual=hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else 'MISSING'
  rows.append({'path':matches[0],'reported_sha256':matches[-1],'actual_sha256':actual,'matches':actual==matches[-1]})
with (out/'preservation_document_hashes.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
tracked=[root/p for p in subprocess.check_output(['git','ls-files','-z'],cwd=root).decode().split('\0') if p]
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
 return h.hexdigest()
with (out/'EVIDENCE_SHA256.txt').open('w') as f:
 for p in sorted(tracked):
  if p.is_file(): f.write(sha(p)+'  '+str(p.relative_to(root))+'\n')
# Include only reviewable artifacts, never old audits, executables or build trees.
allowed={'.md','.csv','.json','.jsonl','.txt','.log','.diff','.py','.cpp','.hpp','.make'}
artifacts=[p for p in out.rglob('*') if p.is_file() and not any(part.startswith('build-') for part in p.relative_to(out).parts) and p.suffix in allowed and p.name!='AUDIT_SHA256.txt']
# flags/cache filenames in build_evidence can be nonstandard: include all those text files.
artifacts=sorted(set(artifacts)|set(p for p in prov.iterdir() if p.is_file()))
with (out/'AUDIT_SHA256.txt').open('w') as f:
 for p in artifacts:f.write(sha(p)+'  '+str(p.relative_to(out))+'\n')
repo_files=[]
for prefix in ['src/engine','src/profiler','src/validator','tests','docs','configs/2026-11-01_selective_recovery','results/2026-11-01_selective_recovery','results/partial_acceptance_offline/raw','results/partial_acceptance_pilot/init_paths','logs']:
 repo_files += [p for p in (root/prefix).rglob('*') if p.is_file()]
repo_files += [root/p for p in ['README.md','IMPLEMENTATION_STATUS.md','CMakeLists.txt','scripts/run_selective_recovery_pilot.py','scripts/analyze_selective_recovery.py','scripts/build_rbl.sh','results/partial_acceptance_pilot/all_runs.json','results/partial_acceptance_pilot/pilot_manifest.json','configs/partial_acceptance_pilot.json']]
# Maps/scenarios needed to validate exactly the used two-map datasets, not confirmation data.
for p in json.loads((out/'initial_fixture_converted_validation.json').read_text()):
 for flag in ['--map','--scen']:repo_files.append(Path(p['command'][p['command'].index(flag)+1]))
zip_path=out/'REVIEW_BUNDLE.zip'
with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for p in artifacts+[out/'AUDIT_SHA256.txt']:z.write(p,'audit/'+str(p.relative_to(out)))
 for p in sorted(set(repo_files)):
  if p.is_file():z.write(p,'repository/'+str(p.relative_to(root)))
(out/'REVIEW_BUNDLE.zip.sha256').write_text(sha(zip_path)+'  REVIEW_BUNDLE.zip\n')
with zipfile.ZipFile(zip_path) as z:assert z.testzip() is None
print('Evidence files:',len(tracked),'Audit text artifacts:',len(artifacts),'ZIP bytes:',zip_path.stat().st_size)
print('Preservation report hashes:',sum(r['matches'] for r in rows),'/',len(rows),'match')
print('Claim statuses:',dict(__import__('collections').Counter(r['status'] for r in csv.DictReader((out/'CLAIMS_VERIFICATION.csv').open()))))
