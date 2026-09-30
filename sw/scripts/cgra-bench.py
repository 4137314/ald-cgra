#!/usr/bin/env python3
"""Measure each sweep point once and preserve every complete report, even failures."""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    here = Path(__file__).resolve().parent
    binary = Path(os.environ.get('CGRA', str(here / '../build/cgra'))).resolve()
    device = os.environ.get('DEV', 'sim')
    output = Path(os.environ.get('OUT', str(here / '../build/benchmark.json'))).resolve()
    env = dict(os.environ)
    stdlib = env.get('STDLIB', str(here / '../config/stdlib'))
    env['CGRA_PATH'] = stdlib + (':' + env['CGRA_PATH'] if env.get('CGRA_PATH') else '')
    try:
        sizes = [int(x) for x in env.get('SIZES', '256 1024 4096').split()]
        repeat = int(env.get('REPEAT', '50'))
        if not sizes or any(n < 1 or n > 4096 for n in sizes) or not 1 <= repeat <= 2147483647:
            raise ValueError('invalid SIZES or REPEAT')
        binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
        output.parent.mkdir(parents=True, exist_ok=True)
    except (OSError, ValueError) as error:
        print(f'cgra-bench: {error}', file=sys.stderr)
        return 1
    if device == 'auto':
        try:
            if subprocess.run([str(binary), 'probe'], env=env).returncode:
                return 1
        except OSError as error:
            print(f'cgra-bench: probe failed: {error}', file=sys.stderr)
            return 1
    # An isolated run directory also retains stderr and incomplete stdout when
    # the CLI fails before producing a report. Never confuse these with old data.
    run = Path(tempfile.mkdtemp(prefix=output.name + '.run-', dir=output.parent))
    summary = dict(schema_version=2, kind='cgra-benchmark-sweep',
                   started=datetime.now(timezone.utc).isoformat(),
                   binary=str(binary), binary_sha256=binary_hash, points=[], ok=True)
    reports = 0
    for index, size in enumerate(sizes):
        command = [str(binary), 'benchall', '-d', device, '--size', str(size),
                   '--repeat', str(repeat), '--json']
        point = dict(size=size, repeat=repeat, command=command)
        stdout = run / f'{index:03d}-size-{size}.json'
        stderr = run / f'{index:03d}-size-{size}.stderr.txt'
        try:
            result = subprocess.run(command, env=env, capture_output=True)
            stdout.write_bytes(result.stdout)
            stderr.write_bytes(result.stderr)
            point.update(exit_code=result.returncode,
                         stderr=str(stderr.relative_to(output.parent)))
            data = json.loads(result.stdout, parse_constant=lambda x: (_ for _ in ()).throw(ValueError(x)))
            if not isinstance(data, dict) or data.get('size') != size or data.get('repeat') != repeat or 'results' not in data:
                raise ValueError('incomplete or mismatched benchmark report')
            point.update(report=str(stdout.relative_to(output.parent)),
                         sha256=hashlib.sha256(result.stdout).hexdigest(),
                         status='PASS' if result.returncode == 0 and data.get('ok') is True else 'FAIL')
            reports += 1
            print(f"size {size}: {point['status']}; {stdout}")
        except (OSError, ValueError) as error:
            point.update(status='ERROR', error=str(error),
                         incomplete_stdout=str(stdout.relative_to(output.parent)))
            print(f'cgra-bench: size {size}: {error}', file=sys.stderr)
        if point['status'] != 'PASS':
            summary['ok'] = False
        summary['points'].append(point)
    try:
        if hashlib.sha256(binary.read_bytes()).hexdigest() != binary_hash:
            summary.update(ok=False, error='binary changed during sweep')
    except OSError as error:
        summary.update(ok=False, error=f'binary unavailable after sweep: {error}')
    summary['reports'] = reports
    # Save failed attempts too. Publish only when at least one new complete
    # point exists; otherwise the previous public artifact remains untouched.
    attempt = run / 'sweep.json'
    attempt.write_text(json.dumps(summary, indent=2) + '\n')
    if reports:
        with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as staged:
            staged.write(attempt.read_bytes())
        os.replace(staged.name, output)
    else:
        print(f'cgra-bench: no new report; previous artifact preserved; diagnostics: {run}', file=sys.stderr)
    return 0 if summary['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
