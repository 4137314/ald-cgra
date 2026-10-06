import os,shutil,subprocess,sys,tempfile
from pathlib import Path
cc=sys.argv[1]
with tempfile.TemporaryDirectory(prefix='cgra-compiler-') as temp:
    root=Path(temp)
    shutil.copy2('/src/Makefile',root/'Makefile')
    for part in ('lib','sw','scripts'):
        shutil.copytree(Path('/src')/part,root/part,ignore=shutil.ignore_patterns('build','__pycache__'))
    env=dict(os.environ,CC=cc,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    subprocess.run([cc,'--version'],check=True)
    for command in (['make','-j4','test'],['make','-j4','PROFILE=asan','test']):
        subprocess.run(command,cwd=root,env=env,check=True)
    if cc.startswith('gcc'):
        for component in ('lib','sw'):
            subprocess.run(['make','-C',component,'-f',component+'.mk','analyze'],cwd=root,env=env,check=True)
