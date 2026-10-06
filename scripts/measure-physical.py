#!/usr/bin/env python3
"""Run comparable real Vivado STA points; never program a board.

Every shape executes once and keeps its own snapshot/manifest, including
failures. The report is physical timing evidence, not measured board speed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True, help="new output directory")
    parser.add_argument("--vivado", default="vivado")
    parser.add_argument("--board", choices=("nexys_a7", "basys3"), default="nexys_a7")
    parser.add_argument("--period", type=float, default=10)
    parser.add_argument("--step-div", type=int, default=2)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    report = {"schema_version": 1, "kind": "real-vivado-geometry-sweep", "points": [],
              "hardware_test": "NOT_RUN", "status": "RUNNING"}
    good = True
    reference_sources = None
    reference_tool = None
    for rows, cols in ((1,16), (2,8), (4,4), (8,2), (16,1)):
        point_dir = out / f"{rows}x{cols}"
        command = [sys.executable, str(ROOT/'hw/scr/artifacts.py'), 'sta',
                   '--out', str(point_dir), '--vivado', args.vivado, '--board', args.board,
                   '--period', str(args.period), '--step-div', str(args.step_div),
                   '--rows', str(rows), '--cols', str(cols)]
        with (out/f'{rows}x{cols}.txt').open('wb') as log:
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        manifests = list(point_dir.glob('runs/*/manifest.json'))
        point = dict(rows=rows, cols=cols, command=command, exit_code=result.returncode,
                     status='ERROR', manifest=None)
        if len(manifests) == 1:
            path = manifests[0]
            manifest = json.loads(path.read_text())
            point.update(status=manifest['status'], manifest=str(path.relative_to(out)),
                         manifest_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                         parameters=manifest['parameters'], metrics=manifest['metrics'])
            if reference_sources is None:
                reference_sources = manifest['source_sha256']
                reference_tool = manifest['tool']
            point['comparable_inputs'] = (manifest['source_sha256'] == reference_sources and
                                          manifest['tool'] == reference_tool)
            good &= point['comparable_inputs']
        good &= result.returncode == 0 and point['status'] == 'PASS'
        report['points'].append(point)
        (out/'summary.json').write_text(json.dumps(report, indent=2) + '\n')
        print(f'{rows}x{cols}: {point["status"]}', flush=True)
    report['status'] = 'PASS' if good else 'FAIL'
    (out/'summary.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0 if good else 1


if __name__ == '__main__':
    sys.exit(main())
