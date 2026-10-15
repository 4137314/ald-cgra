# Rapporto di revisione del manoscritto — 15 ottobre 2026

Manoscritto: *A Configurable CGRA Mesh on FPGA: Design and Pre-Silicon Evidence*
(PDF di riferimento: `doc/review/reference-2026-10-15.pdf`, copia di `doc/main.pdf`,
SHA-256 `997fe01a…b89581`). Titolo dopo la revisione: *Communication Costs and
Verification of a Step-Controlled FPGA CGRA*.

I numeri di sezioni, figure e tabelle seguiti da **[rif.]** si riferiscono al PDF di
riferimento; quelli seguiti da **[riv.]** al PDF revisionato.

## 1. Perimetro e stato iniziale

- HEAD `0f8a54bce20506c141f18f90ac98aec04fc04191`, ramo `main`; 85 voci in
  `git status` all'inizio. Le modifiche staged preesistenti (harness formali,
  `hw.mk`, `tb_block_faults.vhd`, `block-protocol.md`, `verification-matrix.md`,
  `TASKS.md`) non sono state toccate. Il confronto `git diff --cached --stat`
  finale è identico a `baseline-staged.txt`.
- Stato iniziale salvato in `baseline-git-status.txt`, `baseline-staged.txt`,
  `baseline-unstaged.txt` e `baseline-sha256.txt`, con l'hash di 12 053 file
  regolari (esclusi `.git`, directory `build/` e symlink).
- Istruzioni lette: `AGENTS.md` (radice) e `doc/AGENTS.md`; non esistono file
  `CLAUDE.md`. Il paper si trova in `doc/` (`main.tex`, `src/`, `figures/`,
  `tables/`, `config/`, `bib/`).
- Build del solo documento: `make -C doc -f doc.mk`. Il target scrive soltanto
  in `doc/build/`, consuma i dati archiviati e ne riverifica gli hash.
- **File modificabili**: sorgenti LaTeX (`doc/main.tex`, `doc/supplement.tex`,
  `doc/config/*.tex`, `doc/frontmatter/`, `doc/backmatter/`, `doc/src/**`,
  `doc/figures/*/{diagram,figure}.tex`, `doc/tables/*/table.tex`),
  `doc/bib/bib.bib`, script documentali (`doc/tools/analysis_data.py`,
  `doc/tools/measure_analysis.c`), `doc/doc.mk`, nuovi dati con provenienza
  in `doc/data/analysis/` e la directory di revisione `doc/review/`.
- **Protetti**: tutto il resto, incluse le directory `hw/`, `lib/`, `sw/` e `scripts/`, i
  file Nix e CI, `TASKS.md`, `doc/*.md`, `doc/README`, `doc/STRUCTURE.md`, i README
  delle sottodirectory, `doc/verification/**`, `doc/data/{wire-cost,scaling}.*`,
  `doc/data/board/*`, `doc/IEEEtran.*`, `doc/tools/{preview.py,wire_data.py,
  scaling_data.py,model_data.py,board_data.py,measure_*.c esistenti}` e `doc/main.pdf`.
- **Verifica finale del perimetro**: `perimeter-check.txt` riporta 61 file
  modificati, aggiunti o rimossi, tutti nel perimetro, e **0 fuori perimetro**.
  `doc/main.pdf`, i CSV/JSON archiviati, l'RTL, i testbench, i constraint, gli
  script hardware, il software e l'infrastruttura sono identici alla baseline.
  Fa eccezione un elemento non regolare: il symlink ignorato da Git
  `.direnv/flake-profile` è stato ricreato alle 15:35 (verosimilmente dall'hook
  direnv/`nix develop` dell'ambiente) e punta allo stesso profilo di prima. Non è
  un sorgente del progetto.
- File rimossi: `doc/src/analysis/roofline.tex` e `doc/figures/roofline/`, che avevo
  creato nella sessione precedente (non tracciati) e che contenevano la
  convenzione errata discussa al §4.1.

## 2. Comandi eseguiti ed esiti

| Comando | Esito |
|---|---|
| `nix develop .#formal -c python3 hw/formal/run.py --suite full --out <scratch>/full-20261015` | exit 0; suite completa; 38 PROVED, 10 FAIL negativi/mutazioni come richiesto, 0 UNKNOWN |
| `… --suite cover --out <scratch>/cover-20261015` | exit 0; 18 BOUNDED_PASS + 1 PROVED; acceptance PASS |
| `python3 hw/scr/artifacts.py sta --period 10 --step-div 2 --out <scratch>/sta10` (Vivado 2026.1) | PASS, nove gate su nove; WNS +1.862, WHS +0.098; 4999/4971 LUT; 800/256 registri ed endpoint |
| `python3 hw/scr/artifacts.py sta --period 5.859 … --out <scratch>/sta5859` | PASS; WNS +0.177, WHS +0.122; 4999/4975 LUT; periodo applicato 5.859 ns (170.678 MHz) |
| `cc … doc/tools/measure_analysis.c` contro una libreria compilata in isolamento dai sorgenti `lib/src` | exit 0; output identico con `lib/build/libcgra.a` |
| `make -C doc -f doc.mk measure-analysis` | exit 0; ha registrato `doc/data/analysis/counts.csv` (`735f6d81…`) e `provenance.json` |
| `make -C doc -f doc.mk` (più volte, fino alla versione finale) | exit 0; 11 pagine; nessun riferimento indefinito, nessun overfull, 0 warning BibTeX |
| `make -C doc -f doc.mk supplement` | exit 0; 4 pagine; nessun riferimento indefinito, nessun overfull |

Le directory delle run (oltre 680 MB) sono rimaste nella scratchpad di sessione.
In `doc/review/evidence/` ho archiviato manifest, `result.json`, log, `.sby`
e proprietà effettive, più un `archive-index.json` con dimensione e SHA-256 di
ogni file originale. Il criterio è quello dell'archivio di progetto: modelli,
checkpoint e bitstream sono omessi ma conservano l'hash.

Hash dei manifest archiviati:
- formal full `9dd7b57b…`
- formal cover `fa8bf35e…`
- STA 10 ns `1707f3e1…`
- STA 5.859 ns `a240c7de…`

Tutte le run hanno registrato commit `0f8a54b`, `dirty=true` e gli hash per
file. Per le due STA, ogni file VHDL, XDC e Tcl usato coincide con la
revisione corrente.

## 3. Problemi trovati

### 3.1 Errori dimostrati (corretti nel testo)

1. **Roofline incoerente** ([rif.] §VII-C, Fig. 8). Il tetto contava 1 operazione
   per PE e per step (800 M/s), l'intensità contava le operazioni scalari (MAC = 2).
   La frase "four orders of magnitude" era inoltre imprecisa: il divario reale
   andava da 4.06 a 5.94 ordini. *Correzione*: ho eliminato la roofline e
   introdotto convenzioni esplicite ([riv.] §IV-B: slot PE, operazione PE utile,
   operazioni scalari, lavoro host). Al suo posto c'è un modello temporale
   esplicito con il termine device `S·D/f_clk` calcolato dagli step contati
   ([riv.] Eq. 2, Fig. 7). Il beneficio marginale del clock è ora quantificato
   (≤ 2.8·10⁻⁵ del tempo di serializzazione) invece di essere negato in assoluto.
2. **Fig. 9 [rif.] mal interpretata**: la grandezza chiamata "useful PEs per step"
   era in realtà l'occupazione statica dei PE aritmetici. Verifica sul codice
   (`lib/src/vector.c`, `lib/src/kernels.c`): la diagonale produce L risultati in
   L step e lo scan C risultati in C step con ricomputazione. Su una 4×4 si ottiene
   η = 4/(16·4) = 1/16, mentre l'occupazione statica è 1/4. *Correzione*: in
   [riv.] Fig. 8 i pannelli sono separati (η dagli step contati, occupazione
   statica, byte) e il testo ne trae le conclusioni. Il risultato corretto del
   matvec (C moltiplicatori e (R−1)C celle di ritardo) è conservato.
3. **Provenienza delle run Vivado legacy.** Il bitstream gate del 5/10, lo sweep
   geometrico e la ricerca di periodo del 6/10 usavano un `cgra_top.vhd`
   precedente: confronto degli hash dei manifest con i file correnti, con
   `cgra_top.vhd` diverso in tutte quelle run. Il PDF [rif.] li presentava come
   risultati della revisione corrente. Con `G_BLOCK_ENABLE=false` il nuovo
   `generate` è puro cablaggio (letto in `git diff edddace 0f8a54b --
   hw/rtl/cgra_top.vhd`). *Azione*: ho rieseguito le STA a 10 e 5.859 ns sulla
   revisione corrente; le metriche risultano identiche. Il paper distingue ora
   le colonne C ed E ([riv.] Tab. IX). Lo sweep di cinque forme e la ricerca di
   periodo restano evidenza della revisione E e non sono stati ripetuti.
4. **Risultati formali sommati** ([rif.] §X-B): una suite completa precedente, una
   estesa con UNKNOWN e un successo isolato venivano presentati insieme. *Azione*:
   ho rieseguito le suite `full` e `cover` complete sui sorgenti correnti; il
   paper riporta solo questa esecuzione ([riv.] Tab. VIII) e la cronologia
   passa al supplemento.
5. **Rapporto di transazioni nell'abstract precedente**: 2.0–4.0×; quello esatto
   è 1.99–3.95× (515/259, 257/65).
6. **"Exactly-once"** usato senza condizioni. *Correzione*: "at-most-once effects
   with response replay", con le condizioni elencate (una richiesta outstanding,
   cache solo della precedente, sequenza senza wraparound, perdita al reset,
   nonce non garantito unico, nessun retry automatico dell'API).
7. **Retry**: il testo diceva "retries disabled". In realtà la libreria ritrasmette
   dopo un NACK completo (default 3) e i driver verificano che non avvenga alcuna
   ritrasmissione. Testo corretto.
8. **Attribuzione dell'area** dell'estensione (buffer, cache, CRC): non c'è un
   report gerarchico negli archivi. Il testo dichiara la composizione per
   costruzione e la ripartizione come non misurata.
9. **Affermazione sulla geometria** "shape changes communication much more than
   useful work": falsa per il matvec (η varia di 16×, i byte di circa 10×).
   Corretta.

### 3.2 Ambiguità risolte

- **5017 vs 4999 LUT post-sintesi**: il primo valore viene dalla coppia di sintesi
  dell'8/10 sui sorgenti correnti con `flatten_hierarchy none`, il secondo dalla
  strategia di default (5/10 e run di oggi). Verificato sui report grezzi;
  dichiarato nella didascalia di [riv.] Tab. VII.
- **BRAM**: i due banchi logici 1024×16 corrispondono a 2 RAMB18, conteggiati
  come 1 "Block RAM Tile" (`utilization_*.rpt` dell'8/10).
- **Post-sintesi vs post-route** separati: 8459/4348 contro 8164/4346 con i
  blocchi abilitati.
- **Byte residenti dipendenti dai dati** (byte stuffing): 1 857 B con i nuovi
  operandi contro 1 856 B nell'archivio del 7/10; dichiarato nel paper.
- **Conteggio dei launch register**: 800 = 256 (registri PE) + 416 (CFG, 16×26)
  + 64 (west) + 64 (north); 833 con i registri a blocchi. Ricavato da
  `cgra_datapath_registers` in `hw/scr/constraints.tcl` e dalle metriche dei
  manifest.

### 3.3 Problemi di implementazione o progetto (non corretti: fuori perimetro)

- **Evidenza software non allineata.** I 3 519 controlli (7–8/10) usano una
  `sw/cli.c` e un `tb_blocks.vhd` precedenti alla revisione corrente; `lib/` è
  identica. Impatto: il paper la dichiara come archivio "un passo indietro" per
  la CLI. Non l'ho rieseguita, perché `make test` scrive in `lib/build` e
  `sw/build`, aree non isolate.
- **Repository remoto**: `origin/main` è fermo a `dc886d2`, precedente alla
  revisione valutata, e il repository non ha licenza. Impatto: la dichiarazione
  di disponibilità è onesta ma oggi non soddisfa i requisiti di un artefatto
  pubblicabile.
- **Documenti di progetto non aggiornati per scelta di perimetro**: `TASKS.md`,
  `doc/STRUCTURE.md`, `doc/figures/README`, `doc/tables/README`,
  `doc/verification-matrix.md`. Esempio: la matrice di verifica cita ancora
  l'UNKNOWN del 9/10 senza la nuova suite completa.
- **Unità LaTeX non più incluse**, lasciate intatte: `src/{protocol,software,
  analysis,implementation,blocks}.tex` e parte di `src/{protocol,dataflow,
  analysis,implementation,results,blocks,software}/*`, `tables/{evidence,
  geometry}`, `figures/board/figure.tex`. Il loro contenuto corretto è stato
  ripreso nel paper o nel supplemento; restano come sorgenti orfani da riordinare.

### 3.4 Controlli ancora necessari (non eseguiti)

- Misure su scheda: τ, tempi di chiamata, baseline CPU, potenza, recupero.
  Tutto NOT RUN.
- Sweep geometrico e ricerca Fmax sulla revisione corrente (eseguita solo la
  4×4 legacy) e più placement per forma, se si vogliono conclusioni sul timing.
- Corrispondenza formale tra registri RTL e celle del netlist (oggi garantita
  solo per nome).
- Riesecuzione di `make test` e `make sim` sui sorgenti esatti in ambiente
  isolato.

### 3.5 Miglioramenti editoriali

Ho introdotto struttura problema → RQ → contributi → related work → sistema →
modello → metodologia → risultati → limiti → disponibilità → conclusioni. Ho
tolto il tono da relazione di corso; l'affiliazione è ora solo "University of
Trento", perché "Advanced Logic Design" è un corso. Ho ridotto l'abstract e
spostato nel supplemento FSM, esempio sul filo, stack software, diagramma di
timing, grafici ridondanti (rapporti v3/v2, sweep geometrico), piano su scheda
con la figura NOT RUN e cronologia delle run. Le vecchie Tabelle I e VII
([rif.]) sono fuse in una sola ([riv.] Tab. V, con colonna di revisione).

## 4. Matrice delle affermazioni (estratto dei numeri principali)

| Affermazione [riv.] | Fonte | Revisione / config | Esito | Limite residuo |
|---|---|---|---|---|
| Tab. VI: 193→65, 515→259, 257→65, 648→264, 336→144 transazioni; 3651→2115, 5735→6247, 3779→2115, 8472→8728, 5040→5168 B | `doc/data/wire-cost.csv` (`86a76683…`) e nuova misura `doc/data/analysis/counts.csv` (`735f6d81…`) | lib corrente; 4×4; input in didascalia | Verificato: valori identici nelle due misure; formule chiuse controllate dagli script | Solo conteggi |
| Fusione: −1.99…−3.95× transazioni; +2.5…+8.93% byte | Stessi CSV | idem | Verificato (8.93% = 6247/5735) | Non sono speedup |
| τ* = 173.6, 57.9, 57.9 µs | Eq. 3 calcolata da `analysis_data.py` sui CSV | t_b = 10/115200 | Verificato (173.611; 57.870) | Ipotesi di τ uguale per ogni comando |
| Termine device ≤ 2.8·10⁻⁵ | step contati (`requested_steps`) | 4×4, D=2, 100 MHz | Verificato | Esclude i cicli del controller fuori dagli step (stima per ispezione) |
| η = 1/16 per vector/dot/scan; matvec 1/R | step contati + mapping in `vector.c`/`kernels.c` | 5 forme | Verificato (formule chiuse degli step asserite) | Problem size fisse |
| Byte per forma (2 115 contro 11 331, ecc.) | `counts.csv` | v3, 5 forme | Verificato | Mapping non ottimizzati per forma |
| Residenza r=8, n=256: 49 976 / 10 874 / 6 368 / 1 857 B; 2 072 / 82 / 54 / 26 transazioni | `counts.csv` (nuovo driver) | 4×4 | Verificato; formule chiuse delle transazioni asserite | Microbenchmark di ammortamento; byte dipendenti dai dati |
| 26.91× byte (−96.28%), 79.69× transazioni | idem | idem | Verificato | Non speedup |
| +68.6% LUT, +320.5% FF, 2 RAMB18 = 1 tile | report grezzi `synth-20261008T091642Z`, `…092712Z` | sorgenti correnti, `flatten_hierarchy none` | Verificato su `utilization_*.rpt` | Nessuna ripartizione per modulo |
| Post-route con blocchi: WNS +0.427, WHS +0.060, 8 164 LUT | manifest `sta-20261008T091011Z` | hash = revisione corrente | Verificato | Una sola placement |
| 4×4 legacy 10 ns: +1.862 / +0.098; 4 999/4 971 LUT | **nuova run** `sta-20261015T151823Z` | corrente | Eseguito ora | — |
| 5.859 ns: +0.177 / +0.122 | **nuova run** `sta-20261015T152452Z` e archivio `fmax-20261006` | corrente ed E | Eseguito ora; il report riporta 5.859 ns / 170.678 MHz | Ricerca non ripetuta; non è un massimo |
| Sweep cinque forme (Tab. IX, righe E) | manifest `geometry-physical` del 5/10 | E (`cgra_top` precedente) | Controllato in archivio | Non rieseguito; una placement per forma |
| 800 launch register / 256 endpoint | `constraints.tcl` + metriche dei manifest | 4×4 | Verificato | Corrispondenza RTL↔netlist solo per nome |
| Tab. VIII formale: 38 PROVED, 10/10 FAIL attesi, 18 cover | **nuova run** full/cover | corrente (incl. harness staged) | Eseguito ora | Istanze enumerate; liveness solo condizionata |
| Controller: 0 assunzioni; block controller: solo reset iniziale; count ≤ 1024 assunto dal solo sequencer | `assumptions` per task nel manifest full | corrente | Verificato | — |
| 504 transazioni di replay, 1 024 risultati PE, 886 eventi | manifest GHDL 8–9/10 | RTL e testbench con hash correnti (differiscono solo harness formali e `hw.mk`) | Controllato in archivio | Non rieseguito ora |
| 3 519 controlli software | `2026-10-07/block-release`, `2026-10-08/block-asan` | lib corrente, CLI precedente | Controllato in archivio | Non rieseguito |
| Related work (CGRA-ME ISPD'18, QuickDough, OpenEdgeCGRA, Aspros et al.) | Fonti primarie consultate il 15/10 (DOI 10.1145/3177540.3177553; PDF FPT'15; repository GitHub; arXiv 2504.01672v3) | — | Verificato sulle fonti indicate | Caratteristiche non documentate → "not reported" |

## 5. Distinzione delle evidenze

- **Eseguito ora**: suite formali `full` e `cover`; due STA Vivado legacy 4×4;
  nuove misure di conteggio (geometrie, step, scenari di riuso); build e
  ispezione visiva di paper e supplemento.
- **Controllato in archivio**: suite GHDL; suite software; coppia di sintesi e
  STA con blocchi; sweep geometrico e ricerca di periodo; misure del 7/10. Per
  ciascuno ho confrontato gli hash registrati con i file correnti.
- **Non eseguito**: misure su scheda, potenza, ripetizioni di placement,
  equivalenza formale RTL↔netlist, riesecuzione dei test software/GHDL.

## 6. Giudizio per la submission

Il manoscritto è ora coerente con le evidenze e in diversi punti più solido di
prima: risultati formali e STA rieseguiti sulla revisione corrente, convenzioni
di conteggio esplicite, metriche corrette. **Non è pronto per la
pubblicazione.** Blocchi principali:

1. **Nessuna misura fisica.** La tesi su RQ1 resta condizionata alla
   distribuzione di τ. Senza almeno τ misurato e tempi di chiamata su scheda,
   il contributo è un modello verificato con conteggi esatti, accettabile solo
   per un workshop o un formato "pre-silicon/artifact".
2. **Artefatto non pubblicabile così com'è**: revisione valutata non pubblicata
   (commit locale più modifiche non committate), nessuna licenza, nessun DOI di
   archivio. Va risolto dall'autore.
3. **Novità limitata.** Il contributo è metodologico e di accounting su un
   design volutamente minimale; manca un confronto quantitativo con un secondo
   sistema o con un link diverso.
4. **Generalità**: un solo dispositivo e una sola placement per forma; proprietà
   formali su istanze enumerate; corrispondenza RTL↔netlist per nome.
5. **Venue**: il template IEEE conference è stato mantenuto. Nessun limite di
   pagine noto (11 pagine più 4 di supplemento); se la venue impone 6–8 pagine,
   serve un ulteriore taglio.

## 7. Pubblicazione (aggiunta successiva alla revisione)

Su richiesta dell'autore, il lavoro è stato committato e pubblicato.
- `doc/main.pdf` ora contiene il PDF revisionato; la versione del 15/10
  precedente resta in `reference-2026-10-15.pdf`.
- Il supplemento compilato è in `doc/supplement.pdf`.
- Le 37 unità LaTeX orfane elencate al §3.3 sono state rimosse; il loro
  contenuto corretto è nel paper, nel supplemento e nella storia Git.
- `doc/STRUCTURE.md` e i README di `src/`, `figures/` e `tables/` sono stati
  allineati alla nuova struttura.
- Lo strumento di anteprima ora gestisce `table*`.
- Verifiche finali: tutte le anteprime (`make -C doc -f doc.mk -j4 figures
  tables sections`), il paper e il supplemento compilano senza warning.
- `hw/sim/gen_protocol_faults.c` e `hw/sim/tb_protocol_faults.vhd` (lavoro in
  corso A11/H3, non collegato a `hw.mk`) restano non tracciati.
