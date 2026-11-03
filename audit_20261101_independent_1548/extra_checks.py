import pathlib,json,subprocess,shlex,hashlib
A=pathlib.Path(__file__).resolve().parent;R=A.parent
records=[]
def run(name,args):
 p=subprocess.run(args,cwd=R,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(A/(name+'.log')).write_text(p.stdout);records.append({'name':name,'args':args,'exit_code':p.returncode});(A/'extra_commands.json').write_text(json.dumps(records,indent=2));return p
for mode in ['release','asan']:
 B=A/('build-'+mode); commands=json.loads((B/'compile_commands.json').read_text());c=next(c for c in commands if c['file'].endswith('/selective_recovery_tests.cpp'))
 args=shlex.split(c['command']); idx=args.index('-o'); args[idx+1]=str(A/('probe-'+mode+'.o'));args[-1]=str(A/'probe.cpp')
 if run('probe-'+mode+'-compile',args).returncode==0:
  link=shlex.split((B/'CMakeFiles/rbl_selective_recovery_tests.dir/link.txt').read_text());link=[str(A/('probe-'+mode+'.o')) if x.endswith('selective_recovery_tests.cpp.o') else str(B/x) if x in ['librbl_engine.a','libanytime_mapf_core.a'] else x for x in link];link[link.index('-o')+1]=str(A/('probe-'+mode))
  if run('probe-'+mode+'-link',link).returncode==0: print(run('probe-'+mode,[str(A/('probe-'+mode)),'data/sample/random-32-32-20.map','data/sample/random-32-32-20-random-1.scen']).stdout)
D=R/'results/2026-11-01_selective_recovery';val=[]
for f in sorted((D/'raw').glob('*.json')):
 r=json.loads(f.read_text());p=run('validate-'+f.stem,[str(A/'build-release/rbl_validate'),'--map',r['map'],'--scen',r['scen'],'--agents',str(r['num_agents']),'--paths',str(D/'paths'/(f.stem+'.paths')),'--reported-cost',str(r['final_cost'])]);val.append({'run':f.stem,'exit_code':p.returncode})
(A/'independent_validation.json').write_text(json.dumps(val,indent=2));print('validated',len(val),'failures',sum(v['exit_code']!=0 for v in val))
run('upstream',['bash','scripts/verify_upstream.sh'])
run('submodules',['git','submodule','status','--recursive'])
run('vendored-tree',['git','ls-tree','HEAD','third_party/'])
for rev in ['b4d9f9e','07973d0','fb6ba90']:run('commit-'+rev,['git','show','--format=fuller','--stat',rev,'--','src','tests','CMakeLists.txt','scripts'])
run('implementation-diff',['git','diff','07973d0','fb6ba90','--','src','CMakeLists.txt'])
