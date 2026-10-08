from pathlib import Path
import hashlib,json,shutil,gzip
ROOT=Path('/home/main/dev/git/ald-cgra')
DEST=ROOT/'doc/verification/2026-10-08/block-runs'
DEST.mkdir(parents=True,exist_ok=True)
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def archive(src,kind):
    target=DEST/kind/src.name
    if target.exists(): return
    target.mkdir(parents=True)
    index={}
    for p in sorted(src.rglob('*')):
        if not p.is_file(): continue
        name=str(p.relative_to(src))
        if kind=='formal':
            keep=p.suffix not in ('.il','.smt2') and p.name!='frontend.json'
        else:
            keep=name=='manifest.json' or name.startswith(('work/rtl/','work/con/','work/scr/')) or p.suffix in ('.rpt','.tsv') or p.name in ('vivado.log','gates.json')
        index[name]={'sha256':digest(p),'archived':keep,'bytes':p.stat().st_size}
        if keep:
            q=target/name;q.parent.mkdir(parents=True,exist_ok=True)
            if p.suffix=='.json' and p.stat().st_size>500000:
                q=q.with_name(q.name+'.gz');q.write_bytes(gzip.compress(p.read_bytes(),mtime=0))
                index[name].update(archive_path=str(q.relative_to(target)),compression='gzip',archive_sha256=digest(q))
            else: shutil.copy2(p,q)
    (target/'archive-index.json').write_text(json.dumps(dict(source=str(src),files=index,
        note='Original manifest unchanged. Large generated RTLIL/SMT2 and physical checkpoints/bitstreams omitted; hashes retained. Reproduce using saved source copies and commands. This is not a programming artifact.'),indent=2)+'\n')
for src in sorted((ROOT/'hw/build/formal').iterdir()):
    if not (src.name.startswith('block-') or (src.name.startswith(('fast-','full-','cover-')) and ('20261007' in src.name or '20261008' in src.name))): continue
    if not (src/'manifest.json').exists(): continue
    m=json.loads((src/'manifest.json').read_text())
    if len(m['tasks']) != len(m['selected_task_ids']): continue
    archive(src,'formal')
for src in sorted((ROOT/'hw/build/vivado/runs').iterdir()):
    if not ('20261007' in src.name or '20261008' in src.name): continue
    m=json.loads((src/'manifest.json').read_text())
    if 'finished_utc' not in m: continue
    archive(src,'physical')
summary=[]
for p in sorted(DEST.glob('*/*/manifest.json')):
    m=json.loads(p.read_text()); summary.append(dict(path=str(p.relative_to(DEST)),
        status=m.get('status'),mode=m.get('mode',m.get('suite')),
        results={t['id']:t['result'] for t in m.get('tasks',[])},sha256=digest(p)))
(DEST/'index.json').write_text(json.dumps(summary,indent=2)+'\n')
shutil.copy2(__file__,DEST/'archive.py')
print('archived',len(summary),'runs at',DEST)
