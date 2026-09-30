#!/usr/bin/env python3
"""Capture each actual CLI point once; run from the repository root."""
from pathlib import Path
import hashlib,json,os,subprocess,tempfile
output=Path(__file__).resolve().parent/'geometry-points'
output.mkdir(exist_ok=False)
points=[]
with tempfile.TemporaryDirectory(prefix='cgra-benchmark-config-') as temp:
    empty=Path(temp)/'empty.cgra';empty.write_text('')
    env=dict(os.environ, CGRA_CONFIG=str(empty), XDG_CONFIG_HOME=temp, CGRA_PATH='')
    for key in ('CGRA_PORT','CGRA_DEVICE','CGRA_BAUD'):
        env.pop(key,None)
    for geometry in ('1x16','2x8','4x4','8x2','16x1'):
        for version in (2,3):
            for mode,size in (('add',256),('scan',256),('matvec',16),('conv',256)):
                name=f'{geometry}-v{version}-{mode}-{size}'
                command=['sw/build/cgra','bench',mode,'-d',f'sim:{geometry}'+(':v2' if version==2 else ''),'--size',str(size),'--repeat','5','--json']
                result=subprocess.run(command,env=env,capture_output=True,timeout=60)
                path=output/(name+'.json');path.write_bytes(result.stdout)
                (output/(name+'.stderr.txt')).write_bytes(result.stderr)
                data=json.loads(result.stdout)
                points.append(dict(id=name,command=command,exit_code=result.returncode,path=path.name,sha256=hashlib.sha256(result.stdout).hexdigest(),status='PASS' if result.returncode==0 and data['ok'] else 'FAIL'))
(output/'manifest.json').write_text(json.dumps(dict(schema_version=1,points=points),indent=2)+'\n')
assert all(p['status']=='PASS' for p in points)
print(f'{len(points)} CLI geometry points PASS')
