#!/usr/bin/env python3
"""Measure each API workload/geometry point separately, against scalar oracles."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shlex
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("output", type=Path, help="new directory; existing evidence is never replaced")
    ap.add_argument("--baseline", action="store_true", help="check the historical review's byte counts")
    ap.add_argument("--samples", type=int, default=5)
    args = ap.parse_args()
    if sys.platform != "linux":
        ap.error("this experiment currently requires GNU ld wrappers and Linux RSS units")
    args.output.mkdir(parents=True, exist_ok=False)
    compiler = shlex.split(os.environ.get("CC", "cc"))
    subprocess.run(["make", "-C", "lib", "-f", "lib.mk", "static"], cwd=ROOT, check=True)
    binary = ROOT / "build" / ("geometry-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ"))
    binary.parent.mkdir(exist_ok=True)
    command = [*compiler, "-std=c11", "-O3", "-Wall", "-Wextra", "-Werror",
               "-Ilib/include", "-Ilib/src", "scripts/bench_geometry.c", "lib/build/libcgra.a",
               "-Wl,--wrap=calloc,--wrap=malloc,--wrap=free,--wrap=emu_feed", "-o", str(binary)]
    subprocess.run(command, cwd=ROOT, check=True)
    paths = [*sorted((ROOT / "lib/src").glob("*.[ch]")), ROOT / "lib/include/cgra.h",
             ROOT / "scripts/bench_geometry.c", Path(__file__), ROOT / "lib/lib.mk",
             ROOT / "scripts/build-config.mk", ROOT / "scripts/build-config.py"]
    sources = {str(p.relative_to(ROOT)): digest(p) for p in paths}
    provenance = dict(commit=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
                      dirty=bool(subprocess.check_output(["git","status","--porcelain"],cwd=ROOT)),
                      sources_sha256=sources, platform=platform.platform(), backend="clockless-emulator",
                      compiler=subprocess.check_output([*compiler,"--version"],text=True).splitlines()[0],
                      compile_command=command, binary_sha256=digest(binary),
                      library_sha256=digest(ROOT / "lib/build/libcgra.a"),
                      library_build=json.loads((ROOT / "lib/build/.build-config.json").read_text()),
                      workload="review-positive-v1; conv h=[1,-2,3,-4,5]",
                      timing_scope="public API call, includes host preparation/configuration/communication/model",
                      memory_scope="temporary project malloc/calloc requested and live bytes; excludes caller arrays, libc internals and stack; RSS is whole-process Linux peak including warmup",
                      identification="one initial ID excluded; per-call CFG included", bitstream=None)
    manifest = dict(schema_version=1, provenance=provenance, points=[], failures=[])
    reference = {"1x16": [11331,1251,1251], "2x8": [4163,1475,1542],
                 "4x4": [2115,2115,2316], "8x2": [4163,4163,4632], "16x1": [11331,11331,12336]}
    for shape in reference:
        for version in (2,3):
            for kind,n in (("add",256),("scan",256),("matvec",16),("conv",256),("conv",4096)):
                name = f"{shape}-v{version}-{kind}-{n}"
                cmd = [str(binary), f"sim:{shape}" + (":v2" if version==2 else ""), kind, str(n), str(args.samples), "1"]
                p = subprocess.run(cmd,cwd=ROOT,text=True,capture_output=True)
                (args.output/(name+".stderr.txt")).write_text(p.stderr)
                try:
                    point = json.loads(p.stdout)
                    if p.returncode or not point.get("verified"): raise ValueError(f"exit {p.returncode}")
                    values = point["samples"]
                    for key in ("transactions","tx_bytes","rx_bytes","logical_steps","temporary_heap_peak_live"):
                        if len({s[key] for s in values}) != 1: raise ValueError(f"unstable per-call {key}")
                    point["statistics"] = {key: {"median":statistics.median(s[key] for s in values),
                          "p95":sorted(s[key] for s in values)[math.ceil(.95*len(values))-1]}
                          for key in ("wall_ns","process_cpu_ns")}
                    point["useful_arithmetic_ops_per_call"] = {"add":n,"scan":n-1,"matvec":2*n*n-n,
                                                               "conv":2*n*5-(n+5-1)}[kind]
                    if args.baseline and version==3 and kind in ("add","scan","matvec"):
                        expected = reference[shape][("add","scan","matvec").index(kind)]
                        actual = values[0]["tx_bytes"]+values[0]["rx_bytes"]
                        if actual != expected: raise ValueError(f"historical bytes: {actual} != {expected}")
                    point.update(provenance=provenance, command=cmd, exit_code=p.returncode)
                    (args.output/(name+".json")).write_text(json.dumps(point,indent=2)+"\n")
                    manifest["points"].append(name+".json")
                    print(name,point["statistics"]["wall_ns"]["median"],values[0]["temporary_heap_peak_live"],flush=True)
                except (ValueError, KeyError) as e:
                    (args.output/(name+".stdout.txt")).write_text(p.stdout)
                    manifest["failures"].append(dict(point=name,error=str(e),command=cmd,exit_code=p.returncode))
                (args.output/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    if sources != {str(p.relative_to(ROOT)):digest(p) for p in paths}:
        manifest["failures"].append(dict(error="sources changed during experiment"))
        (args.output/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    return bool(manifest["failures"])


if __name__ == "__main__":
    sys.exit(main())
