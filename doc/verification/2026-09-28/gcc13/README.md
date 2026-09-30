# GCC 13.3 diagnostics reproduced

`before.txt` runs the historical `dc886d2376c2662a798c65ac86d0e97938ceb3fb`
sources in an isolated Ubuntu 24.04 container, with GCC
`13.3.0-6ubuntu2~24.04.1`. Both review diagnostics were reproduced:

- `-O3`: `-Wstringop-overflow` in EXEC serialization.
- `-O1 -fanalyzer -Werror`: uninitialized `g.rows` through the dot/configure
  call chain. The reported path omits the mandatory output assignment on a
  successful `cgra_get_info` return; it is inconsistent with that function's
  contract. GCC 15 did not produce the warning.

No counterexample to the supported geometry contract was found. Serializers
now expose individual edge bounds and the tighter derived invariant
`R+C <= MAX_PE+1` for positive R,C with `RC <= MAX_PE` before writing buffers.
Merely checking `R+C <= 2*MAX_EDGE` still warned for WR under vectorization;
those intermediate logs are retained. `geometric-sum-bound.txt` is the clean
`-O3 -Wall -Wextra -Werror` compilation (empty output, exit 0).

Separating diagonal vector/dot kernels from the transport translation unit
eliminates the analyzer's incorrect interprocedural path without initializing
caller output to hide a missing assignment and without warning suppressions.
The resulting code passes both GCC 13.3 analyzers and full software tests:
see `doc/verification/2026-09-29/gcc13-final/manifest.json` and `output.txt`.

Reproduction without relying on the local image cache:

```
git archive dc886d2376c2662a798c65ac86d0e97938ceb3fb | tar -x -C NEW_EMPTY_DIRECTORY
docker run --rm -v ABSOLUTE_DIRECTORY:/work -w /work ubuntu:24.04 sh -c \
  'apt-get update && apt-get install -y --no-install-recommends gcc make python3 libc6-dev && gcc --version && make -C lib -f lib.mk static && make -C lib -f lib.mk analyze'
```

The image ID used locally is recorded in `image-id.txt`; exact compiler package
version is printed in the logs. Ubuntu repositories can change, so compare
the reported version before claiming to have reproduced the same compiler.
Later checks copy current sources into the container and keep the repository
mount read-only. No old evidence or author source was reset or overwritten.
