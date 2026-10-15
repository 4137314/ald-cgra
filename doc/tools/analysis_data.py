#!/usr/bin/env python3
"""Manuscript analysis data: record (measure_analysis.c) or render.

record: run the analysis binary (built against an isolated copy of lib/),
        write data/analysis/counts.csv and provenance.json (new files; the
        earlier archived experiments are never rewritten).
render: verify provenance, re-derive every transaction and step count from
        closed forms, then write build/ana-*.csv and build/ana-macros.tex.

Conventions (Section "Methodology" of the paper):
  PE slot          one PE during one array step (R*C slots per step)
  useful PE op     a PE instruction whose result is part of the answer
                   (one MUL, MAC, ADD or fold per needed value)
  scalar ops       multiplications and additions of the scalar reference
  host ops         additions performed by the host (matvec/conv reduction)
Times are MODELS: T_link = B*t_b + J*tau, T_dev = steps*D/f_clk.
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
ddir, out = root / "doc/data/analysis", root / "doc/build"
ap = argparse.ArgumentParser()
ap.add_argument("action", choices=("record", "render"))
ap.add_argument("--binary", type=Path)
ap.add_argument("--compiler", default="cc")
ap.add_argument("--library", type=Path, help="isolated library archive used for the binary")
args = ap.parse_args()

if args.action == "record":
    raw = subprocess.run([str(args.binary.resolve())], check=True, text=True, capture_output=True).stdout
else:
    raw = (ddir / "counts.csv").read_text()
    meta = json.loads((ddir / "provenance.json").read_text())
    if hashlib.sha256(raw.encode()).hexdigest() != meta["csv_sha256"]:
        ap.error("counts.csv differs from its provenance record; rerun measure-analysis")

rows = list(csv.DictReader(io.StringIO(raw)))
I = lambda r, k: int(r[k])
B = lambda r: I(r, "tx_bytes") + I(r, "rx_bytes")
ceil = lambda p, q: -(-p // q)
shapes = {"1x16": (1, 16), "2x8": (2, 8), "4x4": (4, 4), "8x2": (8, 2), "16x1": (16, 1)}
kernels = ("vector", "dot", "scan", "matvec", "conv")

# ---------------------------------------------------------------- kernels --
K = {(r["geometry"], r["kernel"], r["protocol"]): r for r in rows if r["record"] == "kernel"}
assert len(K) == 50
info = {}
for g, (R, C) in shapes.items():
    L = min(R, C)
    band = {(i // R, j // C) for i in range(68) for j in range(64) if 0 <= i - j < 5}
    Ca, Bt = len({c for _, c in band}), len(band)
    TR, TC = ceil(32, R), ceil(32, C)
    closed = {  # transactions v2, v3; steps; configurations
        "vector": (1 + 3 * ceil(256, L), 1 + ceil(256, L), ceil(256, L) * L, 1),
        "dot": (3 + 2 * 256, 3 + 256, 256, 1),
        "scan": (1 + 4 * ceil(256, C), 1 + ceil(256, C), ceil(256, C) * C, 1),
        "matvec": (TC * (1 + (2 * R + 2) * TR), TC * (1 + R * TR), TC * TR * R, TC),
        "conv": (Ca + (2 * R + 2) * Bt, Ca + R * Bt, Bt * R, Ca),
    }
    for k, (j2, j3, steps, cfg) in closed.items():
        v2, v3 = K[g, k, "v2"], K[g, k, "v3"]
        assert (I(v2, "transactions"), I(v3, "transactions")) == (j2, j3), (g, k)
        assert I(v2, "steps") == I(v3, "steps") == steps, (g, k, I(v3, "steps"), steps)
        assert I(v2, "cfg_transactions") == I(v3, "cfg_transactions") == cfg, (g, k)
    # useful PE operations: one per needed value computed on the FPGA
    useful = {"vector": 256, "dot": 256, "scan": 256, "matvec": 32 * 32, "conv": 64 * 5}
    static = {"vector": L / (R * C), "dot": 1 / (R * C), "scan": C / (R * C),
              "matvec": C / (R * C), "conv": C / (R * C)}
    for k in kernels:
        steps = I(K[g, k, "v3"], "steps")
        info[g, k] = dict(steps=steps, slots=steps * R * C, useful=useful[k],
                          fraction=useful[k] / (steps * R * C), static=static[k])

# The 4x4 points must reproduce the archived Table VIII experiment exactly.
wire = {(r["workload"], int(r["version"])): r for r in csv.DictReader(open(root / "doc/data/wire-cost.csv"))}
for k in kernels:
    for v in (2, 3):
        a, w = K["4x4", k, f"v{v}"], wire[k, v]
        assert (I(a, "transactions"), B(a)) == (int(w["transactions"]), int(w["tx_bytes"]) + int(w["rx_bytes"])), k

# ------------------------------------------------------------------ reuse --
U = {(r["kernel"], int(r["n"]), int(r["repeat"])): r for r in rows if r["record"] == "reuse"}
for (sc, n, k), r in U.items():
    u = ceil(n, 64)
    expect = {"stream": k * (3 + n), "none": 2 + k * (2 * u + 2),
              "one": 2 + u + k * (u + 2), "both": 2 + 2 * u + 2 * k}[sc]
    assert I(r, "transactions") == expect, (sc, n, k)
    if sc == "stream":
        assert B(r) == k * (103 + 24 * n), (sc, n, k)

if args.action == "record":
    sources = ["doc/tools/measure_analysis.c", "doc/tools/analysis_data.py", "lib/include/cgra.h"]
    sources += sorted(str(p.relative_to(root)) for p in (root / "lib/src").iterdir() if p.is_file())
    meta = {
        "schema_version": 1, "experiment": "manuscript-analysis-v1",
        "recorded_at_utc": datetime.now(timezone.utc).isoformat(), "system": platform.platform(),
        "base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root)),
        "compiler": subprocess.check_output(shlex.split(args.compiler) + ["--version"], text=True).splitlines()[0],
        "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        "library_sha256": hashlib.sha256(args.library.read_bytes()).hexdigest() if args.library else None,
        "build": "make -C doc -f doc.mk measure-analysis (library compiled from lib/src into doc/build only)",
        "backend": "in-process emulator; protocol v2, v3 and resident blocks; no serial-time model",
        "counting": "after one ID; CFG, reset, compute and read-back included; blocks include preflight, HELLO, uploads; no retries",
        "verification": "independent scalar result per point; closed-form transaction/step counts checked at render",
        "hardware": "NOT_RUN",
        "sha256": {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in sources},
        "csv_sha256": hashlib.sha256(raw.encode()).hexdigest(),
    }
    ddir.mkdir(parents=True, exist_ok=True)
    if (ddir / "counts.csv").exists():
        raise SystemExit("refusing to overwrite data/analysis/counts.csv; move it aside deliberately")
    (ddir / "counts.csv").write_text(raw)
    (ddir / "provenance.json").write_text(json.dumps(meta, indent=2) + "\n")
    raise SystemExit(0)

# ----------------------------------------------------------------- render --
out.mkdir(exist_ok=True)
TB = 10 / 115200
F, D = 100e6, 2
# (1) geometry metrics: static occupancy, useful fraction, v3 bytes, steps
lines = ["shape," + ",".join(f"{k}_static,{k}_useful,{k}_bytes,{k}_steps" for k in kernels)]
for g in shapes:
    lines.append(g + "," + ",".join(
        f"{info[g,k]['static']:.6f},{info[g,k]['fraction']:.6f},{B(K[g,k,'v3'])},{info[g,k]['steps']}"
        for k in kernels))
(out / "ana-geometry.csv").write_text("\n".join(lines) + "\n")

# (2) time decomposition per kernel, 4x4, v3 and v2 (model)
lines = ["kernel,proto,serial_ms,turn1ms_ms,turn16ms_ms,device_ms,device170_ms"]
for k in kernels:
    for v in ("v2", "v3"):
        r = K["4x4", k, v]
        steps = I(r, "steps")
        lines.append(f"{k},{v},{B(r)*TB*1e3:.4f},{I(r,'transactions')*1e-3*1e3:.4f},"
                     f"{I(r,'transactions')*16e-3*1e3:.4f},{steps*D/F*1e3:.6f},{steps*D/170.677590e6*1e3:.6f}")
(out / "ana-time.csv").write_text("\n".join(lines) + "\n")
(out / "ana-time-v3.csv").write_text("\n".join([lines[0]] + [l for l in lines[1:] if ",v3," in l]) + "\n")

# (3) reuse: bytes per DOT for each scenario at n=256, and totals
lines = ["repeat,stream,none,one,both"]
for k in (1, 2, 4, 8, 16, 32, 64):
    lines.append(f"{k}," + ",".join(f"{B(U[s,256,k])/k:.3f}" for s in ("stream", "none", "one", "both")))
(out / "ana-reuse.csv").write_text("\n".join(lines) + "\n")
lines = ["n,stream,none,one,both"]
for n in (64, 256, 1024):
    lines.append(f"{n}," + ",".join(f"{B(U[s,n,8])/8:.3f}" for s in ("stream", "none", "one", "both")))
(out / "ana-reuse-n.csv").write_text("\n".join(lines) + "\n")

# (4) macros for the text (all numbers derived here, none typed by hand)
def fmt(v, nd=0):
    s = f"{v:,.{nd}f}"
    return s.replace(",", r"\,")
w = lambda k, v: wire[k, v]
WB = lambda k, v: int(w(k, v)["tx_bytes"]) + int(w(k, v)["rx_bytes"])
WJ = lambda k, v: int(w(k, v)["transactions"])
tau = {k: TB * (WB(k, 3) - WB(k, 2)) / (WJ(k, 2) - WJ(k, 3)) for k in kernels}
jr = [WJ(k, 2) / WJ(k, 3) for k in kernels]
br = [WB(k, 3) / WB(k, 2) - 1 for k in kernels]
dev_share = max(I(K["4x4", k, "v3"], "steps") * D / F /
                (B(K["4x4", k, "v3"]) * TB + I(K["4x4", k, "v3"], "steps") * D / F) for k in kernels)
s8 = {s: U[s, 256, 8] for s in ("stream", "none", "one", "both")}
m = [
    "% generated by tools/analysis_data.py from data/analysis/counts.csv and data/wire-cost.csv",
    f"\\newcommand{{\\TauDot}}{{{tau['dot']*1e6:.1f}}}",
    f"\\newcommand{{\\TauMatvec}}{{{tau['matvec']*1e6:.1f}}}",
    f"\\newcommand{{\\TauConv}}{{{tau['conv']*1e6:.1f}}}",
    f"\\newcommand{{\\JRatioMin}}{{{min(jr):.2f}}}",
    f"\\newcommand{{\\JRatioMax}}{{{max(jr):.2f}}}",
    f"\\newcommand{{\\ByteIncreaseMax}}{{{max(br)*100:.1f}}}",
    f"\\newcommand{{\\DeviceShareMax}}{{{dev_share*1e5:.1f}}}",
    f"\\newcommand{{\\ReuseStreamBytes}}{{{fmt(B(s8['stream']))}}}",
    f"\\newcommand{{\\ReuseNoneBytes}}{{{fmt(B(s8['none']))}}}",
    f"\\newcommand{{\\ReuseOneBytes}}{{{fmt(B(s8['one']))}}}",
    f"\\newcommand{{\\ReuseBothBytes}}{{{fmt(B(s8['both']))}}}",
    f"\\newcommand{{\\ReuseStreamJ}}{{{fmt(I(s8['stream'],'transactions'))}}}",
    f"\\newcommand{{\\ReuseNoneJ}}{{{I(s8['none'],'transactions')}}}",
    f"\\newcommand{{\\ReuseOneJ}}{{{I(s8['one'],'transactions')}}}",
    f"\\newcommand{{\\ReuseBothJ}}{{{I(s8['both'],'transactions')}}}",
    f"\\newcommand{{\\ReuseNoneFactor}}{{{B(s8['stream'])/B(s8['none']):.2f}}}",
    f"\\newcommand{{\\ReuseBothFactor}}{{{B(s8['stream'])/B(s8['both']):.2f}}}",
    f"\\newcommand{{\\ReuseBothPercent}}{{{(1-B(s8['both'])/B(s8['stream']))*100:.2f}}}",
    f"\\newcommand{{\\ReuseBothJFactor}}{{{I(s8['stream'],'transactions')/I(s8['both'],'transactions'):.2f}}}",
]
for k in kernels:
    m.append(f"\\expandafter\\newcommand\\csname Useful{k}\\endcsname{{{info['4x4',k]['fraction']:.4f}}}")
(out / "ana-macros.tex").write_text("\n".join(m) + "\n")
