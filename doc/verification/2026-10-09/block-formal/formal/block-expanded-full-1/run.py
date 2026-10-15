#!/usr/bin/env python3
"""Run properties on GHDL-imported production VHDL; retain every run separately."""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
FORMAL = ROOT / "hw/formal"


def dependencies():
    versions = {}
    for tool, args in (("ghdl", ["--version"]), ("yosys", ["-V"]),
                       ("sby", ["--version"]), ("yices-smt2", ["--version"]), ("z3", ["--version"])):
        if not shutil.which(tool):
            raise RuntimeError(f"missing {tool}; run: nix develop .#formal -c make formal-fast")
        p = subprocess.run([tool, *args], text=True, capture_output=True, check=True)
        versions[tool] = {"path": str(Path(shutil.which(tool)).resolve()),
                          "reported_version": p.stdout.strip()}
    p = subprocess.run(["yosys", "-Q", "-T", "-p", "help ghdl"], text=True, capture_output=True)
    if p.returncode or "No such command" in p.stdout:
        raise RuntimeError("Yosys GHDL plugin unavailable; use nix develop .#formal")
    versions["pinned_packages"] = json.loads((FORMAL / "toolchain.json").read_text())
    return versions


def tasks(suite):
    result = [dict(id="F-PE-SMOKE", top="pe_smoke", kind="pe", mode="prove", depth=8,
                   assertions=4, assumptions=1, covers=1),
              dict(id="F-PE-NEGATIVE", top="pe_smoke", kind="pe", mode="bmc", depth=8,
                   negative=True, assertions=5, assumptions=1, covers=1)]
    if suite != "smoke":
        result.append(dict(id="F-PE-ALU-MUX", top="pe_contract", kind="pe", mode="prove",
                           depth=4, gates=True, assertions=1, assumptions=1, covers=20))
        for div in (1, 2, 3, 4):
            result.append(dict(id=f"F-CTRL-1x1-DIV{div}", top="cgra_ctrl", kind="ctrl",
                               mode="prove", depth=16, parameters=dict(G_ROWS=1, G_COLS=1,
                               G_STEP_DIV=div, G_TIMEOUT_CYCLES=3),
                               assertions=77 if div == 1 else 78, assumptions=0, covers=9))
        for rows, cols in ((1, 1), (2, 3)):
            result.append(dict(id=f"F-ARRAY-{rows}x{cols}", top="array_contract", kind="array",
                               mode="prove", depth=4, gates=True,
                               parameters=dict(G_ROWS=rows, G_COLS=cols),
                               assertions=rows*cols, assumptions=1 if rows*cols == 1 else 1+rows*cols,
                               covers=5, composition=rows*cols != 1))
        for kind, count in (("tx", 21), ("rx", 31)):
            result.append(dict(id=f"F-UART-{kind.upper()}-C3", top=f"uart_{kind}", kind=kind,
                               mode="prove", depth=16, parameters=dict(CLKS_PER_BIT=3),
                               assertions=count, assumptions=0, covers=3 if kind == "tx" else 4))
        for div in (1, 2, 3, 4):
            result.append(dict(id=f"F-BLOCK-DOT-DIV{div}", top="block_dot", kind="block_dot",
                               mode="prove", depth=16, parameters=dict(G_STEP_DIV=div),
                               assertions=23, assumptions=1, covers=3))
        result.append(dict(id="F-BLOCK-MEMORY-DIV2", top="block_dot", kind="block_dot",
                           mode="prove", depth=16, parameters=dict(G_STEP_DIV=2),
                           memory=True, assertions=30, assumptions=1, covers=6))
        result.append(dict(id="F-BLOCK-RX-T3", top="block_rx", kind="block_rx", mode="prove",
                           depth=8, parameters=dict(G_TIMEOUT_CYCLES=3), assertions=22, assumptions=0, covers=4))
        result.append(dict(id="F-BLOCK-CTRL-DIV2", top="block_ctrl", kind="block_ctrl", mode="prove",
                           depth=6, parameters=dict(G_STEP_DIV=2, G_TIMEOUT_CYCLES=3),
                           assertions=82, assumptions=1, covers=12))
    if suite == "full":
        controller = next(t for t in result if t['id'] == 'F-BLOCK-CTRL-DIV2')
        for div in (1, 3, 4):
            result.append(dict(controller, id=f"F-BLOCK-CTRL-DIV{div}",
                               parameters=dict(G_STEP_DIV=div, G_TIMEOUT_CYCLES=3)))
        result.append(dict(controller, id="F-BLOCK-CTRL-T100000000", parameters=dict(G_STEP_DIV=2, G_TIMEOUT_CYCLES=100_000_000)))
        receiver = next(t for t in result if t['id'] == 'F-BLOCK-RX-T3')
        result.append(dict(receiver, id="F-BLOCK-RX-T100000000", parameters=dict(G_TIMEOUT_CYCLES=100_000_000)))
        result.append(dict(receiver, id="M-BLOCK-CRC", mode="bmc", depth=20, negative=True,
                           mutation="block-bypass-crc"))
        memory = next(t for t in result if t['id'] == 'F-BLOCK-MEMORY-DIV2')
        for div in (1, 3, 4):
            result.append(dict(memory, id=f"F-BLOCK-MEMORY-DIV{div}", parameters=dict(G_STEP_DIV=div)))
        result.append(dict(memory, id="M-BLOCK-READ", mode="bmc", depth=16, negative=True,
                           mutation="block-read-bank"))
        for rows, cols in ((1, 2), (2, 1), (3, 2), (4, 4), (1, 16), (16, 1)):
            result.append(dict(id=f"F-ARRAY-{rows}x{cols}", top="array_contract", kind="array",
                               mode="prove", depth=4, gates=True,
                               parameters=dict(G_ROWS=rows, G_COLS=cols),
                               assertions=rows*cols, assumptions=1 if rows*cols == 1 else 1+rows*cols,
                               covers=5, composition=rows*cols != 1))
        for rows, cols in ((2, 3), (3, 2), (4, 4), (1, 16), (16, 1)):
            result.append(dict(id=f"F-CTRL-{rows}x{cols}-DIV2", top="cgra_ctrl", kind="ctrl",
                               mode="prove", depth=16, parameters=dict(G_ROWS=rows, G_COLS=cols,
                               G_STEP_DIV=2, G_TIMEOUT_CYCLES=100_000_000),
                               assertions=78, assumptions=0, covers=10))
        for kind, count in (("tx", 21), ("rx", 31)):
            result.append(dict(id=f"F-UART-{kind.upper()}-C868", top=f"uart_{kind}", kind=kind,
                               mode="prove", depth=16, parameters=dict(CLKS_PER_BIT=868),
                               assertions=count, assumptions=0, covers=3 if kind == "tx" else 4))
        block = next(t for t in result if t['id'] == 'F-BLOCK-DOT-DIV2')
        result.append(dict(block, id="M-BLOCK-EARLY", mode="bmc", depth=12, negative=True,
                           mutation="block-early-capture"))
        result.append(dict(result[2], id="M-PE-ADD", mode="bmc", negative=True, mutation="pe-add-to-sub"))
        result.append(dict(result[4], id="M-CTRL-STEP", mode="bmc", negative=True, mutation="ctrl-drop-step"))
        ctrl = next(t for t in result if t['id'] == 'F-CTRL-1x1-DIV2')
        result.append(dict(ctrl, id="M-EXEC-BYTE", mode="bmc", depth=40, negative=True,
                           mutation="exec-byte-swap"))
        array = next(t for t in result if t['id'] == 'F-ARRAY-2x3')
        result.append(dict(array, id="M-ARRAY-NORTH", mode="bmc", depth=6, negative=True,
                           mutation="array-north-self"))
        for kind, mutation in (("tx", "tx-msb-first"), ("rx", "rx-accept-bad-stop")):
            original = next(t for t in result if t["id"] == f"F-UART-{kind.upper()}-C3")
            result.append(dict(original, id=f"M-UART-{kind.upper()}", mode="bmc", depth=40,
                               negative=True, mutation=mutation))
    if suite == "cover":
        result.append(dict(id="F-CTRL-2x3-DIV2", top="cgra_ctrl", kind="ctrl",
                           mode="prove", depth=16, parameters=dict(G_ROWS=2, G_COLS=3,
                           G_STEP_DIV=2, G_TIMEOUT_CYCLES=3),
                           assertions=78, assumptions=6, covers=4, exec_witness=True))
        result = [dict(t, **(dict(block_witness=True, assumptions=6, covers=5, skip=160, solver="z3", engine_options=["--unroll"]) if t["kind"] == "block_ctrl" else {}),
                       id=t["id"] + "-COVER", mode="cover", depth=200 if t["kind"] == "block_ctrl" else 80 if t["kind"] in ("tx", "rx") else
                       96 if t["kind"] == "ctrl" and t["parameters"]["G_ROWS"] * t["parameters"]["G_COLS"] > 1 else 40)
                  for t in result if not t.get("negative")]
    return result


def run_task(task, directory, budget):
    directory.mkdir()
    start = time.monotonic()
    result = dict(task, result="ERROR", acceptance_passed=False, engine=" ".join(["smtbmc", *task.get("engine_options", []), task.get("solver", "yices")]),
                  commands=[], logs=[], clock="rising_edge(clk)",
                  initial_state="production RTL initial values",
                  environment=("initial reset; subsequent inputs unconstrained" if task["kind"] in ("pe", "array")
                               else "all inputs unconstrained; no transmitter fairness assumed"))
    paths = ["hw/rtl/cgra_pkg.vhd"]
    if task["kind"] == "pe":
        paths += ["hw/rtl/pe.vhd", "hw/formal/pe_ports.vhd", f"hw/formal/{task['top']}.sv"]
        if task['top'] == 'pe_contract': paths += ["hw/formal/pe_spec.sv"]
        entity = "pe_ports"
    elif task["kind"] == "array":
        paths += ["hw/rtl/cgra_comp_pkg.vhd", "hw/rtl/pe.vhd", "hw/rtl/cgra_array.vhd",
                  "hw/formal/array_ports.vhd", "hw/formal/pe_spec.sv", "hw/formal/array_contract.sv"]
        if task.get("composition"): paths += ["hw/formal/pe_assume.sv"]
        entity = "array_ports"
    elif task["kind"] == "block_ctrl":
        entity = "block_ctrl"
        if task.get("block_witness"): paths += ["hw/formal/block_ctrl_cover.psl"]
        paths += ["hw/rtl/block_pkg.vhd", "hw/rtl/block_rx.vhd", "hw/rtl/block_dot.vhd", "hw/rtl/block_ctrl.vhd",
                  "hw/formal/block_rx_contract.psl", "hw/formal/block_dot_contract.psl", "hw/formal/block_ctrl_contract.psl"]
    elif task["kind"] == "block_rx":
        entity = "block_rx"
        paths += ["hw/rtl/block_pkg.vhd", "hw/rtl/block_rx.vhd", "hw/formal/block_rx_contract.psl"]
    elif task["kind"] == "block_dot":
        entity = "block_dot"
        paths += ["hw/rtl/block_pkg.vhd", "hw/rtl/block_dot.vhd", "hw/formal/block_dot_contract.psl"]
        if task.get("memory"): paths += ["hw/formal/block_memory.psl"]
    elif task["kind"] in ("tx", "rx"):
        entity = task["top"]
        paths += [f"hw/rtl/{entity}.vhd", f"hw/formal/{entity}_contract.psl"]
    else:
        paths += ["hw/rtl/cgra_ctrl.vhd", "hw/formal/ctrl_contract.psl", "hw/formal/exec_contract.psl"]
        if task.get("exec_witness"): paths += ["hw/formal/exec_cover.psl"]
        entity = "cgra_ctrl"
    result["source_sha256"] = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in paths}
    # Copy exact input bytes, so results remain reproducible after source edits.
    for p in paths:
        shutil.copy2(ROOT / p, directory / Path(p).name)
    if task.get("mutation"):
        filename, before, after = {
            "block-bypass-crc": ("block_rx.vhd", "if le16(data(used-2), data(used-1)) = to_integer(crc) then",
                                 "if true then"),
            "block-read-bank": ("block_dot.vhd", "qa <= bank_a(index); qb <= bank_b(index);",
                                "qa <= bank_b(index); qb <= bank_b(index);"),
            "block-early-capture": ("block_dot.vhd",
                "step <= '1' when state = S_CAPTURE else '0';",
                "step <= '1' when state = S_CAPTURE or state = S_READ else '0';"),
            "pe-add-to-sub": ("pe.vhd", "when OP_ADD   => res := a + b;", "when OP_ADD   => res := a - b;"),
            "ctrl-drop-step": ("cgra_ctrl.vhd", "step <= '1';", "step <= '0';"),
            "exec-byte-swap": ("cgra_ctrl.vhd",
                "elsif byte_idx = 0 then\n              tx_byte   <= std_logic_vector(rd_word(7 downto 0));",
                "elsif byte_idx = 0 then\n              tx_byte   <= std_logic_vector(rd_word(15 downto 8));"),
            "array-north-self": ("cgra_array.vhd", "q((r - 1) * G_COLS + c)", "q(r * G_COLS + c)"),
            "tx-msb-first": ("uart_tx.vhd", "tx_serial <= data_r(bit_idx);", "tx_serial <= data_r(7 - bit_idx);"),
            "rx-accept-bad-stop": ("uart_rx.vhd", "if rx_s2 = '1' then\n                rx_data",
                                    "if rx_s2 = '0' then\n                rx_data")
        }[task["mutation"]]
        target = directory / filename
        shutil.copy2(target, directory / (filename + ".original"))
        contents = target.read_text()
        if contents.count(before) != 1:
            raise RuntimeError("mutation location no longer unique; update the targeted mutation")
        target.write_text(contents.replace(before, after))
        result["mutated_source_sha256"] = {filename: hashlib.sha256(target.read_bytes()).hexdigest()}

    def command(argv, logfile):
        result["commands"].append(argv)
        result["logs"].append(logfile)
        with (directory / logfile).open("w") as log:
            return subprocess.run(argv, cwd=directory, stdout=log, stderr=subprocess.STDOUT,
                                  timeout=budget + 30).returncode

    try:
        vhdl = " ".join(Path(p).name for p in paths if not p.endswith(".sv") and Path(p).name not in ("exec_contract.psl", "exec_cover.psl", "block_memory.psl", "block_ctrl_cover.psl"))
        generics = " ".join(f"-g{k}={v}" for k, v in task.get("parameters", {}).items())
        # GHDL's analysis/elaboration syntax requires options BEFORE sources.
        # A misplaced -g after sources is silently ignored by this frontend.
        if task["kind"] in ("ctrl", "tx", "rx", "block_dot", "block_rx", "block_ctrl"):
            # Fail frontend elaboration if *any* requested parameter is ignored,
            # even when two configurations happen to have equal port widths.
            checks = " and ".join(f"{k} = {v}" for k, v in task["parameters"].items())
            # This GHDL version binds only the last vunit for an architecture.
            # Add the instance check to the SAME vunit, and retain both the
            # original and effective property bytes with independent hashes.
            property_name = "ctrl_contract" if task["kind"] == "ctrl" else entity + "_contract"
            property_file = directory / (property_name + ".psl")
            original = property_file.read_text()
            shutil.copy2(property_file, directory / (property_name + ".source.psl"))
            if task["kind"] == "ctrl":
                fragment = (directory / "exec_contract.psl").read_text()
                if task["parameters"]["G_ROWS"] * task["parameters"]["G_COLS"] > 1:
                    fragment += ("\n  C_EXEC_SPARSE: cover {f_exec and state = S_IDLE and "
                                 "f_mask(0) = '1' and f_mask(G_ROWS * G_COLS - 1) = '1' and "
                                 "f_length < 2 * G_ROWS * G_COLS};\n")
                original = original.rstrip().removesuffix("}") + fragment + "\n}\n"
                if task.get("exec_witness"):
                    original = re.sub(r"  C_CTRL_\w+: cover \{[^}]*\};\n", "", original)
                    witness = (directory / "exec_cover.psl").read_text()
                    original = original.rstrip().removesuffix("}") + witness + "\n}\n"
                    result["environment"] = ("reachability-only stimulus: no reset; EXEC-only; prompt RX; "
                        "one step; ideal prompt TX handshake; mask, payload and checksum symbolic")
                    result["scope"] = "EXEC covers only; these assumptions do not apply to any proof task"
            if task.get("memory"):
                original = original.rstrip().removesuffix("}") + (directory / "block_memory.psl").read_text() + "\n}\n"
            if task.get("block_witness"):
                original = original.rstrip().removesuffix("}") + (directory / "block_ctrl_cover.psl").read_text() + "\n}\n"
            parameter_check = f"\n  F_SELECTED: assert always ({checks});\n"
            effective = original.replace("  default clock is rising_edge(clk);",
                                         "  default clock is rising_edge(clk);" + parameter_check)
            property_file.write_text(effective)
            result["parameter_check_sha256"] = hashlib.sha256(parameter_check.encode()).hexdigest()
            result["effective_properties_sha256"] = hashlib.sha256(effective.encode()).hexdigest()
        if task["kind"] == "block_ctrl":
            child = directory / "block_dot_contract.psl"
            original_child = child.read_text()
            assumption = "  A_BLOCK_COUNT: assume always (count <= BLOCK_WORDS);"
            if original_child.count(assumption) != 1:
                raise RuntimeError("child count obligation changed")
            shutil.copy2(child, directory / "block_dot_contract.source.psl")
            child.write_text(original_child.replace(assumption,
                "  -- No count assumption here: F_BCTRL_COUNT proves the connected bound."))
            result["effective_child_properties_sha256"] = hashlib.sha256(child.read_bytes()).hexdigest()
            if task.get("block_witness"):
                # Child reachability is exercised separately by the memory,
                # sequencer and RX cover tasks. This task witnesses dispatcher paths.
                for child_name in ("block_dot_contract.psl", "block_rx_contract.psl"):
                    child_file = directory / child_name
                    effective_child = re.sub(r"  C_\w+: cover \{[^}]*\};\n", "", child_file.read_text())
                    child_file.write_text(effective_child)
                result["effective_child_properties_sha256"] = {name: hashlib.sha256((directory / name).read_bytes()).hexdigest()
                    for name in ("block_dot_contract.psl", "block_rx_contract.psl")}
            result["discharged_obligation"] = "block_dot.count <= 1024 asserted as F_BCTRL_COUNT on its actual connected input"
        script = f"ghdl --std=08 --no-assert-cover {generics} {vhdl} -e {entity}; write_rtlil dut.il; write_json frontend.json"
        if command(["yosys", "-p", script], "frontend.txt"):
            raise RuntimeError("VHDL frontend failed")
        if task["kind"] == "ctrl":
            ports = json.loads((directory / "frontend.json").read_text())["modules"][entity]["ports"]
            rows, cols = task["parameters"]["G_ROWS"], task["parameters"]["G_COLS"]
            expected_ports = dict(cfg=32*rows*cols, pe_regs=16*rows*cols, west_o=16*rows, north_o=16*cols)
            for port, width in expected_ports.items():
                if len(ports[port]["bits"]) != width:
                    raise RuntimeError(f"generic elaboration mismatch: {port} expected width {width}")
            result["checked_port_widths"] = expected_ports
        elif task["kind"] in ("tx", "rx"):
            ports = json.loads((directory / "frontend.json").read_text())["modules"][entity]["ports"]
            expected_ports = (dict(tx_data=8, tx_serial=1, tx_start=1, tx_busy=1)
                              if task["kind"] == "tx" else dict(rx_data=8, rx_serial=1, rx_valid=1))
            if any(len(ports[p]["bits"]) != width for p, width in expected_ports.items()):
                raise RuntimeError("UART port width changed")
            result["checked_port_widths"] = expected_ports
        elif task["kind"] == "block_ctrl":
            ports = json.loads((directory / "frontend.json").read_text())["modules"][entity]["ports"]
            expected_ports = dict(a=16, b=16, pe_result=16, rx_data=8, tx_data=8)
            if any(len(ports[p]["bits"]) != width for p, width in expected_ports.items()):
                raise RuntimeError("block controller interface changed")
            result["checked_port_widths"] = expected_ports
            result["environment"] = "initial synchronous reset; later reset/RX/TX busy/PE result unrestricted"
            result["scope"] = "integrated parser/length/index safety, commit bounds, ownership, TX handshake and duplicate isolation"
            if task.get("block_witness"):
                result["environment"] = "reachability-only schedule: initial reset, prompt TX, preflight/HELLO then one of four fixed valid frames"
                result["scope"] = "dispatcher covers only; witness assumptions are absent from universal proofs"
        elif task["kind"] == "block_rx":
            ports = json.loads((directory / "frontend.json").read_text())["modules"][entity]["ports"]
            expected_ports = dict(raw=8*144, rx_data=8, length=8, valid_o=1)
            if any(len(ports[p]["bits"]) != width for p, width in expected_ports.items()):
                raise RuntimeError("frame receiver width changed")
            result["checked_port_widths"] = expected_ports
            result["scope"] = "bounded storage, ordered append, CRC recurrence/publication, reset and watchdog safety"
        elif task["kind"] == "block_dot":
            ports = json.loads((directory / "frontend.json").read_text())["modules"][entity]["ports"]
            expected_ports = dict(pe_result=16, wr_data=16, a=16, b=16, result=16)
            if any(len(ports[p]["bits"]) != width for p, width in expected_ports.items()):
                raise RuntimeError("block datapath width changed")
            result["checked_port_widths"] = expected_ports
            result["environment"] = "count <= 1024 (production port subtype); all other inputs unconstrained"
            result["scope"] = ("sequencer control plus full-size RAM retention/read/capture at any constant address; "
                               "unwritten words unconstrained; not caller length validation or CRC" if task.get("memory") else
                               "sequencer control, capture count/cadence, reset and result capture; not RAM data or CRC")
        elif task["kind"] == "array":
            ports = json.loads((directory / "frontend.json").read_text())["modules"][entity]["ports"]
            rows, cols = task["parameters"]["G_ROWS"], task["parameters"]["G_COLS"]
            expected_ports = dict(cfg=32*rows*cols, q=16*rows*cols, west=16*rows, north=16*cols)
            if any(len(ports[p]["bits"]) != width for p, width in expected_ports.items()):
                raise RuntimeError("array generic/port width mismatch")
            result["checked_port_widths"] = expected_ports
        else:
            ports = json.loads((directory / "frontend.json").read_text())["modules"][entity]["ports"]
            expected_ports = dict(n=16, s=16, e=16, w=16, q=16, cfg=32)
            if any(len(ports[p]["bits"]) != width for p, width in expected_ports.items()):
                raise RuntimeError("PE datapath/config width changed")
            result["checked_port_widths"] = expected_ports
        model = "read_rtlil dut.il\n"
        if task.get("composition"):
            # Replace ONLY PE instances by a transition contract already
            # proved on production pe.vhd. All imported array wiring survives.
            frontend = json.loads((directory / "frontend.json").read_text())["modules"]
            pe_ports = frontend["pe_Brtl"]["ports"]
            expected_pe = {"clk":1, "rst":1, "step":1, "cfg":32, "dout":16,
                           **{f"neigh[{d}]":16 for d in "nsew"}}
            if set(pe_ports) != set(expected_pe) or any(len(pe_ports[p]["bits"]) != w
                                                        for p,w in expected_pe.items()):
                raise RuntimeError("PE abstraction interface changed")
            instances = sum(c["type"] == "pe_Brtl" for m in frontend.values()
                            for c in m.get("cells", {}).values())
            if instances != rows*cols: raise RuntimeError("unexpected production PE instance count")
            model += "delete pe_Brtl\n"
            result["abstraction"] = dict(component="pe_Brtl", instances=instances,
                obligation="F-PE-ALU-MUX", contract="dout(t+1)=reset/hold/full-16-bit-ALU transition",
                initial_state="abstract PE outputs arbitrary; first-cycle reset required by parent")
            result["environment"] += "; proved PE transition guarantees imported as assumptions"

        files = "dut.il\n"
        if task["kind"] in ("pe", "array"):
            define = "-D NEGATIVE" if task.get("negative") and not task.get("mutation") else ""
            if task.get("mutation") == "array-north-self":
                define = "-D MUTATION_NORTH"
                result["negative_stimulus"] = "distinct per-PE CONST or PASS-north each cycle; all other inputs unrestricted"
            sv = [Path(p).name for p in paths if p.endswith(".sv")]
            model += f"read_verilog -formal {define} {' '.join(sv)}\n"
            files += "\n".join(sv) + "\n"
        if task["kind"] == "array":
            model += f"chparam -set ROWS {rows} -set COLS {cols} array_contract\n"
        if task["kind"] == "block_ctrl":
            # Preserve every signal, input and property. Unmark the top output
            # ports so ordinary cone-of-influence optimization can discard RAM
            # data unused by controller properties/covers after hierarchy flattening.
            model += "delete -output block_ctrl/w:*\n"
            result["cone_reduction"] = "top outputs become internal wires; no object, assertion, assumption or input deleted"
        model += f"prep -top {task['top']} -flatten\n"
        if task.get("block_witness"):
            model += "memory_map\nopt -full\n"
        if task.get("gates"):
            model += "wreduce\nopt -full\ntechmap\nopt -full\n"
        model += "write_json properties.json\n"
        (directory / "task.sby").write_text(
            f"[options]\nmode {task['mode']}\ndepth {task['depth']}\ntimeout {budget}\n"
            + (f"skip {task['skip']}\n" if "skip" in task else "") +
            f"[engines]\n{result['engine']}\n[script]\n{model}[files]\n{files}")
        rc = command(["sby", "task.sby"], "solver.txt")
        result["exit_code"] = rc
        statusfile = directory / "task/status"
        status = statusfile.read_text().split()[0] if statusfile.exists() else "ERROR"
        result["solver_status"] = status
        result["result"] = {"PASS": ("PROVED" if task["mode"] == "prove" else "BOUNDED_PASS"),
                            "FAIL": "FAIL", "TIMEOUT": "TIMEOUT", "UNKNOWN": "UNKNOWN"}.get(status, "ERROR")

        # Inspect the prepared property cells AND the actual SMT model. A solver
        # PASS alone is insufficient: a dropped property must fail this gate.
        design = json.loads((directory / "task/src/properties.json").read_text())
        counts = Counter()
        for m in design["modules"].values():
            for c in m["cells"].values():
                if c["type"] == "$check":
                    counts[c["parameters"]["FLAVOR"]] += 1
                elif c["type"] in ("$assert", "$assume", "$cover"):
                    counts[c["type"][1:]] += 1
        result["property_cells"] = dict(counts)
        if task["kind"] == "block_ctrl":
            widths = sorted(int(c["parameters"]["WIDTH"], 2) for m in design["modules"].values()
                            for c in m["cells"].values() if c["type"] == "$anyconst")
            if widths != ([2, 8] if task.get("block_witness") else [8]):
                raise RuntimeError("symbolic cache/witness selector missing or wrong width")
            result["symbolic_cache_address_bits"] = 8
        if task.get("memory"):
            symbolic = [c for m in design["modules"].values() for c in m["cells"].values()
                        if c["type"] == "$anyconst"]
            if len(symbolic) != 1 or int(symbolic[0]["parameters"]["WIDTH"], 2) != 10:
                raise RuntimeError("symbolic memory address missing or wrong width")
            result["symbolic_address_bits"] = 10
            memories = {name: {k: int(c["parameters"][k], 2) for k in ("SIZE", "WIDTH", "ABITS")}
                        for m in design["modules"].values() for name, c in m["cells"].items()
                        if c["type"] == "$mem_v2"}
            if memories != {bank: dict(SIZE=1024, WIDTH=16, ABITS=10) for bank in ("bank_a", "bank_b")}:
                raise RuntimeError("production memory count/capacity/width changed")
            result["checked_memories"] = memories
        for singular, plural in (("assert", "assertions"), ("assume", "assumptions"), ("cover", "covers")):
            if counts[singular] != task[plural]:
                raise RuntimeError(f"property loss/change: {singular} expected {task[plural]}, found {counts[singular]}")
        smt = (directory / "task/model/design_smt2.smt2").read_text()
        smt_counts = {k: len(re.findall(r"^; yosys-smt2-" + k + r" ", smt, re.M))
                      for k in ("assert", "assume", "cover")}
        result["smt2_cells"] = smt_counts
        if task["kind"] == "block_ctrl":
            widths = sorted(int(x) for x in re.findall(r"^; yosys-smt2-anyconst \S+ (\d+) ", smt, re.M))
            if widths != ([2] if task.get("block_witness") else [8]):
                raise RuntimeError("cache address/witness selector did not reach SMT model")
            result["smt2_symbolic_widths"] = widths
        if task.get("memory"):
            addresses = re.findall(r"^; yosys-smt2-anyconst \S+ (\d+) ", smt, re.M)
            if addresses != ["10"]:
                raise RuntimeError("symbolic memory address did not reach SMT model")
            result["smt2_symbolic_address_bits"] = 10
        user_assumptions = sum(line.startswith("; yosys-smt2-assume ") and "$auto$formalff" not in line
                               for line in smt.splitlines())
        result["smt2_user_assumptions"] = user_assumptions
        selected = "cover" if task["mode"] == "cover" else "assert"
        if smt_counts[selected] != counts[selected] or user_assumptions != counts["assume"]:
            raise RuntimeError("properties did not reach SMT model")
        result["model_sha256"] = {
            p: hashlib.sha256((directory / p).read_bytes()).hexdigest()
            for p in ("dut.il", "task/src/properties.json", "task/model/design_smt2.smt2")}
        expected = "FAIL" if task.get("negative") else "PASS"
        traces = sorted(str(p.relative_to(directory)) for p in (directory / "task/engine_0").glob("trace*.yw"))
        result["traces"] = traces
        if task.get("negative") and not traces:
            result["error"] = "negative control did not produce a counterexample"
            if status == "FAIL": raise RuntimeError(result["error"])
        result["acceptance_passed"] = status == expected and rc == (2 if task.get("negative") else 0)
        if task["mode"] == "cover":
            result["meaning"] = "bounded reachability only; no universal guarantee"
    except subprocess.TimeoutExpired:
        result["result"] = "TIMEOUT"
        result["error"] = "outer process budget exceeded"
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        result["result"] = "ERROR"
        result["error"] = str(error)
    result["duration_seconds"] = round(time.monotonic() - start, 3)
    (directory / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"{task['id']}: {result['result']} ({result['duration_seconds']} s)"
          + (" [expected negative]" if task.get("negative") else ""), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-deps", action="store_true")
    parser.add_argument("--suite", choices=("smoke", "fast", "full", "cover"), default="fast")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--task", action="append", help="explicit subset by stable ID; manifest marks subset")
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args()
    try:
        versions = dependencies()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f"formal: {error}", file=sys.stderr)
        return 1
    if args.check_deps:
        print(json.dumps(versions, indent=2))
        return 0
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + f"-{os.getpid()}"
    out = args.out or ROOT / "hw/build/formal" / (args.suite + "-" + run_id)
    out.mkdir(parents=True, exist_ok=False)
    shutil.copy2(Path(__file__), out / "run.py")
    shutil.copy2(FORMAL / "toolchain.json", out / "toolchain.json")
    revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True)
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True)
    manifest = dict(schema_version=1, run_id=run_id, suite=args.suite, tools=versions,
                    commit=(revision.stdout.strip() if revision.returncode == 0
                            else os.environ.get("CGRA_SOURCE_REVISION")),
                    dirty=(bool(dirty.stdout) if dirty.returncode == 0 else
                           {"true": True, "false": False}.get(os.environ.get("CGRA_SOURCE_DIRTY"))),
                    runner_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), tasks=[])
    selected = tasks(args.suite)
    if args.task:
        known = {t["id"] for t in selected}
        if not set(args.task) <= known: parser.error("unknown task for selected suite")
        required = set(args.task)
        if any(t.get("composition") and t["id"] in required for t in selected):
            required.add("F-PE-ALU-MUX" if args.suite != "cover" else "F-PE-ALU-MUX-COVER")
        selected = [t for t in selected if t["id"] in required]
    if args.suite == "cover" and any(t.get("composition") for t in selected):
        selected.insert(0, next(t for t in tasks("fast") if t["id"] == "F-PE-ALU-MUX"))
    manifest["complete_suite"] = not bool(args.task)
    manifest["selected_task_ids"] = [t["id"] for t in selected]
    for task in selected:
        obligation = next((t for t in manifest["tasks"] if t["id"] == "F-PE-ALU-MUX" and
                           t["result"] == "PROVED" and t["acceptance_passed"]), None)
        same_inputs = obligation and all(obligation["source_sha256"].get(p) ==
                         hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in
                         ("hw/rtl/cgra_pkg.vhd", "hw/rtl/pe.vhd", "hw/formal/pe_spec.sv"))
        if task.get("composition") and not same_inputs:
            manifest["tasks"].append(dict(task, result="NOT_RUN", acceptance_passed=False,
                                         error="mandatory production PE obligation not proved in this run"))
        else:
            manifest["tasks"].append(run_task(task, out / task["id"], args.timeout))
        (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    ok = all(t["acceptance_passed"] for t in manifest["tasks"])
    print(f"Manifest: {out / 'manifest.json'}; acceptance {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
