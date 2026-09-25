#!/usr/bin/env python3
"""Serialize host compiler arguments supplied by the component makefiles."""
import argparse
import json
from pathlib import Path
import shlex
import tempfile


def publish(destination, entries):
    """Keep the old database intact if serialization or writing fails."""
    raw = json.dumps(entries, indent=2) + "\n"
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8",
                                         dir=destination.parent, delete=False) as output:
            temporary = Path(output.name)
            output.write(raw)
        temporary.replace(destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    fragment = commands.add_parser("fragment")
    fragment.add_argument("output", type=Path)
    fragment.add_argument("flags", type=Path)
    fragment.add_argument("sources", nargs="+")
    fragment.add_argument("--tests", nargs="*", default=[])
    merge = commands.add_parser("merge")
    merge.add_argument("output", type=Path)
    merge.add_argument("inputs", type=Path, nargs="+")
    args = parser.parse_args()
    try:
        if args.action == "fragment":
            # make writes CC + CFLAGS directly to this file. Parsing it here
            # avoids another shell/JSON quoting layer around arbitrary paths.
            flags = shlex.split(args.flags.read_text())
            if not flags:
                parser.error("empty compiler command")
            entries = []
            for source in [*args.sources, *args.tests]:
                if not Path(source).is_file():
                    parser.error(f"source does not exist: {source}")
                argv = [*flags]
                if source in args.tests:
                    argv.append("-UNDEBUG")
                argv.extend(["-c", source])
                entries.append({"directory": str(Path.cwd()), "file": source, "arguments": argv})
        else:
            entries = []
            for path in args.inputs:
                fragment = json.loads(path.read_text())
                if not isinstance(fragment, list):
                    parser.error(f"expected a compilation database array: {path}")
                entries.extend(fragment)
        publish(args.output, entries)
    except (OSError, ValueError) as error:
        parser.exit(1, f"compdb: {error}\n")


if __name__ == "__main__":
    main()
