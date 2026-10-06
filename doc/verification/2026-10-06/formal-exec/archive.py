"""Retain reproducible inputs, logs and witnesses; large generated models stay in build/."""
import hashlib,json,shutil
from pathlib import Path
out=Path('doc/verification/2026-10-06/formal-exec/archive')
out.mkdir(exist_ok=False)
root=Path('hw/build/formal')
summary=[]
for run in sorted(root.glob('*-20261006T*')):
    manifest=run/'manifest.json'
    if not manifest.exists(): continue
    data=json.loads(manifest.read_text())
    expected=data['selected_task_ids']
    if {t['id'] for t in data['tasks']} != set(expected):
        raise RuntimeError(f'incomplete run: {run}')
    summary.append(dict(run=run.name,suite=data['suite'],complete_suite=data['complete_suite'],
        accepted=all(t['acceptance_passed'] for t in data['tasks']),
        tasks=[dict(id=t['id'],result=t['result'],acceptance_passed=t['acceptance_passed']) for t in data['tasks']]))
    for path in run.rglob('*'):
        if not path.is_file(): continue
        rel=path.relative_to(run)
        # Root provenance and per-task inputs/results, plus exact diagnostics.
        keep=(len(rel.parts)==1 or
              (len(rel.parts)==2 and path.name not in ('frontend.json','dut.il')) or
              path.name in ('status','config.sby','logfile.txt','logfile_basecase.txt','logfile_induction.txt') or
              (path.name.startswith('trace') and path.suffix in ('.yw','.vcd')))
        if keep:
            target=out/run.name/rel; target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(path,target)
index={str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.rglob('*')) if p.is_file()}
(out.parent/'index.json').write_text(json.dumps(dict(runs=summary,files_sha256=index,
    omitted='generated RTLIL/SMT/netlist models and redundant SBY source copies; hashes/commands retained in result.json'),indent=2)+'\n')
print(f'Archived {len(summary)} executed runs, {len(index)} files')
