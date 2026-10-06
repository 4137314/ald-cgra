# Block framing/session model — first executable component

The code is private and not yet connected to the device model, controller,
public API or CLI. This checkpoint does not close B01–B03. The selected contract
and remaining integration are in `doc/block-protocol.md`.

Actual gates, with source/command/exit manifests in adjacent directories:

- release and ASan/UBSan/LSan: **3336 named checks each**, including **1036**
  block codec/model checks; LSan enabled;
- GCC 15.2 analyzer: PASS;
- installation **12**, compilation database **211**, incremental build **27**:
  PASS. The expected compdb inventory now includes the new translation units.

The test also enumerates sequence IDs 2 through 65535 with ordinary C
assertions, then checks explicit refusal to wrap. Named-check counts are not
coverage percentages. No model timing is interpreted as FPGA performance.

`measurement.json` captures one actual encoded-byte comparison: eight dot256
executions, signed repeating inputs stated there. Block includes HELLO, both
uploads and every DOT/RESULT; legacy excludes initial ID and includes the
ordinary API's per-call configuration. Every result is checked independently.
**1850 bytes/25 transactions** versus **49976/2072**. This demonstrates potential
traffic savings in the functional model; hardware integration remains required.
