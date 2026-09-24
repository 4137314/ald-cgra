# Verifiche parsing e benchmark — 21 settembre 2026

Terzo gruppo di correzioni; [registro task](../../../../TASKS.md).
I log degli altri gruppi rimangono conservati nelle directory precedenti.

| Log | Comando dalla root | Esito |
|---|---|---|
| test.txt | `make test` | exit 0 |
| asan.txt | `make PROFILE=asan test` | exit 0, ASan + UBSan |
| analyze.txt | `make -C sw -f sw.mk analyze` | exit 0, include bench_ref.c |
| docs.txt | `make -C sw -f sw.mk doc` | exit 0, man/info |

Entrambe le suite C/CLI: **299** controlli libreria, **77** PTY,
**169** software, **16** CLI shell, **61** benchmark/CLI Python.
Nessun test del descrittore alto saltato. Nessun warning del compilatore o
sanitizzatore nei log. `git diff --check` superato.

I test benchmark usano cinque geometrie (1×16, 16×1, 2×3, 3×2, 4×4), protocolli
v2/v3 e riferimenti scalari indipendenti. Il template custom 4×4 fuori geometria
è atteso come errore. Un peer PTY restituisce dati sbagliati con checksum valido:
il test richiede il fallimento del benchmark. I numeri di tempo nei log sono
misure del software locale e non prestazioni FPGA.

Si verificano escaping/UTF-8 con json.loads, rifiuto di NaN/Infinity, metadati,
fallimenti/skips, artefatti con exit nonzero, parsing numerico e capacità.
Non sono stati eseguiti nuovi run Vivado o su scheda. Il report LaTeX non è
stato modificato da questo gruppo; qui sono ricostruiti i manuali CLI.

Il workload dei benchmark è cambiato (deterministic-edges-v1); l'oracolo e il
confronto stanno fuori dal tempo misurato, mentre CFG/esecuzione restano dentro.
Non confrontare direttamente con tempi precedenti senza ripetere le misure.
Provenienza: `sources.sha256` e `environment.txt`; working tree non committato.
