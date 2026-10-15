"""Replay targeted formal mutations in actual GHDL testbenches; no RTL edits."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib,json,shutil,subprocess,tempfile,time
ROOT=Path(__file__).resolve().parents[4]
OUT=Path(__file__).resolve().parent
SRCS=['cgra_pkg','cgra_comp_pkg','uart_rx','uart_tx','pe','cgra_array','cgra_ctrl','block_pkg','block_rx','block_dot','block_ctrl','cgra_top']
paths=[f'hw/rtl/{s}.vhd' for s in SRCS]+['hw/sim/tb_blocks.vhd','hw/sim/tb_block_faults.vhd','hw/sim/gen_block_vectors.c','lib/src/block_wire.c','lib/src/block_model.c']
inputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
manifest=dict(started_utc=datetime.now(timezone.utc).isoformat(),commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT)),source_sha256=inputs,ghdl=subprocess.check_output(['ghdl','--version'],text=True),cases=[])
for name,rtl,before,after,tb,expected in [
 ('M-BLOCK-READ','block_dot', 'qa <= bank_a(index); qb <= bank_b(index);','qa <= bank_b(index); qb <= bank_b(index);','tb_block_faults','result or execution count corrupted'),
 ('M-BLOCK-CRC','block_rx','if le16(data(used-2), data(used-1)) = to_integer(crc) then','if true then','tb_blocks','block reply length at event')]:
 target=OUT/name;target.mkdir(exist_ok=False)
 for p in paths:shutil.copy2(ROOT/p,target/Path(p).name)
 source=target/(rtl+'.vhd'); text=source.read_text();assert text.count(before)==1
 shutil.copy2(source,source.with_suffix('.vhd.original'));source.write_text(text.replace(before,after))
 shutil.copy2(ROOT/'hw/build/blocks.vec',target/'blocks.vec')
 (target/'build').mkdir();shutil.copy2(target/'blocks.vec',target/'build/blocks.vec')
 commands=[['ghdl','-a','--std=08',*[s+'.vhd' for s in SRCS],tb+'.vhd'],['ghdl','-e','--std=08',tb],['ghdl','-r','--std=08',tb,'-gG_STEP_DIV=2',*(['-gG_VECTOR_FILE=blocks.vec'] if tb=='tb_blocks' else []),'--assert-level=error']]
 results=[];start=time.monotonic()
 for i,command in enumerate(commands):
  with (target/f'{i}.txt').open('w') as log:rc=subprocess.run(command,cwd=target,stdout=log,stderr=subprocess.STDOUT,timeout=60).returncode
  results.append(dict(command=command,exit_code=rc,log=f'{i}.txt'))
  if i<2:assert rc==0,results
 log=(target/'2.txt').read_text();ok=rc!=0 and expected in log
 manifest['cases'].append(dict(id=name,result='FAIL' if rc else 'BOUNDED_PASS',acceptance_passed=ok,expected_failure=expected,commands=results,duration_seconds=time.monotonic()-start,mutation_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),vector_sha256=hashlib.sha256((target/'blocks.vec').read_bytes()).hexdigest()))
 for p in target.glob('work-obj*.cf'):p.unlink()
 assert ok,log
manifest['status']='PASS';(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps([(r['id'],r['result'],r['acceptance_passed']) for r in manifest['cases']]))
