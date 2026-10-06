"""Replay existing protocol regression against real RTL and one temporary mutation."""
import hashlib,json,shutil,subprocess,tempfile,time
from pathlib import Path
root=Path.cwd()
out=root/'doc/verification/2026-10-06/exec-ghdl'
paths=['hw/rtl/cgra_pkg.vhd','hw/rtl/cgra_comp_pkg.vhd','hw/rtl/pe.vhd',
       'hw/rtl/cgra_array.vhd','hw/rtl/cgra_ctrl.vhd','hw/sim/tb_protocol_diff.vhd',
       'hw/sim/gen_protocol_vectors.c','lib/src/emu.c','lib/src/emu.h',
       'lib/src/bitvec.h','lib/include/cgra.h']
report=dict(commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
    dirty=bool(subprocess.check_output(['git','status','--porcelain'])),
    sources_sha256={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},
    tools={name:subprocess.check_output([name,'--version'],text=True) for name in ('ghdl','cc')},
    runs=[])
with tempfile.TemporaryDirectory(prefix='cgra-exec-replay-') as temp:
    base=Path(temp)
    gen=base/'gen'
    command=['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Ilib/include','-Ilib/src',
             'hw/sim/gen_protocol_vectors.c','lib/src/emu.c','-o',str(gen)]
    subprocess.run(command,check=True)
    vec=base/'protocol.vec'
    subprocess.run([str(gen),str(vec)],check=True)
    report['vector_sha256']=hashlib.sha256(vec.read_bytes()).hexdigest()
    shutil.copy2(vec,out/'protocol.vec')
    for mutation in (False,True):
        work=base/('mutated' if mutation else 'production');work.mkdir()
        sources=[]
        for p in paths:
            if p.endswith('.vhd'):
                target=work/Path(p).name;shutil.copy2(root/p,target);sources.append(target.name)
        if mutation:
            p=work/'cgra_ctrl.vhd'
            source=p.read_text()
            before='elsif byte_idx = 0 then\n              tx_byte   <= std_logic_vector(rd_word(7 downto 0));'
            after='elsif byte_idx = 0 then\n              tx_byte   <= std_logic_vector(rd_word(15 downto 8));'
            assert source.count(before)==1
            p.write_text(source.replace(before,after))
            report['mutation']=dict(before=before,after=after,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
        commands=[['ghdl','-a','--std=08',*sources],['ghdl','-e','--std=08','tb_protocol_diff'],
                  ['ghdl','-r','--std=08','tb_protocol_diff',f'-gG_VECTOR_FILE={vec}','--assert-level=error']]
        start=time.monotonic()
        with (out/(work.name+'.txt')).open('w') as log:
            for command in commands:
                rc=subprocess.run(command,cwd=work,stdout=log,stderr=subprocess.STDOUT).returncode
                if rc:break
        report['runs'].append(dict(mutation=mutation,commands=commands,exit_code=rc,
            status='PASS' if rc==0 else 'FAIL',acceptance_passed=(rc!=0 if mutation else rc==0),
            duration_seconds=time.monotonic()-start,log=work.name+'.txt'))
(out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
assert all(r['acceptance_passed'] for r in report['runs'])
print('PASS: production replay; expected FAIL: byte-swap mutation')
