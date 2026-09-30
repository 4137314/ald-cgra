# Baseline before consolidation

Initial HEAD: `dc886d2376c2662a798c65ac86d0e97938ceb3fb`; initially clean.
`sources.sha256` identifies the actual tracked input files, before changes.
`environment.json`, `gates.json` and `defects.json` record commands and outcomes.
Logs belong to this snapshot; subsequent runs must use new directories.

All eight baseline commands in `gates.json` returned zero. Software checks:
299 library + 122 transport + 262 kernels + 6 injected faults + 253 DSL/compiler
+ 46 CLI + 61 benchmark = **1049**, independently in release and ASan/UBSan.
LSan remained enabled. Installation: **12**; compilation database: **121**;
mock Tcl: **56**. These counts are not coverage percentages. Tcl's deliberate
negative cases print errors and are expected by the regression harness.
GHDL logs distinguish each geometry and protocol simulation. The documentation
gate was incremental (existing current PDF); it is not a new editorial review.
The analyzer used GCC 15.2.0; it does not resolve the GCC 13.3 investigation.

Three reproduced defects (all incorrectly exit 0):

```
sw/build/cgra ping -d sim unexpected
sw/build/cgra run add --a 1 --b 2 -d sim --cols 4
make -C lib -f lib.mk CC=/nonexistent/cgra-review-cc static
```

The first formal dependency acquisition uses the repository's existing
`flake.lock`, not a floating channel. `formal-tools-build.log` and
`formal-tools.path` record that acquisition; formal proof results are stored
separately from this pre-change software baseline.
