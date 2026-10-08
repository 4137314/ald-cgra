#!/usr/bin/env python3
"""Public API wire counts per geometry; does not measure FPGA performance."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('out', type=Path, help='new directory, never overwrite previous evidence')
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    cc = shlex.split(os.environ.get('CC', 'cc'))
    commands = [['make', '-C', 'lib', '-f', 'lib.mk', 'static']]
    subprocess.run(commands[0], cwd=ROOT, check=True)
    hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
              for base in ('lib/include', 'lib/src') for p in (ROOT/base).glob('*') if p.is_file()}
    for name in ('scripts/bench_blocks.c', 'scripts/measure-blocks.py'):
        hashes[name] = hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
    metadata = dict(schema_version=1, scope='public API wire counts; no wall time, FPGA timing or speedup',
        commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT)),
        platform=platform.platform(), compiler=subprocess.check_output([*cc,'--version'],text=True),
        build=json.loads((ROOT/'lib/build/.build-config.json').read_text()), source_sha256=hashes,
        commands=commands, workload='a[i]=i%7-3, b[i]=i%5-2; signed int64 scalar oracle reduced modulo 65536',
        backend='clockless in-process model', precision_bits=16, protocol_legacy=3, block_revision=1,
        accounting='legacy excludes initial ID, includes CFG each call; blocks includes preflight/HELLO/upload',
        bitstream=None, hardware='NOT_RUN', result='ERROR')
    try:
        with tempfile.TemporaryDirectory(prefix='cgra-block-measure-') as tmp:
            binary=str(Path(tmp)/'measure')
            command=[*cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Ilib/include',
                     'scripts/bench_blocks.c','lib/build/libcgra.a','-o',binary]
            commands.append(command);subprocess.run(command,cwd=ROOT,check=True)
            for geometry in ('1x16','2x8','4x4','8x2','16x1'):
                command=[binary,geometry];commands.append(command)
                p=subprocess.run(command,cwd=ROOT,text=True,capture_output=True)
                (args.out/(geometry+'.log')).write_text(p.stdout+p.stderr)
                point=dict(metadata, commands=list(commands), result='FAIL', exit_code=p.returncode)
                if p.returncode==0: point.update(measurement=json.loads(p.stdout),result='PASS')
                (args.out/(geometry+'.json')).write_text(json.dumps(point,indent=2)+'\n')
                p.check_returncode()
        metadata['result']='PASS'
    finally:
        (args.out/'manifest.json').write_text(json.dumps(metadata,indent=2)+'\n')

if __name__=='__main__': main()
