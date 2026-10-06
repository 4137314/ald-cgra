# Fragmented PTY test fixture

The initial GCC 13.3 plan matrix failed the partial-write regression. The
instrumented reproducer retained the original production library and replaced
only the copied test's entry point with 100 repetitions. It observed `read=1`
for the six-byte CFG frame after the library returned success and consumed
both injected EINTR/EAGAIN conditions. A PTY has no packet-read guarantee.

The corrected fixture collects exactly the required number of bytes, retries
EINTR and checks the complete independent expected frame, checksum and TX byte
count. The suite alarm still bounds a missing fragment. Production transport
code is unchanged. Other full-frame reads in the uncertain-session and plan
fixtures use the same helper.

- `before.json`/`before.txt`: actual failing reproduction on the original
  source snapshot; source hashes retained in `source-snapshot.txt`.
- `after.json`/`after.txt`: 100 repetitions of the corrected regression, **800
  checks PASS**, GCC 13.3, same container image. `repeat-fixed.py` is executable
  with the frozen `/src` mount recorded in the manifest.
- `../plans-compiler-fixed/`: complete GCC 13.3 / Clang 18.1.3 release and
  ASan/UBSan/LSan gates, and GCC analyzers; all passed after this change.

The earlier failed matrix in `../plans-compiler/` is retained unchanged.
