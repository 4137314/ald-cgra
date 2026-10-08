import hashlib,json,shutil,subprocess,tempfile,time
from pathlib import Path
root=Path.cwd()
out=root/'doc/verification/2026-10-08/block-compiler'
out.mkdir()
snapshot=Path(tempfile.mkdtemp(prefix='cgra-fixed-toolchain-'))
shutil.copy2(root/'Makefile', snapshot/'Makefile')
for part in ('lib','sw','scripts'):
    shutil.copytree(root/part,snapshot/part,ignore=shutil.ignore_patterns('build','__pycache__'))
runner=root/'doc/verification/2026-10-06/plans-compiler/runner.py'
shutil.copy2(runner,out/'runner.py'); shutil.copy2(runner,snapshot/'runner.py')
image='sha256:c3649cca2305a4bfcab418350466f7a3da6be212a2ac28633435b282708fdad3'
inputs={str(p.relative_to(snapshot)):hashlib.sha256(p.read_bytes()).hexdigest() for p in snapshot.rglob('*') if p.is_file()}
(out/'inputs.json').write_text(json.dumps(dict(commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),dirty=subprocess.check_output(['git','status','--porcelain'],text=True),image=image,snapshot=str(snapshot),sources_sha256=inputs),indent=2)+'\n')
results=[]
for cc in ('gcc-13','clang-18'):
    cmd=['docker','run','--rm','-v',str(snapshot)+':/src:ro',image,'python3','/src/runner.py',cc]
    start=time.monotonic()
    with (out/(cc+'.txt')).open('w') as log:
        result=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
    results.append(dict(compiler=cc,command=cmd,exit_code=result.returncode,status='PASS' if result.returncode==0 else 'FAIL',duration_seconds=time.monotonic()-start))
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print(cc,results[-1]['status'],flush=True)
raise SystemExit(any(r['exit_code'] for r in results))
