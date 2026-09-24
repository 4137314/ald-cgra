#!/usr/bin/env python3
"""Compile one report unit using the report's class, styles and source files."""
import argparse
from pathlib import Path
import re
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('kind', choices=('figure', 'table', 'section', 'part'))
parser.add_argument('name', help='unit name; parts use e.g. protocol/fused-execute')
parser.add_argument('--latexmk', default='latexmk')
args = parser.parse_args()
if not re.fullmatch(r'[a-z][a-z0-9-]*(/[a-z][a-z0-9-]*)*', args.name):
    parser.error('invalid unit name')
if args.kind != 'part' and '/' in args.name:
    parser.error('only part names may contain a directory')
if args.kind in ('figure', 'table'):
    unit = Path(args.kind + 's') / args.name / (args.kind + '.tex')
else:
    unit = Path('src') / (args.name + '.tex')
if not (root / unit).is_file() or args.name == 'sections':
    parser.error(f'no {args.kind} unit: {unit}')
out = Path('build/previews') / args.kind / args.name
(root / out).mkdir(parents=True, exist_ok=True)
prefix = r'''% Generated preview driver; edit the source unit, not this file.
\documentclass[conference]{IEEEtran}
\usepackage{xr}
\input{config/preamble}
% Full-report references are an optional cache. They are deliberately prefixed
% so local labels/citations cannot overwrite or duplicate the imported names.
\IfFileExists{build/main.aux}{\externaldocument[report-]{build/main}}{}
\hypersetup{pdftitle={CGRA unit preview},pdfpagemode=UseNone}
\begin{document}
% Install after hyperref's begin-document hooks redefine \ref.
\let\previewOriginalRef\ref
\renewcommand{\ref}[1]{%
  \ifcsname r@report-#1\endcsname
    \previewOriginalRef*{report-#1}%
  \else
    \previewOriginalRef*{#1}%
  \fi}
'''

def has_citations(path, seen=None):
    """Check this unit's literal input graph, not unrelated report sections."""
    seen = set() if seen is None else seen
    if path in seen:
        return False
    seen.add(path)
    source = (root / path).read_text()
    if re.search(r'\\cite(?:\[[^\]]*\])?\{', source):
        return True
    for name in re.findall(r'\\input\{([^}]+)\}', source):
        child = Path(name if name.endswith('.tex') else name + '.tex')
        if (root / child).is_file() and has_citations(child, seen):
            return True
    return False

if args.kind in ('figure', 'table'):
    width = r'\textwidth' if r'\begin{figure*}' in (root / unit).read_text() else r'\columnwidth'
    body = r'\def\previewWidth{' + width + '}\n'
    body += r'\def\previewKind{' + args.kind + '}\n'
    body += r'\def\previewUnit{' + unit.as_posix() + '}\n'
    body += r'\input{tools/preview-float}' + '\n'
else:
    body = r'\input{' + unit.as_posix() + '}\n'
    if has_citations(unit):
        body += r'\bibliographystyle{IEEEtran}' + '\n' + r'\bibliography{bib/bib}' + '\n'
text = prefix + body + '\\end{document}\n'
driver = out / 'preview.tex'
# Stable mtime lets latexmk skip unchanged previews and track only their inputs.
if not (root / driver).exists() or (root / driver).read_text() != text:
    (root / driver).write_text(text)
command = shlex.split(args.latexmk) + ['-pdf', '-interaction=nonstopmode',
          '-halt-on-error', '-file-line-error', f'-output-directory={out}', str(driver)]
subprocess.run(command, cwd=root, check=True)
print(f'Preview: doc/{out}/preview.pdf')
