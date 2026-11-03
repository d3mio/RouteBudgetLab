import subprocess,sys,json,time,shlex
from pathlib import Path
root=Path(__file__).resolve().parent
name=sys.argv[1]; cmd=sys.argv[2:]
t=time.time()
r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(root/(name+'.log')).write_text(r.stdout)
with (root/'commands.jsonl').open('a') as f: f.write(json.dumps(dict(name=name,command=cmd,exit_code=r.returncode,elapsed_s=time.time()-t))+'\n')
print(name, 'exit',r.returncode, r.stdout[-1500:])
sys.exit(r.returncode)
