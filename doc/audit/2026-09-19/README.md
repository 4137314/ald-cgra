# Riproduzioni dell'audit

Snapshot diagnostico del 18–19 settembre 2026. Eseguire dalla radice della
repository. Questi esempi dimostrano difetti nella revisione analizzata:
non costituiscono una suite di regressione che deve passare.

`revision.txt` e `source-sha256.txt` identificano il codice analizzato, inclusi
i cambiamenti già presenti nel working tree. `test.txt`, `sim.txt` e
`latex-nix-clean.txt` conservano gli esiti delle verifiche generali.

## CLI e DSL

```sh
make lib sw
python3 doc/audit/2026-09-19/cli_repro.py
```

Gli esempi usano l'emulatore e file temporanei. L'esempio di input/output sullo
stesso file opera esclusivamente dentro una directory temporanea.
`cli-observed.txt` registra l'esecuzione durante l'audit.

## Trasporto C attraverso PTY

```sh
python3 doc/audit/2026-09-19/transport.py
```

Carica `lib/build/libcgra.so`, usa PTY locali e il parser dell'emulatore.
Dimostra timeout riavviato per frammento, contatore RX incrementato senza
ricezione e recupero RUN che interpreta RST come sei step. Il parser emulato
non ha un watchdog temporale: nell'RTL la stessa sequenza è pertinente quando
il recupero host precede il watchdog, ad esempio 50 ms rispetto a 1 s.

## Contratti C e overflow

```sh
cc -g -O0 -fno-wrapv -fsanitize=undefined -fno-sanitize-recover=undefined \
  -Ilib/include -Isw doc/audit/2026-09-19/api_edges.c \
  lib/src/cgra.c lib/src/emu.c sw/compile.c sw/dsl.c \
  -o /tmp/cgra-audit-api-20260919
/tmp/cgra-audit-api-20260919
/tmp/cgra-audit-api-20260919 overflow
```

L'ultima invocazione deve fallire con signed integer overflow nel codice
analizzato. `-fno-wrapv` rende esplicite le normali regole C per l'overflow
signed; alcuni wrapper del compilatore nell'ambiente Nix aggiungono `-fwrapv`.
`NULL` per il secondo vettore di `cgra_vec_binop` viene correttamente rifiutato:
è incluso come controllo, non come difetto.

## Descrittore seriale oltre FD_SETSIZE

```sh
cc -g -O1 -fsanitize=address -Ilib/include \
  doc/audit/2026-09-19/highfd.c lib/src/cgra.c lib/src/emu.c \
  -o /tmp/cgra-audit-highfd-20260919
/tmp/cgra-audit-highfd-20260919
```

Richiede un limite di almeno circa 1.110 descrittori per processo. Apre
`/dev/null` e una PTY, senza usare porte fisiche. Nella configurazione verificata
il processo termina con `bit out of range 0 - FD_SETSIZE on fd_set` (exit 134).
I descrittori sono rilasciati alla terminazione del processo.

## Assertion RTL che falliscono

`tb_audit_mcp.vhd` è una copia del banco di replay con un'asserzione aggiunta
sulla distanza reset→primo step. `tb_audit_uart.vhd` presenta uno stop bit basso.
Il primo richiede il file di vettori prodotto dalla suite esistente.

```sh
make sim
mkdir -p /tmp/cgra-audit-ghdl-20260919
ghdl -a --std=08 --workdir=/tmp/cgra-audit-ghdl-20260919 \
  hw/rtl/cgra_pkg.vhd hw/rtl/pe.vhd hw/rtl/cgra_comp_pkg.vhd \
  hw/rtl/cgra_array.vhd hw/rtl/cgra_ctrl.vhd hw/rtl/uart_rx.vhd \
  doc/audit/2026-09-19/tb_audit_mcp.vhd \
  doc/audit/2026-09-19/tb_audit_uart.vhd
ghdl -r --std=08 --workdir=/tmp/cgra-audit-ghdl-20260919 tb_audit_mcp \
  -gG_VECTOR_FILE=hw/build/protocol_diff.vec -gG_STEP_DIV=2 --assert-level=error
ghdl -r --std=08 --workdir=/tmp/cgra-audit-ghdl-20260919 tb_audit_uart --assert-level=error
```

Entrambe le ultime invocazioni falliscono sul codice analizzato. Sono verifiche
funzionali della sequenza dei segnali, non misure di ritardi post-route.

## Controllo del passaggio GCC analyzer

`analyzer_probe.c` contiene intenzionalmente una dereferenziazione NULL.
Confrontare la diagnostica dei due comandi:

```sh
cc -fanalyzer -Wall -Wextra -fsyntax-only doc/audit/2026-09-19/analyzer_probe.c
cc -fanalyzer -Wall -Wextra -c doc/audit/2026-09-19/analyzer_probe.c \
  -o /tmp/cgra-audit-analyzer-20260919.o
```

L'esito osservato è in `analyzer-observed.txt`; `tool-versions.txt` identifica
GCC, GHDL, Nix e Python. I file `*-observed.txt` conservano anche gli esiti
delle prove C, PTY e RTL descritte sopra.

## Bibliografia

`crossref.json` conserva le risposte della prima verifica automatica dei DOI,
compresi gli errori HTTP: un errore 429 non dimostra un riferimento scorretto.
Per PipeRench la verifica successiva ha confermato il record IEEE
`10.1109/ISCA.1999.765937`; il DOI ACM presente nella repository restituiva 404.
Il record IEEE è conservato in `piperench-crossref.json`; un secondo controllo
delle richieste precedentemente limitate è in `crossref-recheck.json`.
Le fonti primarie e i limiti della verifica sono nel documento principale.
