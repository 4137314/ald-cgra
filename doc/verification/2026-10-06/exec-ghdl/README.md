# EXEC byte ordering — existing GHDL regression detects the formal mutation

`replay.py` compiles the production controller/array and the existing C-model
protocol transcript generator in temporary directories. **504 transactions
PASS**, 4x4, STEP_DIV=2, including watchdog recovery. The same low-byte-to-high-
byte mutation as `M-EXEC-BYTE` is applied only to a second temporary RTL copy.
GHDL fails at transaction 50, byte 0: expected 51, received 191. The driver
requires the simulation phase and that protocol assertion, not merely any
nonzero tool exit, for negative acceptance. Production RTL is unchanged.

The initial attempt in `../exec-ghdl-initial/` failed before simulation because
GHDL elaboration opens the default vector file before the runtime generic
override. That attempt is not a positive or negative-control verification.
The corrected runner supplies the same hashed vector at the default path as
well. All commands, tool versions, hashes and logs are retained.
