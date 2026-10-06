"""Repeat only the reproduced byte-stream fixture on a copied source tree."""
from pathlib import Path
import shutil, subprocess, tempfile
with tempfile.TemporaryDirectory(prefix='cgra-fragment-fixed-') as temp:
    root=Path(temp)
    for part in ('lib','scripts'):
        shutil.copytree(Path('/src')/part, root/part,
                        ignore=shutil.ignore_patterns('build','__pycache__'))
    p=root/'lib/test/test_transport.c'
    source=p.read_text()
    source=source[:source.index('int main(int argc, char **argv)')]+'''int main(void) {
        setvbuf(stdout, NULL, _IOLBF, 0);
        alarm(20);
        for (int i = 0; i < 100; i++) test_partial_write_and_retry_budget();
        return 0;
    }\n'''
    p.write_text(source)
    subprocess.run(['make','-C',str(root/'lib'),'-f','lib.mk','CC=gcc-13',
                    'build/test_transport'],check=True)
    subprocess.run([str(root/'lib/build/test_transport')],check=True)
