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

## Revisione generale del 25 settembre 2026

[Report completo](doc/review-2026-09-25.md) sul commit `c8a3c41`;
[prove aggiuntive](doc/verification/2026-09-25/review/README.md).
La CI di questo commit è verde. La revisione ha trovato casi non coperti
dalle suite precedenti e definisce i seguenti interventi; gli stati sotto
seguono le successive correzioni e le evidenze del diario.
Le verifiche storiche sopra restano riferite al loro perimetro; per esempio
A01 estende S6 alla matrice comando/opzione, A10 estende gli oracoli di S7.

| ID | Priorità | Intervento / criterio di chiusura | Stato |
|---|---|---|---|
| A01 | P1 | `--out` coerente per tutti i comandi ammessi; nessun file svuotato da uno stream inutilizzato; regressioni | VERIFICATO |
| A02 | P2 | `.pc` coerente con `libdir`/`includedir`; client esterno con directory custom e DESTDIR | VERIFICATO |
| A03 | P1 | README/commenti hardware coerenti con protocollo, geometria e prove; nessuna garanzia di funzionamento derivata dalla sola STA | DA FARE |
| A04 | P1 | `ASYNC_REG`, verifica netlist/placement e copertura dei vincoli con Vivado reale; estensione H5 | DA FARE |
| A05 | P1/P2 | Manifest e pubblicazione per run Vivado; programmazione associata all'artefatto verificato | DA FARE |
| A06 | P2/P3 | PDF di consegna unico, commit/manifest e bibliografia ribilanciata dopo layout finale; estensione T4/R6 | DA FARE |
| A07 | P2 | Arity/opzioni CLI, help/version, completion e limiti shell verificati; opzioni output completate | VERIFICATO |
| A08 | P2 | Separazione dei domini CLI e contesto di invocazione; definire estensioni diagnostica/deadline mantenendo ABI | VERIFICATO |
| A09 | P2 | Convoluzione senza Toeplitz densa; oracoli/guasti e confronto memoria/tempo host | VERIFICATO |
| A10 | P1/P2 | Benchmark dei kernel mancanti, artifact per tutto lo sweep, baseline e metadati | VERIFICATO |
| A11 | P2 | Matrice requisiti/test, confini ALU/UART e guasti protocollo sistematici; estensione H3/T3 | DA FARE |
| A12 | P2 | CI per domini, GCC/Clang, artifact e `checks` Nix; supporto piattaforme esplicito | VERIFICATO |
| A13 | P2/P3 | Manuali/esempi coerenti, claim→fonte, integrità CSV distinta da attualità dei sorgenti | DA FARE |
| A14 | P2 | Guida AGENTS nella radice, caricamento verificato in Codex/Claude e indice operativo breve | PARZIALE |
| A15 | P2 | Confronto forme a 16 PE con workload/risorse comparabili e run Vivado selezionati | PARZIALE |
| A16 | P2 | Licenza scelta dall'autore e metadati Nix coerenti; estensione T4 | PARZIALE |
| A17 | P2 | Compilation database JSON valido con percorsi speciali e flag coerenti con la build | VERIFICATO |
| A18 | P2 | Oggetti invalidati quando cambiano compiler/flag nello stesso profilo | VERIFICATO |

Il ciclo di revisione ha prodotto analisi, task ed evidenze, senza correzioni al codice
di produzione o nuovi risultati Vivado. I probe hanno riprodotto A01, A02,
A17 e parte di A07 in percorsi temporanei. Il report distingue difetti,
limiti già dichiarati e proposte sperimentali.

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

### 2026-09-25 — Output CLI, installazione e compilation database

- **A01 verificato, A07 parziale:** i comandi di consultazione scrivono sullo
  stream scelto da `--out`; gli operativi/interattivi rifiutano l'opzione prima
  dell'esecuzione. Validata la compatibilità di `--io`, `--json`, `--dtype`;
  errori di `check` su stderr. Restano arità, altre opzioni, help/version,
  completion e limiti shell in A07. Manuali aggiornati.
- **A02 verificato:** pkg-config segue `libdir`/`includedir` personalizzati,
  mantenendo i default relativi al prefisso ed escludendo DESTDIR. Client C
  esterno compilato/eseguito con soli flag pkg-config, anche con spazi,
  apostrofi e ampersand nei percorsi.
- **A17 verificato:** JSON con array di argomenti, flag esportati dai Makefile,
  header coerente col profilo e pubblicazione solo dopo generazione riuscita.
  Verificate 13 unità host in release/ASan, eseguendo tutti i comandi in
  syntax-only; preservato il database precedente su merge fallito. Nuovo
  target `test-compdb` incluso nella CI. A18 resta da fare: questo intervento
  non invalida gli oggetti di build al cambio di compiler/flag.
- Riprodotti i tre difetti con nuove regressioni **prima** delle correzioni;
  dopo, **1049** controlli software release e ASan/UBSan, **12** installazione,
  **121** compilation database superati. LSan nella sandbox fallisce per
  ptrace: conservato il log e ripetuta l'intera suite fuori sandbox con i
  controlli attivi, esito 0.
- Manuali man/Info e PDF costruiti; ripetuta la misura wire, **CSV identico**,
  metadati aggiornati per il Makefile libreria modificato. Nessuna modifica
  RTL o nuovo run Vivado.

Commit delle correzioni: `25c01d1`, `7491f55`, `8d1e5c8`.
Evidenze in [`doc/verification/2026-09-25/fixes/`](doc/verification/2026-09-25/fixes/README.md).

## Consolidamento e formalizzazione del 27 settembre 2026

L'incarico continua A03–A16/A18; A01, A02 e A17 vanno preservati.
Gli ID nuovi sotto coprono soltanto requisiti aggiuntivi. Nessun push,
release o programmazione della scheda è autorizzato da questo ciclo.

| ID | Intervento / criterio di chiusura | Stato |
|---|---|---|
| F01 | Toolchain fissata, prova del PE VHDL di produzione, proprietà presenti nel solver e controllo negativo raggiungibile | VERIFICATO |
| F02 | PE completo a 16 bit e wiring/sincronia array sulle istanze dichiarate, cover e mutazioni mirate | VERIFICATO |
| F03 | Controller: RUN/EXEC, indici, STEP_DIV 1/2/3/4, handshake, watchdog con ipotesi esplicite | PARZIALE |
| F04 | UART TX/RX nel modello digitale dichiarato, prove e cover | VERIFICATO |
| F05 | Target fast/full/cover, manifest JSON, matrice requisiti–test–proprietà–evidenza e CI/Nix eseguiti | VERIFICATO |
| B01 | Contratto sperimentale a blocchi versionato, CRC/framing/sequence e deduplicazione delimitata | DA FARE |
| B02 | Memoria locale e sequencer RTL, modello, API e CLI con workload end-to-end e regressioni legacy | DA FARE |
| B03 | Prove buffer/sequencer, misura traffico/round trip, sintesi reale e limiti fisici documentati | DA FARE |
| C07 | Deadline dell'intera transazione, diagnostica additiva, CLOEXEC/apertura esclusiva opzionale | VERIFICATO |
| C08 | Server PTY configurabile/arrestabile e piani riutilizzabili con invalidazione esplicita | PARZIALE |

### Checkpoint 2026-09-27 / 01 — baseline completata

- HEAD iniziale `dc886d2376c2662a798c65ac86d0e97938ceb3fb`, working tree
  inizialmente pulito; nessun rollback al riferimento storico.
- Eseguiti con esito 0: `make -j4 test`, `make -j4 PROFILE=asan test`
  (LSan attivo), `make test-install`, `make test-compdb`, `make sim`,
  `make -C hw -f hw.mk test-scripts`, `make docs`,
  `make -C lib -f lib.mk analyze`. GCC 15.2.0, GHDL 6.0.0.
- Conteggi correnti: **1049** controlli software per profilo, **121** compdb,
  **12** installazione, **56** Tcl. Non sono percentuali di copertura.
- Riprodotti prima delle modifiche: `ping -d sim unexpected` e
  `run add --a 1 --b 2 -d sim --cols 4` accettati; cambio a
  `CC=/nonexistent/cgra-review-cc` dichiara erroneamente valida la libreria.
- Evidenze immutabili, comandi, versioni, hash e risultati in
  `doc/verification/2026-09-27/baseline/`. Vivado 2026.1 disponibile fuori PATH;
  nessun nuovo run fisico ancora eseguito.
- Prossimo passo: F01, acquisizione della combinazione formale dal nixpkgs
  già fissato, prova minima e controllo negativo prima degli interventi RTL.

### Checkpoint 2026-09-28 / 02 — frontend formale e build incrementale

- F01: acquisiti dal lock esistente GHDL 6.0.0, plugin
  `1b97dc71377cea7e861be6625be4353c377a5fb5`, Yosys 0.67, SBY 0.61,
  Yices 2.7.0. SBY Nix stampa `unknown version`: versione dal derivation,
  senza inventare l'output dell'eseguibile.
- Sul PE di produzione: reset, hold, NOP e CONST **PROVED** per induzione;
  4 assertion e 1 assumption utente presenti nell'SMT. Proprietà falsa:
  **FAIL** raggiungibile con trace; cover dell'uscita non nulla raggiunto.
  Manifest e log in `doc/verification/2026-09-28/formal-smoke/`.
- Specifica bit-vector indipendente dei 16 opcode: primo tentativo gerarchico
  **TIMEOUT** a 120 s; flatten/wreduce/techmap/opt (nessuna riduzione del
  dominio) consente **PROVED** per induzione in 5 s, profondità base 4.
  Integrazione runner/target e proprietà controller ancora in corso.
- A18: firma effettiva di compiler/flag/linker/dipendenze opzionali/percorsi
  generati; invariata se gli input restano uguali. La regressione ha trovato
  inoltre la propagazione accidentale dei flag CLI alla libreria quando
  CFLAGS proviene dall'ambiente; isolati ora i flag originali del chiamante.
  Recipe ricorsive marcate per conservare il jobserver anche in modalità pipe.
- `make test-build`: **27** verifiche superate in copia isolata, incluse
  installazione e compdb, compiler inesistente, rebuild necessario e no-op.
  Log del fallimento intermedio e del run corretto conservati in
  `doc/verification/2026-09-28/build/`. Nessun push/programmazione.

### Checkpoint 2026-09-28 / 03 — CLI, prove reali e convoluzione

- **A07 verificato:** tabella comandi/arità/opzioni prima degli effetti,
  help/version, completion e shell con rifiuto integrale di righe/token
  eccessivi, NUL e virgolette aperte. **47** regressioni aggiuntive, inclusa
  sessione terminale PTY; fallback senza readline provato (42 controlli prima
  delle ultime cinque verifiche completion/PTY). A01 preservato.
- **A08 parziale:** main ridotto al punto d'ingresso; contesto esplicito,
  parsing/dispatch, vettori, benchmark, discovery e shell separati. Restano
  deadline/diagnostica in C07 e piani/server in C08.
- **F01 verificato, F02/F03/F05 parziali:** full PE a 16 bit, mux e 16 opcode
  **PROVED**; controller 1x1 DIV1/2/3/4 e cinque rettangoli fino a 16 PE
  **PROVED** sulle proprietà descritte in `hw/formal/README.md`. Full usa
  watchdog di produzione sui rettangoli. Negative control e due mutazioni
  temporanee **FAIL atteso** con tracce. Cover entro 32 cicli: **BOUNDED_PASS**,
  sola raggiungibilità. Mancano array, EXEC completo, UART e liveness.
- Eseguiti `formal-fast/full/cover` e `nix build
  .#checks.x86_64-linux.formal-fast --no-link`. Tre run accettati archiviati
  con sorgenti effettivi in `doc/verification/2026-09-28/formal/archive/`.
  Conservati anche timeout/errori intermedi; scoperti e corretti frontend
  che ignorava generici fuori posizione e sostituzione fra vunit multiple.
- **A09 verificato:** tile implicite limitate alla banda, nessun workspace
  Toeplitz. 50 punti prima/dopo, v2/v3 e cinque geometrie; byte/transazioni/
  step identici. Per n=4096,k=5 heap temporaneo 33.587.200 -> 0 byte; mediana
  API sul modello 4x4 v3 104,037 -> 2,156 ms. Non sono prestazioni FPGA.
- Dopo A09: **1240** controlli release e ASan/UBSan con LSan, GHDL completo
  superato. Prima di A09: compdb **171**, installazione **12**, build **27**,
  analyzer GCC15 CLI superati. Non sono percentuali di copertura.
- Manuali e PDF costruiti; pagina convoluzione renderizzata e controllata.
  Radice AGENTS aggiunta; uso da parte di Claude non ancora verificato.
  Rimossa attribuzione MIT non supportata dai metadati Nix; licenza da autore.
- Prossimo: diagnostiche GCC13.3 riprodotte sul commit storico in copia
  isolata, poi C07/C08, benchmark completi e ampliamento delle prove RTL.
  Nessun nuovo run Vivado, push o programmazione in questo checkpoint.

### Checkpoint 2026-09-29 / 04 — trasporto C e server PTY

- **C07/A08 verificati:** API additive per budget monotono della transazione
  (scrittura, risposta e retry), diagnostica copiata con fase/errno/progresso,
  CLOEXEC e apertura esclusiva opzionale. Timeout storico resta per risposta;
  un kernel composto non acquisisce implicitamente una deadline globale.
- **C08 parziale:** server configurabile v2/v3/geometria e cancellazione anche
  con TX bloccato; CLI `emulate --rows --cols --protocol` con stop pulito su
  SIGINT/SIGTERM. Corretto overflow della coda del modello per 100 ID ricevuti
  consecutivamente. Il modello senza clock non verifica il watchdog RTL.
  Restano i piani riutilizzabili e il contratto di configurazione residente.
- Riprodotti su sorgenti storici GCC 13.3 `-Wstringop-overflow` e falso percorso
  analyzer su g.rows. Limiti di serializzazione espliciti R+C<=MAX_PE+1 e
  separazione dei kernel vettoriali: GCC 13.3 release e due analyzer passano,
  senza soppressioni. Conversioni/shift del modello C resi definiti in C11.
- **1323** controlli release e ASan/UBSan con LSan, GCC15 analyzer libreria e
  GHDL completo: PASS con manifest e hash invariati durante ciascun gate in
  `doc/verification/2026-09-28/transport/`. Il test iniziale di backpressure
  aveva una race nella preparazione della PTY: fallimento conservato, setup
  corretto e gate ripetuti; nessun allentamento della deadline in produzione.
- GCC13.3 software/analyzer, Clang22 software, manuali/PDF, installazione **12**,
  compdb **179**, build incrementale **27**: PASS, manifest separati in
  `doc/verification/2026-09-29/`. Primo tentativo Docker bloccato dal socket
  sandbox conservato; successiva esecuzione autorizzata separata.
- F02 in corso: PE e array1x1 monolitici PROVED; array2x3 monolitico TIMEOUT
  a 90 s, poi PROVED con composizione. Il contratto PE è provato sul vero RTL
  nello stesso run, a 16 bit e su tutti gli opcode; l'array importato conserva
  il wiring e usa quelle garanzie come assumption esplicite. Nessuna riduzione
  del dominio. Full/cover e mutazione nord avviati prima di chiudere F02.
- Nessun push, release, programmazione o nuovo risultato Vivado. Prossimi:
  completare F02, UART/EXEC, piani, benchmark e blocchi; le fasi restanti
  dell'incarico originale restano tutte attive.

### Checkpoint 2026-09-29 / 05 — array e UART formali

- **F02/F04 verificati nel perimetro dichiarato:** suite full con **23 task
  PROVED** e **6 FAIL attesi** con controesempi; cover con obbligo PE PROVED
  e **10 BOUNDED_PASS** di raggiungibilità. Sorgenti/monitor/log/tracce archiviati
  in `doc/verification/2026-09-29/formal/`, inclusi tentativi falliti.
- Array: otto forme enumerate; 1x1 monolitica, altre con composizione del
  contratto PE provato sullo stesso RTL nello stesso run. Domini 16 bit e
  opcode completi. Non è una prova parametrica di tutte le geometrie.
- UART: TX/RX PROVED a divisori 3 e 868; cover sul divisore ridotto. Nuovo
  generatore digitale indipendente in GHDL, fase variabile e periodo ±2%,
  frame ravvicinati, glitch/stop/break/reset; 77 controlli dati/guasti per
  divisore 16 e 868, più assertion valid. Nessuna garanzia analogica.
- Matrice requisiti–test–proprietà–evidenza in `doc/verification-matrix.md`.
  F03 resta parziale per risposta EXEC completa e liveness condizionata.
- **A12 in corso:** job CI separati e check Nix eseguibili aggiunti; derivazioni
  in esecuzione, nessun successo dedotto dalla sola configurazione. Supporto
  dichiarato limitato a x86_64 Linux; matrice compiler CI GCC13/Clang18.
- Restano attivi benchmark CLI/piani, protocollo a blocchi/memoria locale,
  nuovi run Vivado e allineamento documentale. Nessun push o programmazione.

### Checkpoint 2026-09-30 / 06 — benchmark e gate Nix eseguiti

- **A10 verificato:** matvec/scan/conv tramite API pubbliche, riferimenti scalari
  indipendenti, esempio custom riconosciuto per struttura; mapping arbitrari
  senza oracolo restano skip. Un warm-up verificato e campioni API/CPU con
  mediana/p95. Profilazione additiva opzionale separa I/O CFG, altro I/O e
  restante tempo host; byte/transazioni/step richiesti/operazioni scalari distinti.
- Schema JSON 2 con hash delle sorgenti, flag/compiler effettivi, piattaforma,
  parametri e campioni. Ogni punto dello sweep è eseguito una sola volta e
  conservato con hash/exit status; test conta le invocazioni reali. Report falliti
  integri; nessun nuovo report conserva l'indice precedente.
- **40 punti CLI PASS** su cinque forme a 16 PE e v2/v3; dataset con estremi e
  zeri dichiarato distinto dagli input positivi della revisione. In particolare
  matvec16x16/16x1 trasferisce 11565 byte, saltando un peso nullo: non forzato a
  12336, già riprodotto con gli input originali nel confronto A09.
- Release e ASan/UBSan/LSan: **1413 controlli PASS**. Primo tentativo sanitizer
  fallito per ptrace della sandbox, conservato; run fuori sandbox passato con
  LSan attivo. Analyzer GCC, **12 install / 179 compdb / 27 build**, docs PASS.
- **A12 verificato localmente:** job separati software GCC13/Clang18, HDL/Tcl,
  formal, documenti e Nix; actionlint PASS. Tutte e cinque le derivazioni Nix
  correnti eseguite: pacchetto, software/analyzer/fallback senza readline,
  GHDL/Tcl, formal-fast a 11 task, PDF. Hash degli input immutabili dello store
  in `doc/verification/2026-09-30/benchmark/nix/`. Nessuna pretesa di esecuzione
  remota GitHub Actions su modifiche non pubblicate. Piattaforma x86_64 Linux.
- GCC13.3 e Clang18.1.3 eseguiti anche su Ubuntu 24.04: **1413 controlli
  per compiler/profilo**, release e ASan/UBSan/LSan; analyzer GCC13 entrambi
  PASS. Compiler-rt esplicito nell'immagine e in CI, ID immagine conservato.
- Evidenze e riproduttore dei punti in `doc/verification/2026-09-30/benchmark/`.
  F03, C08 piani, A04/A05 fisico e B01–B03 blocchi restano aperti. Si procede
  con commit locali coesi e successivi interventi; nessun push/programmazione.
- Commit locali: `00e44e2` build, `3754afd` convoluzione, `bff5c57` trasporto,
  `df3504b` CLI, `7f2340c` benchmark, `b9ec58f` formale/CI. I primi quattro
  gruppi verificati anche estraendo l'esatto indice Git in copie temporanee;
  evidenze in `doc/verification/2026-09-30/atomic/`.

## Prossime correzioni senza scheda

1. **F03/F05/C08:** completare EXEC/liveness e piani riutilizzabili;
   preservare CLI e deadline già verificate nei checkpoint 03–04.
2. **A03/A06/A14:** allineare README/commenti alle evidenze, definire il PDF
   di consegna e le istruzioni comuni per gli agenti.
3. **A04/A05/H5:** dichiarare i sincronizzatori, controllare il netlist reale
   e rendere gli artefatti Vivado identificabili per run.
4. **H3/T3/A10/A11/A12:** completare la matrice di corruzione header/opcode, byte
   inseriti/persi e le verifiche sulle altre piattaforme; aggiungere
   oracoli benchmark per i mapping attualmente esclusi.
5. **A15:** completare confronto area/timing con Vivado sulle geometrie;
   benchmark CLI e misure software sono chiusi in A09/A10.
6. **T4/A16:** licenza del progetto da definire con
   l'autore; non dedurre una licenza dai soli metadati Nix.
7. **Toolchain:** il Nixpkgs fissato segnala `texlive.combine` come deprecato
   verso 27.05; migrare prima di aggiornare il lock file. La build corrente
   riesce. H5: estendere i run Vivado oltre il punto 4×4/10 ns appena verificato,
   controllando eccezioni, generici e sweep Fmax finale.

## Ottimizzazioni successive alla correttezza

- [x] Convoluzione con generazione implicita delle tile, senza Toeplitz densa.
- [ ] Piani compilati riutilizzabili con invalidazione esplicita.
- [ ] Confronto delle geometrie a workload e risorse comparabili.
- [ ] Protocollo oltre 16 PE con lunghezze, capability e framing definiti.
- [x] Primo gate Vivado reale dopo le correzioni: synth e STA 4×4/10 ns/STEP_DIV=2.
- [ ] Estendere misure Vivado a Fmax e configurazioni comparabili; poi prove su scheda.
