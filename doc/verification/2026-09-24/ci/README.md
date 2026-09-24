# Correzione CI Ubuntu — 24 settembre 2026

## Errore e modifica

Il [run iniziale](https://github.com/4137314/ald-cgra/actions/runs/35984335163)
su `a422826` falliva nella compilazione del generatore dei vettori GHDL.
GCC 13 a `-O2 -Wall -Wextra -Werror` non dimostrava l'inizializzazione
di `cfg` attraverso i cicli con geometria runtime di `diag_cfg`.
Il job Nix era riuscito; gli step software successivi a GHDL erano saltati.
Log e stato completi: [initial-failure.txt](initial-failure.txt),
[initial-run.json](initial-run.json).

Il commit `4025339` inizializza esplicitamente il buffer a zero prima di
configurare i PE attivi. La validazione della geometria e il contenuto
trasmesso restano invariati. `-Werror` e tutte le verifiche restano attivi.
Non è stato aggiunto un test che controlli soltanto la presenza dell'inizializzatore:
la compilazione del generatore e i test dei kernel esercitano il caso reale.

## Riproduzione e verifiche locali

Snapshot pulito di `a422826` in container Ubuntu 24.04, poi copia del solo
`lib/src/cgra.c` corretto. Immagine `ubuntu:24.04`, digest
`sha256:008173c23f95b170204355c12626cb5a965d779a7e1283b09e9cffbb1bf33ca3`.
Tool installati con `apt-get install gcc make ghdl tcl python3 texinfo pkg-config`;
versioni in [ubuntu-environment.txt](ubuntu-environment.txt).

| Comando | Esito | Log |
|---|---|---|
| Generatore originale, `make -C hw -f hw.mk build/ghdl/gen_diff_vectors` | Fallimento riprodotto, stesso diagnostico GCC 13 | [ubuntu-before.txt](ubuntu-before.txt) |
| `make sim` dopo la correzione | Successo: tutti i testbench e le geometrie previste | [ubuntu-sim.txt](ubuntu-sim.txt) |
| `make test` | 1019 controlli più smoke test superati | [ubuntu-test.txt](ubuntu-test.txt) |
| `make test-install` | 9 controlli superati | [ubuntu-install.txt](ubuntu-install.txt) |
| `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 make PROFILE=asan test` | 1019 controlli più smoke test superati | [ubuntu-asan.txt](ubuntu-asan.txt) |
| `make -C hw -f hw.mk test-scripts` | 56 controlli superati | [ubuntu-tcl.txt](ubuntu-tcl.txt) |
| Server emulato e `cgra -d "$PTS" probe` | Discovery e ID via PTY riusciti | [ubuntu-probe.txt](ubuntu-probe.txt) |

Il compilatore Nix locale è diverso da quello Ubuntu: il semplice esito
positivo locale precedente non copriva questo errore. I log non sono ripuliti
dai warning: quelli non fatali restano visibili.

Ripetuta inoltre la misura host con `make -C doc -f doc.mk measure`:
CSV identico, metadati aggiornati al sorgente corretto
([wire-data.txt](wire-data.txt)). `make doc` riesce ([report.txt](report.txt)).
Questi due comandi sono eseguiti nel dev shell Nix, fuori dal container.

## Verifica GitHub

Il [run della correzione](https://github.com/4137314/ald-cgra/actions/runs/35985004700)
è terminato con **successo** sul commit `4025339`. Entrambi i job sono
riusciti: `build-and-test` (tutti gli step, inclusi ASan/UBSan e probe PTY)
e `nix` (pacchetto host, report e `nix flake check`).
[remote-run.json](remote-run.json) registra commit, job e stati finali;
[remote-build-and-test.txt](remote-build-and-test.txt) conserva il log Ubuntu.

Il controllo Nix è relativo al runner Linux x86_64; non dimostra il supporto
alle altre piattaforme dichiarate nella flake, che resta nel task T3.

`sources.sha256` identifica i sorgenti di produzione, build e workflow
verificati; i percorsi sono relativi alla root della repository. Le precedenti
evidenze datate identificano i rispettivi snapshot storici e sono preservate.
