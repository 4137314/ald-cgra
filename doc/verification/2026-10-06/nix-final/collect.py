from pathlib import Path
import hashlib,json,re,shutil,subprocess
out=Path(__file__).resolve().parent
results=json.loads((out/'results.json').read_text());meta=[]
for item in results:
    deriv=json.loads(subprocess.check_output(['nix','derivation','show',item['drvPath']],text=True))
    data=next(iter(deriv.get('derivations',deriv).values()))
    source=re.search(r'cp -r (/nix/store/\S+) source',data['env']['buildCommand']).group(1)
    kind='formal-fast' if 'formal-fast' in item['drvPath'] else 'software'
    (out/(kind+'-derivation.json')).write_text(json.dumps(deriv,indent=2)+'\n')
    stored=Path(item['outputs']['out'])
    target=out/kind;target.mkdir(exist_ok=True)
    if kind=='software':
        for p in stored.glob('*.txt'):shutil.copy2(p,target/p.name)
    else:
        for p in stored.glob('*/manifest.json'): shutil.copy2(p,target/(p.parent.name+'.json'))
    files={str(p.relative_to(Path(source))):hashlib.sha256(p.read_bytes()).hexdigest()
           for part in ('hw/formal','hw/rtl','lib','sw','scripts') for p in (Path(source)/part).rglob('*') if p.is_file()}
    meta.append(dict(check=kind,status='PASS',source=source,drv=item['drvPath'],output=str(stored),sources_sha256=files))
(out/'manifest.json').write_text(json.dumps(dict(command=['nix','build','--no-link','--json','.#checks.x86_64-linux.formal-fast','.#checks.x86_64-linux.software'],exit_code=0,checks=meta),indent=2)+'\n')
