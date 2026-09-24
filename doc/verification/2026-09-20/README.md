# Verifiche delle correzioni — 20 settembre 2026

Esiti sul working tree, successivi all'audit storico del 19 settembre.
Stato e limiti delle correzioni: [TASKS.md](../../../TASKS.md).

| Log | Comando (dalla root salvo indicazioni) | Esito |
|---|---|---|
| first-test.txt | `make test` | exit 0 |
| first-asan.txt | `make PROFILE=asan test` | exit 0 |
| first-sim.txt | `make sim` | exit 0 |
| first-analyze-lib.txt | `make -C lib -f lib.mk analyze` | exit 0 |
| first-analyze-sw.txt | `make -C sw -f sw.mk analyze` | exit 0 |

I log riguardano simulazione e software su Linux. Non certificano una FPGA.
