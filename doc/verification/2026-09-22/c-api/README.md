# Verifiche API C — 22 settembre 2026

Quinto gruppo di correzioni, iniziato il 21 settembre e completato il 22;
[registro task](../../../../TASKS.md). I gruppi precedenti restano conservati.

| Log | Comando dalla root | Esito |
|---|---|---|
| test.txt | `make test` | exit 0 |
| asan.txt | `make PROFILE=asan test` | exit 0, ASan + UBSan |
| install.txt | `make test-install` | exit 0 |
| sim.txt | `make sim` | exit 0, replay e test GHDL |
| lib-gates.txt | `make -C lib -f lib.mk analyze man` | exit 0 |
| sw-gates.txt | `make -C sw -f sw.mk analyze doc` | exit 0 |
| compdb.txt | `make compdb` | exit 0 |

Entrambe le suite complete: **299** controlli libreria, **122** PTY,
**262** kernel pubblici, **6** guasti forzati, **253** software,
**16** CLI shell, **61** benchmark/CLI Python (**1019** in totale).
In aggiunta, **9** controlli d'installazione. `git diff --check` superato.
Database clangd letto con un parser JSON e verificato per le nuove sorgenti.
Nessun warning del compilatore C o diagnostica dei sanitizzatori nei log finali.
GHDL termina con due avvisi di nomi oscurati nei testbench (`tb_matvec.vhd`
e `tb_kernel_uart.vhd`), riportati in `sim.txt`; nessuna assertion fallita.

## Contratti verificati

- `cgra_matvec`, `cgra_scan` e `cgra_conv` sono nella libreria statica e
  condivisa installata, senza dipendenza dal DSL. La CLI chiama gli stessi
  motori tramite adattatori che preservano i ritorni interni in numero di
  elementi. L'API pubblica ritorna sempre uno status.
- Riferimenti scalari per matrice 5×7, scan ADD/MUL/MIN/MAX e convoluzione,
  con overflow a 16 bit e code di tile, su 1×1, 1×16, 16×1, 2×3, 3×2,
  2×8, 8×2 e 4×4, ciascuna in v2 e v3.
- Capacità, footprint/somme/prodotti fuori intervallo, canary, puntatori
  NULL/input vuoti, operazioni sullo stesso buffer, sovrapposizioni parziali
  rifiutate prima dell'ID e dell'esecuzione, `errno` di apertura.
- Peer PTY: risposta corretta al primo chunk, checksum errato al secondo;
  output parziale di vector/scan/matvec/conv, dot non pubblicato su errore,
  sessione bloccata e assenza di retry o altri comandi successivi.
- Wrapping GNU/ELF di `calloc`, `read` e `__read_chk`: memoria esaurita per
  workspace/emulatore, callback PTY solo dopo allocazione riuscita, ripetizione
  su EINTR ed errore pubblico su EIO. Le risposte forzate non sono misure di
  comportamento della scheda.
- Client C esterno compilato con `cgra.h` e pkg-config installati, collegato
  alla libreria condivisa: i tre nuovi kernel eseguiti su 2×3 in v2/v3.
- Replay dei transcript host contro controller/array RTL su otto geometrie,
  replay UART su 2×3/3×2, regressioni protocollo e wiring standalone fino a 8×8.
  Il generatore ora collega anche `lib/src/kernels.c` direttamente dalle sorgenti.

## Problemi risolti durante la verifica

Il primo test con errori forzati intercettava soltanto `read`; in ASan/Fortify
il compilatore emetteva `__read_chk`, causando attesa fino all'allarme del test.
Il wrapper finale copre entrambe le varianti mantenendo il controllo della
capacità. Dopo il cambio di ambiente, LeakSanitizer non poteva funzionare nel
sandbox basato su ptrace: la suite finale è stata autorizzata ed eseguita fuori
dal sandbox. I log sopra riportano i run finali riusciti.

## Limiti e provenienza

Verificato su Linux con toolchain in `environment.txt`. Il test d'iniezione
richiede un linker con `--wrap`; altre piattaforme e il pacchetto/check Nix
rimangono nel task T3. La sincronizzazione sullo stesso handle/dispositivo
resta a carico del chiamante. I server PTY usano `ptsname` all'avvio e devono
essere separati in processi se concorrenti.

La convoluzione conserva una Toeplitz densa in memoria. La riduzione della
memoria tramite tile implicite resta un'ottimizzazione distinta. I kernel
possono lasciare risultati parziali su errore; non promettono rollback.

Nessuna modifica RTL, misura Vivado o prova su scheda in questo gruppo.
Il report LaTeX non è modificato qui; restano i task di revisione R1–R6.
`sources.sha256` identifica sorgenti host, RTL/test/build e documentazione
di riferimento del working tree verificato; HEAD è solo il commit di base.
