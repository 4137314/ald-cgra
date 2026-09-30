#!/usr/bin/env python3
"""Generate stable benchmark provenance from actual sources and build signatures."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
destination, cli_config, lib_config = map(Path, sys.argv[1:])
inputs = {}
for folder in ('lib', 'sw', 'scripts'):
    for path in sorted((root / folder).rglob('*')):
        rel = path.relative_to(root)
        if 'build' in rel.parts or '__pycache__' in rel.parts or not path.is_file():
            continue
        if path.suffix in ('.c', '.h', '.mk', '.py', '.cgra', '.sh'):
            inputs[str(rel)] = hashlib.sha256(path.read_bytes()).hexdigest()

def git(*args):
    try:
        return subprocess.check_output(['git', *args], cwd=root, stderr=subprocess.DEVNULL).decode().strip()
    except (OSError, subprocess.CalledProcessError):
        return None

commit = git('rev-parse', 'HEAD') or os.environ.get('CGRA_SOURCE_REVISION')
status = git('status', '--porcelain')
metadata = dict(commit=commit, dirty=None if status is None else bool(status),
                sources_sha256=hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest(),
                input_files_sha256=inputs,
                cli=json.loads(cli_config.read_text()), lib=json.loads(lib_config.read_text()))
# An array avoids the C minimum 4095-character string-literal limit. All text
# is data, never shell code; split physical lines for readable generated files.
data = json.dumps(metadata, sort_keys=True, separators=(',', ':')).encode('ascii') + b'\0'
header = '/* Generated; source hashes exclude build outputs. */\nstatic const char CGRA_BUILD_JSON[] = {\n'
header += '\n'.join(','.join(str(c) for c in data[i:i+32]) + ',' for i in range(0, len(data), 32))
header += '\n};\n'
if not destination.exists() or destination.read_text() != header:
    temporary = destination.with_suffix('.tmp')
    temporary.write_text(header)
    os.replace(temporary, destination)
