import subprocess,pathlib,json,platform,os
A=pathlib.Path(__file__).resolve().parent
root=A.parent
commands=[]
def run(name,args):
 p=subprocess.run(args,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 (A/(name+'.log')).write_text(p.stdout)
 commands.append({'name':name,'args':args,'exit_code':p.returncode})
 (A/'commands.json').write_text(json.dumps(commands,indent=2))
 print(name,p.returncode,flush=True)
 return p.returncode
run('provenance',['git','status','--porcelain=v1'])
run('history',['git','log','-3','--format=fuller','--stat'])
run('toolchain',['/Library/Developer/CommandLineTools/usr/bin/clang++','--version'])
run('host',['uname','-a'])
for mode in ['release','asan']:
 b=str(A/('build-'+mode))
 args=['cmake','-S',str(root),'-B',b,'-DCMAKE_BUILD_TYPE='+('Release' if mode=='release' else 'Debug'),'-DCMAKE_CXX_COMPILER=/Library/Developer/CommandLineTools/usr/bin/clang++','-DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk','-DBOOST_ROOT=/opt/homebrew/opt/boost']
 if mode=='asan': args+=['-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1']
 if run(mode+'-configure',args)==0 and run(mode+'-build',['cmake','--build',b,'-j2'])==0:
  run(mode+'-ctest',['ctest','--test-dir',b,'--output-on-failure'])
