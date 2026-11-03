import json,shlex,subprocess
from pathlib import Path
out=Path(__file__).resolve().parent
for mode in ['release','asan']:
 b=out/('build-'+mode)
 cc=next(c for c in json.loads((b/'compile_commands.json').read_text()) if c['file'].endswith('/tests/selective_recovery_tests.cpp'))
 cmd=shlex.split(cc['command']); cmd[cmd.index('-o')+1]=str(out/('probe-'+mode+'.o'));cmd[-1]=str(out/'probe.cpp')
 p=subprocess.run(cmd,capture_output=True,text=True); (out/('probe-compile-'+mode+'.log')).write_text(p.stdout+p.stderr); print(mode,'compile',p.returncode);p.check_returncode()
 link=shlex.split((b/'CMakeFiles/rbl_selective_recovery_tests.dir/link.txt').read_text());link[link.index('-o')+1]=str(out/('probe-'+mode));link=[str(out/('probe-'+mode+'.o')) if x.endswith('selective_recovery_tests.cpp.o') else x for x in link]
 p=subprocess.run(link,cwd=b,capture_output=True,text=True);(out/('probe-link-'+mode+'.log')).write_text(p.stdout+p.stderr);print(mode,'link',p.returncode);p.check_returncode()
 with (out/'probe_commands.jsonl').open('a') as f:f.write(json.dumps({'compile':cmd,'link':link,'link_cwd':str(b)})+'\n')
