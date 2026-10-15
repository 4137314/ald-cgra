#!/usr/bin/env python3
"""Record the scaling experiment (measure_scaling.c), or render report assets.

record: run the measurement binary, store data/scaling.{csv,json} with hashes.
render: verify provenance and closed-form counts, write build/scaling-*.csv.
Counts only: the emulator has no serial-time model.
"""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import io
import json
import math
from pathlib import Path
import platform
import shlex
import subprocess

root = Path(__file__).resolve().parents[2]
data_dir, out = root / "doc/data", root / "doc/build"
ap = argparse.ArgumentParser()
ap.add_argument("action", choices=("record", "render"))
ap.add_argument("--binary", type=Path)
ap.add_argument("--compiler", default="cc")
args = ap.parse_args()
if args.action == "record":
    if args.binary is None:
        ap.error("record requires --binary")
    raw = subprocess.run([str(args.binary.resolve())], check=True, text=True,
                         capture_output=True).stdout
else:
    raw = (data_dir / "scaling.csv").read_text()
    meta = json.loads((data_dir / "scaling.json").read_text())
    if hashlib.sha256(raw.encode()).hexdigest() != meta["csv_sha256"]:
        ap.error("scaling.csv differs from recorded provenance; rerun the measurement")

rows = list(csv.DictReader(io.StringIO(raw)))
total = lambda r: int(r["tx_bytes"]) + int(r["rx_bytes"])
tx = lambda r: int(r["transactions"])
ceil = lambda a, b: -(-a // b)

# ---- repeat experiment: closed forms independent of the counters ----------
rep = {(r["protocol"], int(r["n"]), int(r["repeat"])): r for r in rows if r["experiment"] == "repeat"}
lengths, repeats = (16, 64, 256, 1024), (1, 2, 4, 8, 16, 32, 64)
assert len(rep) == 3 * len(lengths) * len(repeats)
for n in lengths:
    for k in repeats:
        v2, v3, bl = rep["v2", n, k], rep["v3", n, k], rep["blocks", n, k]
        assert tx(v2) == k * (3 + 2 * n) and total(v2) == k * (103 + 22 * n), (n, k)
        assert tx(v3) == k * (3 + n) and total(v3) == k * (103 + 24 * n), (n, k)
        # preflight + HELLO + two banks in 64-word frames + DOT/RESULT per call
        assert tx(bl) == 2 + 2 * ceil(n, 64) + 2 * k, (n, k)
        # unescaped lower bound: two 16-bit words per element must cross once
        assert total(bl) > 4 * n

# ---- geometry experiment: transaction formulas of Section VII -------------
shapes = {"1x16": (1, 16), "2x8": (2, 8), "4x4": (4, 4), "8x2": (8, 2), "16x1": (16, 1)}
geo = {(r["geometry"], r["kernel"], r["protocol"]): r for r in rows if r["experiment"] == "geometry"}
assert len(geo) == 5 * 5 * 2
for name, (R, C) in shapes.items():
    K, Q = ceil(256, min(R, C)), ceil(256, C)
    TR = ceil(32, R)
    live = lambda t, n, C=C: any((j % 5) - 2 for j in range(t * C, min(n, (t + 1) * C)))
    TCa = sum(live(t, 32) for t in range(ceil(32, C)))
    band = {(i // R, j // C) for i in range(68) for j in range(64)
            if 0 <= i - j < 5 and live(j // C, 64)}
    Ca, B = len({c for _, c in band}), len(band)
    expect = {"vector": (1 + 3 * K, 1 + K), "dot": (3 + 512, 3 + 256),
              "scan": (1 + 4 * Q, 1 + Q),
              # zero-weight column tiles are skipped (x = b has zeros at i%5==2)
              "matvec": (TCa * (1 + (2 * R + 2) * TR), TCa * (1 + R * TR)),
              "conv": (Ca + (2 * R + 2) * B, Ca + R * B)}
    for kernel, (e2, e3) in expect.items():
        assert (tx(geo[name, kernel, "v2"]), tx(geo[name, kernel, "v3"])) == (e2, e3), (name, kernel)

# The 4x4 points must equal the archived wire experiment (Table VIII).
wire = {(r["workload"], int(r["version"])): r for r in csv.DictReader(open(data_dir / "wire-cost.csv"))}
for kernel in ("vector", "dot", "scan", "matvec", "conv"):
    for v in (2, 3):
        g, w = geo["4x4", kernel, f"v{v}"], wire[kernel, v]
        assert (tx(g), total(g)) == (int(w["transactions"]), int(w["tx_bytes"]) + int(w["rx_bytes"])), kernel

if args.action == "record":
    sources = ["doc/tools/measure_scaling.c", "doc/tools/scaling_data.py", "lib/include/cgra.h",
               "lib/src/cgra.c", "lib/src/kernels.c", "lib/src/emu.c", "lib/src/emu.h"]
    sources += sorted(str(p.relative_to(root)) for p in (root / "lib/src").glob("block*"))
    meta = {
        "schema_version": 1, "experiment": "scaling-v1",
        "recorded_at_utc": datetime.now(timezone.utc).isoformat(), "system": platform.platform(),
        "base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root)),
        "working_tree": "source hashes identify the measured code; HEAD alone is insufficient",
        "compiler_command": args.compiler,
        "compiler": subprocess.check_output(shlex.split(args.compiler) + ["--version"], text=True).splitlines()[0],
        "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        "build": "make -C lib -f lib.mk PROFILE=release static; make -C doc -f doc.mk measure-scaling",
        "backend": "in-process emulator (v2, v3, resident blocks); no serial-time model",
        "counting": "legacy: after one ID; blocks: includes preflight, HELLO and uploads; no retries",
        "verification": "independent scalar result per point; closed-form counts checked here",
        "hardware": "NOT_RUN",
        "sha256": {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in sources},
        "csv_sha256": hashlib.sha256(raw.encode()).hexdigest(),
    }
    data_dir.mkdir(exist_ok=True)
    (data_dir / "scaling.csv").write_text(raw)
    (data_dir / "scaling.json").write_text(json.dumps(meta, indent=2) + "\n")
else:
    out.mkdir(exist_ok=True)
    lines = ["repeat,v2,v3,blocks"]
    lines += [f"{k},{total(rep['v2',256,k])},{total(rep['v3',256,k])},{total(rep['blocks',256,k])}"
              for k in repeats]
    (out / "scaling-repeat.csv").write_text("\n".join(lines) + "\n")
    lines = ["n,v3r1,blocksr1,v3r64,blocksr64"]
    lines += [f"{n},{total(rep['v3',n,1])},{total(rep['blocks',n,1])},"
              f"{total(rep['v3',n,64])/64:.3f},{total(rep['blocks',n,64])/64:.3f}" for n in lengths]
    (out / "scaling-length.csv").write_text("\n".join(lines) + "\n")
    kernels = ("vector", "dot", "scan", "matvec", "conv")
    lines = ["shape," + ",".join(kernels)]
    lines += [name + "," + ",".join(str(total(geo[name, k, "v3"])) for k in kernels) for name in shapes]
    (out / "scaling-geometry.csv").write_text("\n".join(lines) + "\n")
    # Marginal resident cost per additional DOT+RESULT at n=256 (for the text).
    marginal = (total(rep["blocks", 256, 64]) - total(rep["blocks", 256, 1])) / 63
    fmt = lambda v: f"{v:,}".replace(",", r"\,")
    (out / "scaling-macros.tex").write_text(
        "% generated by tools/scaling_data.py from data/scaling.csv\n"
        f"\\newcommand{{\\ResidentMarginal}}{{{marginal:.1f}}}\n"
        f"\\newcommand{{\\ResidentOneCall}}{{{fmt(total(rep['blocks', 256, 1]))}}}\n"
        f"\\newcommand{{\\FusedOneCall}}{{{fmt(total(rep['v3', 256, 1]))}}}\n")
