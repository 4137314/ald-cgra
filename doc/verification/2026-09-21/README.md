# Verifiche — 21 settembre 2026

Secondo gruppo di correzioni: sessioni di trasporto e gate Tcl.
Stato e lavoro residuo: [TASKS.md](../../../TASKS.md).
I log del primo gruppo (GHDL incluso) sono nella directory del 20 settembre.

| Log | Comando dalla root | Esito |
|---|---|---|
| test.txt | `make test` | exit 0; 299 libreria, 77 PTY, 141 software, 16 CLI |
| asan.txt | `make PROFILE=asan test` | exit 0; stessi controlli, ASan/UBSan |
| analyze-lib.txt | `make -C lib -f lib.mk analyze` | exit 0 |
| tcl.txt | `make -C hw -f hw.mk test-scripts` | exit 0; 56 controlli |
| manuals.txt | `make -C sw -f sw.mk doc` | exit 0 |
| docs-old-environment-failed.txt | `make docs` | exit 2: vecchio ambiente senza pgfplots |
| docs-nix.txt | `nix develop --command make docs 'LATEXMK=latexmk -g'` | exit 0; PDF 11 pagine, man e info |

I test Tcl usano risposte sintetiche alle chiamate Vivado; i numeri di slack
nei loro log sono inventati per il test e NON sono misure hardware. Non è
stato eseguito Vivado. La verifica fisica dei vincoli e i benchmark su scheda
restano da fare. `sources.sha256` identifica le sorgenti del secondo gruppo.

Il PDF verificato è `doc/build/main.pdf`, ancora distinto dalla copia
`doc/main.pdf` (T4 aperto). Restano due overfull hbox (0.59 e 5.47 pt), R6.
L'avviso transitorio sui riferimenti è risolto dal secondo passaggio latexmk.
Il log Nix segnala anche deprecazioni di `texlive.combine` e
`linuxPackages.perf`; aggiornare il packaging in un ciclo successivo (T3).
