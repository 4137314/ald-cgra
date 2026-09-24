# Correzioni e verifiche CGRA

Registro operativo avviato il **19 settembre 2026** sulla base
dell'[audit](doc/audit-2026-09-19.md). L'audit e i suoi log descrivono lo
snapshot precedente alle correzioni e restano conservati come evidenza.

Stati: **DA FARE**, **IN CORSO**, **PARZIALE**, **VERIFICATO**.
Un task passa a VERIFICATO soltanto con modifica e verifica indicate qui.
Le verifiche in simulazione non attestano timing/area della FPGA.

## Task dell'audit

| ID | Priorità | Correzione / criterio di chiusura | Stato |
|---|---|---|---|
| H1 | P1 | Attesa reset→primo step e assertion multicycle anche sulle transizioni | VERIFICATO |
| H2 | P1 | Recupero dopo richieste incomplete senza interpretare RST come payload | VERIFICATO |
| H3 | P2 | Contratto dei guasti, framing, header corrotti e retry verificati | PARZIALE |
| H4 | P2 | Scartare frame UART con stop bit invalido e verificare recupero | VERIFICATO |
| H5 | P1 | Fmax da periodo verificato, gate finale e controllo percorsi/vincoli | PARZIALE |
| S1 | P1 | Ingressi DSL corretti anche per negazione, operandi scambiati e rettangoli | VERIFICATO |
| S2 | P2 | Rifiutare due immediati diversi sullo stesso PE | VERIFICATO |
| S3 | P2 | Parsing rigoroso, step entro protocollo e validazione semantica condivisa | VERIFICATO |
| S4 | P1 | Errori di discovery propagati e parsing senza modifiche parziali | VERIFICATO |
| S5 | P2 | Arity delle pipeline verificata prima dell'esecuzione | VERIFICATO |
| S6 | P1/P2 | Proteggere input/output coincidenti e propagare errori di scrittura | VERIFICATO |
| S7 | P2 | Benchmark falliti espliciti, exit status, JSON valido e oracoli | VERIFICATO |
| C1 | P1 | Trasporto senza accesso oltre FD_SETSIZE | VERIFICATO |
| C2 | P2 | Deadline monotona della risposta, contratto timeout esplicito | VERIFICATO |
| C3 | P2 | Contatori dei byte realmente trasferiti anche sotto guasti | VERIFICATO |
| C4 | P2 | Opcode dei kernel validati e semantica coerente v2/v3 | VERIFICATO |
| C5 | P2 | Dimensioni senza overflow; capacità validate prima di CFG/RUN | VERIFICATO |
| C6 | P2/P3 | API pubblica dei kernel completa; ownership, aliasing, errori e concorrenza | VERIFICATO |
| T1 | P2 | Target analyzer che esegua realmente il passaggio GCC | VERIFICATO |
| T2 | P2 | Standard library trovata dopo installazione con prefisso arbitrario | VERIFICATO |
| T3 | P2 | Regressioni di semantica, guasti, installazione e supporto piattaforme | PARZIALE |
| T4 | P2/P3 | Provenienza artefatti, PDF unico, risultati versionati e licenza | PARZIALE |
| R1 | P1 | Correggere tassonomia output stationary e limiti architetturali | VERIFICATO |
| R2 | P2 | Distinguere PE di calcolo, routing, storage e riduzione host | VERIFICATO |
| R3 | P1/P2 | Allineare affermazioni fisiche e prestazionali alle prove | VERIFICATO |
| R4 | P2/P3 | Correggere formule, conteggi, frecce e timing diagram | VERIFICATO |
| R5 | P2/P3 | Fonte bibliografica unica, DOI e metadati controllati | VERIFICATO |
| R6 | P3 | Migliorare leggibilità del PDF e ridurre ripetizioni | VERIFICATO |
| R7 | P2/P3 | Report in unità atomiche con anteprime isolate per agenti e revisione | VERIFICATO |

## Lavoro già presente e verificato prima di questo ciclo

- Geometria runtime R×C fino a 16 PE, wrapper legacy 4×4 e API con capacità.
- Kernel confrontati con riferimenti scalari su otto geometrie e protocolli v2/v3.
- Replay controller/array e UART rettangolare; test standalone di wiring fino a 8×8.
- Audit: 263 controlli libreria + 77 software e simulazioni esistenti superati.
  Questi esiti non coprono i difetti aggiuntivi trovati nell'audit.
- PDF ricostruibile nel dev shell Nix aggiornato.

## Diario delle correzioni

### 2026-09-19–20 — Primo gruppo verificato

- **H1:** guardia iniziale in `cgra_ctrl.vhd` e assertion reset→primo step
  in `tb_protocol_diff.vhd`, con STEP_DIV 1/2/3/4. Verifica funzionale GHDL;
  resta necessaria una nuova analisi Vivado dei vincoli (H5).
- **H4:** stop bit UART validato; frame invalido/break scartato e recupero
  verificato in `tb_uart.vhd`.
- **S1/S2:** A=north e B=west anche con operandi scambiati, negazione e
  rettangoli; immediato unico del PE validato nei mapping standard e custom.
- **S3, parziale:** step 0..255, literal/override a 16 bit, reset e opzioni
  PE/stage validati. Restano altri campi con `atoi`, troncamenti e variabili.
- **S4:** parsing transazionale, letture file e errori discovery propagati;
  un file invalido non lascia modifiche parziali al contesto.
- **S5:** compilazione preventiva di tutti gli stage e controllo ingressi B
  prima di CFG/RUN; geometria offline di `check` dichiarata (4×4).
- **S6:** output temporaneo fino al completamento del comando; regressioni
  input/output coincidenti e hard link; errori di scrittura propagati.
  La copia finale non è atomica in caso di guasto del filesystem.
- **C1/C2/C3:** `poll`, deadline monotona dell'intera risposta, contatori dei
  byte trasferiti; PTY con descrittore alto, frammentazione e disconnessione.
- **C4/C5:** whitelist degli opcode; capacità/dimensioni controllate prima
  dell'esecuzione; somme/prodotti e indici senza overflow nei casi verificati.
- **T1:** analyzer compila oggetti con `-fanalyzer`, senza `-fsyntax-only`;
  verificato anche che rilevi un dereference NULL intenzionale.
- **T3, parziale:** aggiunte regressioni C, CLI e trasporto; restano installazione,
  altri guasti di protocollo e piattaforme non Linux.

Verifiche superate: `make test` e `make PROFILE=asan test` (**299** controlli
libreria, **19** trasporto PTY, **141** software, **16** CLI, oltre agli smoke
test); `make sim`; analyzer libreria e software. Log conservati in
[`doc/verification/2026-09-20/`](doc/verification/2026-09-20/README.md).
Nessuna misura nuova di area/Fmax o prova su scheda.

### 2026-09-21 — Errori di protocollo e script timing

- **H2 verificato:** rimosso il reset implicito dopo trasferimenti incerti.
  Timeout, I/O e risposte malformate invalidano la cache ID e bloccano la
  sessione. Le chiamate successive restituiscono `CGRA_ERR_DESYNC` senza
  inviare byte. Nessun retry su timeout/checksum errato, nemmeno EXEC con
  reset. Restano retry su NACK completo per CFG/WR e EXEC con reset.
  Il ripristino richiede parser remoto tornato idle e riapertura esplicita;
  chiudere/riaprire da solo non resetta la scheda. Contratto in header/man/info.
- **H3 parziale:** documentati assunzioni e limiti di checksum/framing/retry
  anche nel report LaTeX. PTY verifica RUN/CFG/WR/RD/EXEC incompleti o con
  risposta malformata, assenza di RST/retry, output e contatori preservati.
  Restano matrice completa di corruzione header/opcode, byte inseriti/persi,
  e un eventuale protocollo con framing e sequenze: v2/v3 non li risolvono.
- **H5 parziale:** Fmax calcolata da `1000/periodo` realmente verificato;
  controllo finale dopo phys_opt, setup/hold obbligatori, slack finiti,
  selettori di porte/registri non vuoti, argomenti numerici validati.
  Il CSV nuovo `fmax_validated_history.csv` distingue questi risultati da
  quelli storici; comprende geometria e conserva più cifre del periodo.
  **56 controlli Tcl** esercitano i flussi reali con risposte Vivado simulate,
  inclusi percorsi assenti e fallimento finale. Aggiunti target `test-scripts`
  e passaggio CI. Restano verifica dei selettori e copertura dei vincoli sul
  netlist reale, STA, area e nuove misure Vivado.
- **R3 parziale:** README, descrizione degli script e PERFLOG distinguono ora
  frequenza vincolata, risultato storico e clock fisico. Rimane da correggere
  sistematicamente il resto del report sulle affermazioni fisiche/prestazionali.
- Manuali CLI aggiornati anche sul comportamento dell'output temporaneo (S6).

Verifiche C release e ASan/UBSan superate: **299** libreria, **77** trasporto,
**141** software, **16** CLI; analyzer libreria superato. GHDL del primo gruppo
resta l'evidenza delle modifiche RTL, che questo secondo gruppo non cambia.
Manuali man/info e report PDF (11 pagine) ricostruiti nel dev shell Nix
aggiornato. Il primo tentativo nell'ambiente precedente mancava di `pgfplots`;
`nix develop --command make docs 'LATEXMK=latexmk -g'` ha completato il build.
Restano due overfull hbox già rilevati nell'audit (R6), oltre ai task di
contenuto del report. Il PDF aggiornato è `doc/build/main.pdf`; la copia
`doc/main.pdf` rimane da unificare in T4.
Log e impronte delle sorgenti in
[`doc/verification/2026-09-21/`](doc/verification/2026-09-21/README.md).

### 2026-09-21 — Parsing e benchmark

- **S3, avanzamento:** eliminate le conversioni `atoi` da DSL e CLI.
  Validati baud/timeout, size/repeat/cols, dtype/profili IO e input numerici
  a 16 bit; zero timeout distinto dal valore predefinito. Rifiutati campi
  sovradimensionati, coordinate/token residui, NUL incorporati, variabili
  assenti o malformate e capacità esaurite. `$$` rappresenta un dollaro.
  `CGRA_PATH` non viene più troncato; percorsi troppo lunghi danno errore.
  Il registro dei file è incluso nel rollback, anche per include annidati.
  `check`, `show` e `run` condividono i controlli aggiunti ai selettori output.
  **Residuo:** completare la semantica dei campi non applicabili ai pattern
  dedicati (op/a/b/out possono essere ignorati in alcuni casi) e precisare/
  verificare reset=once per mapping con stato. Il task resta PARZIALE.
- **S7 verificato, con perimetro esplicito:** `bench` e `benchall` usano
  riferimenti scalari indipendenti (`sw/bench_ref.c`) per diagonali senza stato
  e riduzioni su PE(0,0); ogni iterazione misurata viene confrontata fuori
  dall'intervallo cronometrato. Mapping validi senza riferimento sono indicati
  come `skipped`, errori di compilazione/esecuzione o risultati errati come
  `failed`. Nessun modo caricato scompare dal report.
- JSON con escaping e UTF-8 verificati da un parser reale: schema, metadati
  del backend/geometria/protocollo, parametri, mapping e riepilogo degli esiti.
  Totali limitati alle misure riuscite. Un errore restituisce exit nonzero;
  un benchmark singolo privo di riferimento non dichiara successo.
- Report di benchmark completi pubblicati con `-o` anche su fallimento;
  lo script di sweep conserva l'artefatto e propaga l'errore. Errori precedenti
  alla generazione del report preservano il vecchio file. Questa è l'eccezione
  esplicita al comportamento S6, documentata nei manuali.
- **T3:** nuovi test Python standard library (JSON + PTY), aggiunti a `make test`
  e alle dipendenze CI/dev shell. Il peer PTY restituisce una risposta EXEC
  con checksum corretto ma valore errato: il benchmark deve fallire.
  Testate cinque geometrie su entrambi i protocolli, input estremi e script
  di sweep. Il template custom 4×4 è correttamente rifiutato sui rettangoli.

Verifiche release e ASan/UBSan superate: **299** libreria, **77** trasporto,
**169** software, **16** CLI shell e **61** benchmark/CLI Python. Superati
analyzer (incluso il nuovo riferimento scalare), build man/info e diff check.
Esiti e impronte del terzo gruppo in
[`doc/verification/2026-09-21/cli-validation/`](doc/verification/2026-09-21/cli-validation/README.md).
La revisione non modifica RTL; non introduce nuove misure fisiche. I benchmark
precedenti usavano input e intervalli temporali diversi: non confrontarli
numericamente con il nuovo workload senza rieseguire entrambi.

### 2026-09-21 — Semantica dei pattern e installazione

- **S3 completato:** campi ammessi espliciti per ciascun pattern. Rifiutati
  `pe` fuori da custom, `op/a/b` globali per custom, campi computazionali dei
  pattern dedicati, `out` e `reset=each` delle riduzioni. La validazione è
  condivisa da compilazione ed esecuzione e precede CFG/reset/run; i campi
  ereditati dal merge non vengono silenziosamente scartati.
- **S3 reset:** `once` (default) azzera all'inizio di ogni esecuzione non vuota
  diagonal/custom e conserva lo stato tra chunk della stessa chiamata;
  `each` azzera ogni chunk. Le riduzioni azzerano una volta per chiamata.
  Corretti stato residuo tra chiamate e differenza v2/v3 nei diagonali con
  meno step delle lane. Documentati anche input vuoti e reset aggiuntivi dei
  diagonali v3 stateless con risultati completamente assestati.
- **T2 completato:** `include` e `init` usano il percorso stdlib configurato
  da `PREFIX`/`pkgdatadir`; l'header generato cambia quando cambia prefisso,
  senza richiedere un clean. `DESTDIR` resta escluso dai percorsi runtime.
  Quoting dei percorsi di installazione e flag pkg-config corretti; install
  usa artefatti e manuale release anche quando si passa `PROFILE=asan`.
  `init` propaga gli errori di copia/scrittura e la mancanza della libreria;
  un fallimento può lasciare file parziali, come ora dichiarato nei manuali.
- **T3, parziale:** 84 nuovi controlli software (stato iniziale nonzero,
  chiamate ripetute, NOP non letti, once/each e pattern incompatibili), più
  9 controlli d'installazione isolata aggiunti anche alla CI. Verificati
  prefisso con spazi/apostrofo/ampersand, include prima di init, cambio
  prefisso sugli stessi oggetti, staging/deployment e client C esterno
  compilato con header, libreria condivisa e pkg-config installati.
  Restano altre classi di guasto, piattaforme non Linux e verifica del
  pacchetto/check Nix sulle sorgenti finali.
- Manuali man/info e README aggiornati; database clangd rigenerato con
  l'header dei percorsi. Eliminato un warning di conversione nella maschera
  `uint16_t` di matvec emerso dal build con sanitizzatori.

Suite complete release e ASan/UBSan superate: **299** libreria, **77** PTY,
**253** software, **16** CLI shell, **61** benchmark/CLI Python; **9** verifiche
di installazione superate. Dopo la correzione del warning, rieseguiti i 253
test software in entrambi i profili; installazione rieseguita dopo la correzione
del profilo del manuale. Superati build finale, analyzer, manuali e diff check.
Log e impronte in
[`doc/verification/2026-09-21/patterns-install/`](doc/verification/2026-09-21/patterns-install/README.md).
Nessuna modifica RTL o nuova misura fisica. Il reset iniziale aggiunto su v2
cambia i conteggi di traffico: rieseguire le misure prima di riutilizzare
tabelle storiche (R3). L'estensione degli oracoli benchmark a custom/stateful
e kernel dedicati resta un miglioramento successivo, con skip espliciti attuali.

### 2026-09-21–22 — Kernel installabili e contratti C

- **C6 completato:** aggiunte `cgra_matvec`, `cgra_scan` e `cgra_conv` a
  `cgra.h` e alla libreria statica/condivisa. Dimensioni `size_t`, capacità
  d'uscita esplicite, ritorno `CGRA_OK`/errore. I motori sono ora in
  `lib/src/kernels.c`; la CLI conserva soltanto adattatori per i propri
  conteggi e messaggi, senza una seconda implementazione degli algoritmi.
- Controllati footprint, somme/prodotti delle dimensioni e sovrapposizioni
  prima di I/O o scritture dell'output. Vettoriali e scan consentono output
  esattamente coincidente con un input; altre sovrapposizioni sono rifiutate.
  Matvec/conv richiedono output separato dagli input; dot può scrivere il
  risultato dentro un input già consumato. Le capacità dichiarate dal
  chiamante non permettono di verificare la dimensione reale dell'allocazione.
- Input vuoti definiti senza I/O: vector/scan no-op, dot produce zero;
  matvec con zero righe non scrive, con zero colonne produce righe di zeri;
  conv con almeno un input vuoto produce output vuoto. Le operazioni vuote
  richiedono handle/op validi, ma non verificano lo stato remoto.
- Documentati ownership e durata dei buffer/stringhe, chiamate sincrone,
  serializzazione per handle **e per dispositivo fisico**, cache ID e
  riprogrammazione, wrap/shift/ABS, output parziali e stato remoto su errore.
  `cgra_has_exec` può restituire zero anche su errore: per distinguere v2
  si usa `cgra_get_info` controllandone il ritorno prima della versione.
- `cgra_open` imposta/preserva `errno`; `CGRA_ERR_NOMEM` distingue il
  fallimento di allocazione da argomenti invalidi. Il server PTY ora
  propaga gli errori di lettura/setup e ripete le syscall interrotte; la
  callback di disponibilità segue la creazione riuscita dell'emulatore.
  L'avvio del server usa ancora `ptsname`: le istanze concorrenti devono
  stare in processi separati, come dichiarato nell'API.
- **T3 avanzato:** 262 controlli della sola API installata su otto geometrie
  e v2/v3; riferimenti scalari, aliasing, canary, input vuoti e overflow.
  45 controlli PTY aggiunti per guasti a metà kernel e output parziali;
  6 controlli con errori di allocazione/lettura forzati. Il test d'installazione
  compila e collega un client esterno che esegue i tre nuovi kernel su v2/v3.

Suite release e ASan/UBSan complete: **299** controlli libreria esistenti,
**122** PTY, **262** kernel pubblici, **6** guasti forzati, **253** software,
**16** CLI shell e **61** benchmark/CLI Python; **9** verifiche d'installazione.
Superati analyzer libreria/software, man/info, database clangd e diff check.
Il replay GHDL usa i motori spostati nella libreria; evidenze in
[`doc/verification/2026-09-22/c-api/`](doc/verification/2026-09-22/c-api/README.md).
Il test di errori forzati usa wrapping GNU/ELF anche per `__read_chk`, necessario
con Fortify e sanitizzatori. Il nuovo sandbox impediva LeakSanitizer tramite
ptrace: la verifica completa è stata eseguita fuori sandbox dopo autorizzazione.
Nessuna nuova misura fisica; nessuna modifica RTL. Il report LaTeX resta da
rivedere nei task R1–R6. Convoluzione ancora con Toeplitz densa: ottimizzazione
di memoria separata, non implicata dall'esposizione dell'API.

### 2026-09-22–23 — Report, bibliografia e dati riproducibili

- **R1 verificato:** definizione corretta di output stationary (somma locale),
  MAC in PE(0,0) come esempio supportato, limite di forwarding distinto
  dall'impossibilità generale di uno schedule. README e manuale Info allineati.
  Gli immediati possono raggiungere ogni PE tramite CFG: la limitazione agli
  ingressi di bordo riguarda i dati in streaming, non ogni possibile operando.
- **R2 verificato:** matrice-vettore con C moltiplicatori in riga 0,
  (R−1)C celle di ritardo e riduzione host esplicita. Sul 4×4 sono quattro
  moltiplicatori e dodici celle di ritardo, non sedici MAC utili per step.
  Convoluzione ancora con workspace Toeplitz denso, dichiarato nel report.
- **R3 verificato per le affermazioni:** rimossi dal report e dal README i
  risultati Fmax/area/speedup non sostenuti da log della versione attuale.
  Ricerca del periodo, candidato finale, inferenza DSP e clock fisico descritti
  con i rispettivi limiti. La verifica fisica continua a essere aperta in H5.
  Nuovo confronto del codice corrente su v2/v3: cinque workload identici,
  scalar reference, canary, ID escluso, conteggi TX/RX/transazioni archiviati.
  Meno transazioni non implica sempre meno byte né uno speedup misurato.
- **R4 verificato:** formule generalizzate, dot v3 n+3 transazioni, quattro
  byte extra dell'header EXEC e 8680 clock per byte 8N1 a 100 MHz/115200 baud.
  Figure corrette: collegamenti bidirezionali, zeri entranti, accumulatore e
  reset del PE, snapshot sistolico cronologico, capture dopo la guardia reset.
  Diagramma controller sostituito da fasi esplicitamente raggruppate, con
  bypass del calcolo su checksum EXEC errato.
- **R5 verificato:** unica sorgente BibTeX, 23 citazioni risolte; DOI PipeRench
  corretto, HyCUBE come contributo a conferenza, FTDI AN232B-04 pertinente alla
  latenza, versioni dei manuali visibili e URL cliccabili. Archiviate risposte
  Crossref per 13 DOI e fonti primarie per gli altri riferimenti; esplicitate
  le discrepanze dei metadati (anno online/stampa, sottotitoli, paginazione).
  IEEEtran.bst originale distribuito localmente con provenienza/licenza.
- **R6 verificato:** titolo/abstract e sezioni più concisi, tabella delle prove
  con limiti, figure più leggibili, bibliografia bilanciata e metadati PDF
  allineati. PDF di nove pagine ricostruito e controllato visivamente;
  nessuna citazione irrisolta, warning BibTeX o box orizzontale fuori margine.
- **T3/T4 parziali:** build Nix del report superata da snapshot pulito con
  i nuovi file inclusi e Python dichiarato. CSV/JSON con hash dei sorgenti e
  grafico/tabella generati dalla stessa fonte; provenienza dei file IEEEtran
  registrata. Restano pacchetto host/check Nix e altre piattaforme, licenza
  del progetto e trattamento definitivo della vecchia copia `doc/main.pdf`.

Verifiche del gruppo: misurazione ripetuta identica (**10 righe**), formule
indipendenti di transazioni e byte per tutti i kernel, controllo hash dei
sorgenti e rifiuto di CSV alterato, build LaTeX/BibTeX e manuale Info,
build Nix del PDF, controllo visivo e `git diff --check`.
Log e fonti in [`doc/verification/2026-09-22/report/`](doc/verification/2026-09-22/report/README.md).
Il gruppo è iniziato il 22 settembre; non ha modificato il software di
produzione né l'RTL e non ripete come nuovi i 1019 controlli del gruppo C6.

### 2026-09-23 — Modularità LaTeX e primi gate Vivado reali

- **R7 verificato:** `main.tex` solo assemblaggio; front matter e bibliografia
  separati; sezioni indice e file per argomento. Ogni figura ha `diagram.tex`
  e `figure.tex` (didascalia, label, dimensioni); ogni tabella è un'unità.
  `doc/AGENTS.md` e `doc/STRUCTURE.md` indicano dove intervenire e i gate minimi.
- Anteprime indipendenti con la stessa classe/stile del report, includendo le
  didascalie. Target `figure-NAME`, `table-NAME`, `section-NAME` e
  `part UNIT=section/topic`; output separati e compilazione parallela senza
  aux condivisi. `latexmk` segue gli input annidati e salta gli input invariati.
  Le reference esterne usano l'ultima build completa quando disponibile.
- Verificate **8 figure, 5 tabelle, 10 sezioni**, oltre a due topic isolati.
  Il solo spostamento dei file preserva tutti i token di testo del PDF e le
  nove pagine (una differenza di spaziatura). Le successive aggiunte sui
  risultati Vivado sono modifiche di contenuto distinte, dichiarate sotto.
- Build completa LaTeX/BibTeX e pacchetto Nix del report superati da snapshot
  pulito; verificati anche skip incrementale e ricompilazione dell'anteprima
  dopo una modifica al TikZ annidato, senza avviare il report completo.
- **H5 avanzato:** trovato Vivado 2026.1 fuori PATH in `/opt/Xilinx/2026.1`.
  Eseguiti `make synth` e `make sta` con il binario reale, entrambi exit 0,
  Nexys A7 `xc7a100tcsg324-1`, 4×4, STEP_DIV=2, periodo 10 ns, nessun floorplan.
  Synthesis gate: 0 errori e 0 critical warning. STA post-route: **WNS +1.635 ns,
  WHS +0.138 ns**, nessun endpoint interno unconstrained. I numeri non derivano
  dai test Tcl con mock né da GHDL. Nessuna modifica RTL/vincoli in questo ciclo.
- **H5 resta parziale:** un solo periodo/part/geometria, nessuno sweep Fmax
  eseguito, assunzioni delle eccezioni da approfondire e scheda non provata.
  Il report è allineato a questa nuova evidenza. Le risorse archiviate
  (5016 LUT, 1034 registri, 16 DSP48E1) sono del gate synth separato con
  gerarchia conservata, non del netlist routed; nessun delta di ottimizzazione
  attribuito senza baseline controllata.

Evidenze modularità in [`doc/verification/2026-09-23/modularity/`](doc/verification/2026-09-23/modularity/README.md);
versione Vivado, comandi, hash e sette report grezzi in
[`doc/verification/2026-09-23/vivado/`](doc/verification/2026-09-23/vivado/README.md).

### 2026-09-24 — Compatibilità GCC 13 e verifica CI completa

- Riprodotto in Ubuntu 24.04 il fallimento CI del generatore GHDL:
  GCC 13 con `-O2 -Werror` segnalava il buffer `cfg` del mapping diagonale
  come potenzialmente non inizializzato. La verifica geometrica garantiva
  gli elementi trasmessi, ma il compilatore non lo deduceva attraverso i cicli.
- Corretto con inizializzazione esplicita in `lib/src/cgra.c`, commit
  `4025339`; mantenuti `-Werror`, test e controlli. Nessuna modifica RTL.
- Snapshot pulito Ubuntu: `make sim`, **1019** controlli software in release
  e ASan/UBSan, **9** verifiche d'installazione, **56** controlli Tcl e probe
  PTY superati. Ripetuta la misura wire: CSV identico, provenienza aggiornata.
- Su GitHub il [run 35985004700](https://github.com/4137314/ald-cgra/actions/runs/35985004700)
  è concluso con **successo**: `build-and-test` ha superato tutti gli step;
  `nix` ha completato pacchetto host, report e `nix flake check`. Verificato
  Linux x86_64; T3 resta parziale per altre piattaforme e classi di guasto.

Run, riproduzione e log in
[`doc/verification/2026-09-24/ci/`](doc/verification/2026-09-24/ci/README.md).

## Prossime correzioni senza scheda

1. **H3/T3:** completare la matrice di corruzione header/opcode, byte
   inseriti/persi e le verifiche sulle altre piattaforme; aggiungere
   oracoli benchmark per i mapping attualmente esclusi.
2. **Convoluzione:** generare tile implicite senza allocare Toeplitz densa,
   preservando ABI, semantica v2/v3, overflow, errori e confronti scalari.
3. **T4:** un solo PDF distribuito e licenza del progetto da definire con
   l'autore; non dedurre una licenza dai soli metadati Nix.
4. **Toolchain:** il Nixpkgs fissato segnala `texlive.combine` come deprecato
   verso 27.05; migrare prima di aggiornare il lock file. La build corrente
   riesce. H5: estendere i run Vivado oltre il punto 4×4/10 ns appena verificato,
   controllando eccezioni, generici e sweep Fmax finale.

## Ottimizzazioni successive alla correttezza

- [ ] Convoluzione con generazione implicita delle tile, senza Toeplitz densa.
- [ ] Piani compilati riutilizzabili con invalidazione esplicita.
- [ ] Confronto delle geometrie a workload e risorse comparabili.
- [ ] Protocollo oltre 16 PE con lunghezze, capability e framing definiti.
- [x] Primo gate Vivado reale dopo le correzioni: synth e STA 4×4/10 ns/STEP_DIV=2.
- [ ] Estendere misure Vivado a Fmax e configurazioni comparabili; poi prove su scheda.
