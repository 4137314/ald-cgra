from pathlib import Path
import hashlib,json,shutil,subprocess,time
from datetime import datetime,timezone
repo=Path.cwd();evidence=repo/'doc/verification/2026-09-30/synchronizers/routed-second'
evidence.mkdir(exist_ok=False)
run=repo/'hw/build/vivado/synchronizers-routed-second-20260930'
run.mkdir(parents=True,exist_ok=False)
for part in ('rtl','scr','con'):shutil.copytree(repo/'hw'/part,run/part)
shutil.copy2(evidence.parent/'inspect-routed.tcl',run/'inspect.tcl')
command=['/opt/Xilinx/2026.1/Vivado/bin/vivado','-mode','batch','-nolog','-nojournal','-source','inspect.tcl']
m=dict(command=command,cwd=str(run),started=datetime.now(timezone.utc).isoformat(),commit=subprocess.check_output(['git','rev-parse','HEAD']).decode().strip(),dirty=bool(subprocess.check_output(['git','status','--porcelain'])),source_sha256={str(p.relative_to(run)):hashlib.sha256(p.read_bytes()).hexdigest() for p in run.rglob('*') if p.is_file()},board='nexys_a7',part='xc7a100tcsg324-1',parameters=dict(rows=4,cols=4,step_div=2,period_ns=10),verification='synthesis, structural inspection and routed setup/hold; no bitstream or board programming')
start=time.monotonic()
with (evidence/'output.txt').open('wb') as log:
 r=subprocess.run(command,cwd=run,stdout=log,stderr=subprocess.STDOUT)
m.update(exit_code=r.returncode,status='PASS' if r.returncode==0 else 'FAIL',duration_seconds=time.monotonic()-start)
if (run/'reports').exists():
 shutil.copytree(run/'reports',evidence/'reports',ignore=shutil.ignore_patterns('*.dcp'))
 m['artifact_sha256']={str(p.relative_to(run)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (run/'reports').iterdir() if p.is_file()}
(evidence/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
print(m['status'],evidence/'manifest.json');raise SystemExit(r.returncode)
