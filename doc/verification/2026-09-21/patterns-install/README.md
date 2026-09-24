# Verifiche pattern e installazione — 21 settembre 2026

Quarto gruppo di correzioni; [registro task](../../../../TASKS.md).
I log dei gruppi precedenti descrivono i rispettivi snapshot e restano conservati.

| Log | Comando dalla root | Esito |
|---|---|---|
| test.txt | `make test` | exit 0 |
| asan.txt | `make PROFILE=asan test` | exit 0, ASan + UBSan |
| unit-final.txt | `make -C sw -f sw.mk unit` | exit 0, dopo la correzione del warning |
| asan-unit-final.txt | `make -C sw -f sw.mk PROFILE=asan unit` | exit 0, dopo la correzione del warning |
| install.txt | `make test-install` | exit 0, include install con PROFILE=asan |
| build-final.txt | `make` | exit 0 |
| analyze-initial.txt | `make -C sw -f sw.mk analyze` | exit 0, prima della correzione del warning |
| analyze-final.txt | `make -C sw -f sw.mk analyze` | exit 0, sorgenti finali |
| docs.txt | `make -C sw -f sw.mk doc` | exit 0, man/info |
| compdb.txt | `make compdb` | exit 0 |

Le suite complete contengono **299** controlli libreria, **77** PTY,
**253** software, **16** CLI shell e **61** benchmark/CLI Python.
Il log ASan iniziale conserva un warning GCC di conversione nella maschera
dei tap matvec. La conversione è stata resa esplicita sull'intera espressione;
i 253 controlli software (inclusi matvec e altri kernel) sono stati rieseguiti
in release e ASan/UBSan. I log finali non contengono warning o errori dei
sanitizzatori. Nessuna diagnostica di runtime dei sanitizzatori nella suite
completa. `git diff --check` superato; compilation database letto con un parser
JSON e verificato per l'include dell'header generato in main.c e dsl.c.

## Copertura aggiunta

- Stato iniziale dei PE nonzero, accumulazione custom, chiamate ripetute,
  reset default/once/each e registri NOP non selezionati in lettura.
- Geometrie 1×1, 2×3, 3×2, ciascuna su v2/v3; diagonali con step insufficienti
  per assestare il risultato, seguiti da esecuzione a zero step.
- Tredici combinazioni di campi inapplicabili: parsing sintattico ammesso,
  compilazione/esecuzione rifiutate, output intatto e nessun comando
  CFG/reset/run (ID già in cache prima della misura).
- **9 controlli d'installazione** in directory temporanee, usando una copia
  delle sorgenti di lavoro: prefisso con spazi/apostrofo/ampersand, include
  fuori checkout prima di init, copia dei quattro file stdlib, client C
  esterno con header/libreria/pkg-config installati, cambio PREFIX sugli
  stessi oggetti, DESTDIR seguito da deployment e fallimento di init.
  La prima installazione richiede esplicitamente PROFILE=asan per verificare
  che install costruisca e copi gli artefatti release, manuale incluso.

## Limiti e provenienza

Verifiche su Linux, emulatori e PTY; nessuna scheda o nuova misura Vivado.
RTL invariato in questo gruppo; GHDL non rieseguito. Il pacchetto e i check
Nix non sono stati rieseguiti sulle sorgenti finali. Il report LaTeX non è
stato modificato da questo gruppo; i manuali CLI sono stati ricostruiti.

Il reset iniziale di ogni mode_run su v2 aggiunge traffico rispetto al vecchio
comportamento. I conteggi e tempi storici richiedono nuove misure prima di
essere confrontati. Gli input diagonal/custom vuoti configurano senza reset
o step; le riduzioni vuote azzerano e producono uno zero. Il reset v3 extra
dei diagonali stateless completamente assestati garantisce gli stessi
risultati selezionati; non promette identità di tutti i registri di routing.

`sources.sha256` identifica i file sorgente/build/documentazione host dello
snapshot verificato; `environment.txt` riporta toolchain e HEAD di base.
Il working tree comprende anche correzioni precedenti non committate.
