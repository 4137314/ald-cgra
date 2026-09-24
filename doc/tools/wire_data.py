#!/usr/bin/env python3
"""Record current counters, or render the one CSV source into report assets."""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path
import platform
import shlex
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("action", choices=("record", "render"))
parser.add_argument("--binary", type=Path)
parser.add_argument("--compiler", default="cc")
args = parser.parse_args()
data_dir = root / "doc/data"
if args.action == "record":
    if args.binary is None:
        parser.error("record requires --binary")
    result = subprocess.run([str(args.binary.resolve())], check=True, text=True, capture_output=True)
    raw = result.stdout
else:
    raw = (data_dir / "wire-cost.csv").read_text()
rows = list(csv.DictReader(io.StringIO(raw)))
names = ("vector", "dot", "scan", "matvec", "conv")
assert len(rows) == 10
values = {}
for row in rows:
    name = row["workload"]
    numbers = {k: int(v) for k, v in row.items() if k != "workload"}
    key = (name, numbers["version"])
    assert name in names and key not in values
    assert all(v >= 0 for v in numbers.values())
    assert numbers["rows"] == numbers["cols"] == 4 and numbers["calls"] == 1
    values[key] = numbers
for name in names:
    v2, v3 = values[name, 2], values[name, 3]
    assert all(v2[k] == v3[k] for k in ("n", "m", "k", "rows", "cols", "calls"))
    dimensions = {"vector": (256,0,0), "dot": (256,0,0), "scan": (256,0,0),
                  "matvec": (32,32,0), "conv": (64,0,5)}
    assert tuple(v2[k] for k in ("n", "m", "k")) == dimensions[name], name
    # Algebraic command counts from the report, independent of the counters.
    n = v2["n"]
    chunks = (n+3)//4
    # The dense matrix has 8 column tiles and 64 nonzero blocks. For the
    # Toeplitz matrix, derive the occupied tiles from its band, not the runner.
    occupied = {(r//4, c//4) for r in range(n + v2["k"] - 1)
                for c in range(n) if 0 <= r-c < v2["k"]} if name == "conv" else set()
    active_columns = len({c for _, c in occupied})
    blocks = len(occupied)
    expected = {"vector": (1 + 3 * chunks, 1 + chunks),
                "dot": (3 + 2*n, 3+n),
                "scan": (1 + 4 * chunks, 1 + chunks),
                "matvec": (8 * (1 + 10*8), 8 * (1 + 4*8)),
                "conv": (active_columns + 10*blocks, active_columns + 4*blocks)}
    assert (v2["transactions"], v3["transactions"]) == expected[name], name
    expected_bytes = {"vector": (67 + 56*chunks, 67 + 32*chunks),
                      "dot": (103 + 22*n, 103 + 24*n),
                      "scan": (67 + 58*chunks, 67 + 32*chunks),
                      "matvec": (8*67 + 64*124, 8*67 + 64*128),
                      "conv": (active_columns*67 + blocks*124,
                               active_columns*67 + blocks*128)}
    assert tuple(v["tx_bytes"] + v["rx_bytes"] for v in (v2,v3)) == expected_bytes[name], name

if args.action == "record":
    sources = ["doc/tools/measure_wire.c", "doc/tools/wire_data.py", "lib/lib.mk",
               "lib/include/cgra.h", "lib/src/cgra.c", "lib/src/kernels.c",
               "lib/src/buffers.h", "lib/src/emu.c", "lib/src/emu.h"]
    metadata = {
        "schema_version": 1, "experiment": "current-kernels-wire-v1",
        "recorded_at_utc": datetime.now(timezone.utc).isoformat(), "system": platform.platform(),
        "base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "working_tree": "source hashes identify the measured code; HEAD alone is insufficient",
        "compiler_command": args.compiler,
        "compiler": subprocess.check_output(shlex.split(args.compiler) + ["--version"], text=True).splitlines()[0],
        "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        "build": "make -C lib -f lib.mk PROFILE=release static; make -C doc -f doc.mk measure",
        "backend": "in-process emulator, protocol v2 and v3, no serial-time model",
        "counting": "one successful call per row after ID; CFG/compute included; no retries",
        "verification": "independent scalar results and output canary before recording",
        "sha256": {p: hashlib.sha256((root/p).read_bytes()).hexdigest() for p in sources},
        "csv_sha256": hashlib.sha256(raw.encode()).hexdigest(),
    }
    data_dir.mkdir(exist_ok=True)
    (data_dir / "wire-cost.csv").write_text(raw)
    (data_dir / "wire-cost.json").write_text(json.dumps(metadata, indent=2) + "\n")
else:
    metadata = json.loads((data_dir / "wire-cost.json").read_text())
    if hashlib.sha256(raw.encode()).hexdigest() != metadata["csv_sha256"]:
        parser.error("CSV differs from recorded provenance; rerun the measurement")
    out = root / "doc/build"
    out.mkdir(exist_ok=True)
    labels = {"vector": "Vector, 256", "dot": "Dot, 256", "scan": "Scan, 256",
              "matvec": r"Matvec, $32\times32$", "conv": r"Conv, $k=5,n=64$"}
    def fmt(n):
        return f"{n:,}".replace(",", r"\,")
    table = [r"\begin{tabular}{@{}lrrrr@{}}", r"\toprule",
             r"& \multicolumn{2}{c}{Transactions} & \multicolumn{2}{c}{Bytes} \\",
             r"\cmidrule(lr){2-3}\cmidrule(lr){4-5}",
             r"Workload & v2 & v3 & v2 & v3 \\", r"\midrule"]
    ratios = ["workload,transactions,bytes"]
    for name in names:
        a, b = values[name, 2], values[name, 3]
        ba, bb = a["tx_bytes"] + a["rx_bytes"], b["tx_bytes"] + b["rx_bytes"]
        table.append(labels[name] + " & " + " & ".join(map(fmt, (a["transactions"], b["transactions"], ba, bb))) + r" \\")
        ratios.append(f'{name},{b["transactions"]/a["transactions"]:.9f},{bb/ba:.9f}')
    table.extend([r"\bottomrule", r"\end{tabular}"])
    (out / "wire-table.tex").write_text("\n".join(table) + "\n")
    (out / "wire-ratios.csv").write_text("\n".join(ratios) + "\n")
print(f"{args.action}: 10 validated rows, source doc/data/wire-cost.csv")
