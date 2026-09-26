# Correzioni host successive alla revisione

Data: **25 settembre 2026**. Base della revisione: `c8a3c41`;
report/registro iniziale: `8437afd`.
Ispezione editoriale finale e ricontrollo hash/link completati il **26 settembre**.

| Task | Commit | Correzione |
|---|---|---|
| A01, parte di A07 | `25c01d1` | Stream esplicito per consultazione/ping/show, validazione delle opzioni output prima degli effetti, diagnostica check su stderr |
| A02 | `7491f55` | Directory effettive in pkg-config, default relativi al prefisso, esclusione di DESTDIR |
| A17 | `8d1e5c8` | Compilation database dai flag Make, JSON con `arguments`, header per profilo e pubblicazione con sostituzione finale |

## Regressioni prima e dopo

Le nuove regressioni sono state eseguite contro il codice precedente prima
delle correzioni. I log non sono stati sostituiti con gli esiti successivi.

| Caso | Prima | Dopo |
|---|---|---|
| Output CLI | [cli-before.txt](cli-before.txt): confronto fallito, file di destinazione vuoto | [cli-after.txt](cli-after.txt): 46 controlli; suite completa finale sotto |
| Directory installazione | [install-before.txt](install-before.txt): pkg-config non indica i percorsi scelti | [install-after.txt](install-after.txt): 12 controlli, incluso client C esterno |
| Percorso checkout con virgolette | [compdb-before.txt](compdb-before.txt): JSONDecodeError | [compdb-after.txt](compdb-after.txt): 121 controlli, 13 unità C per ciascuno dei due profili |

La regressione CLI confronta stdout e file per i comandi ammessi, verifica che
le combinazioni vietate preservino il file e impediscano l'esecuzione, controlla
errori di scrittura e diagnostica. I test benchmark verificano anche la
pubblicazione di report completi con esito fallito.

L'installazione usa una copia dei sorgenti e directory temporanee con spazi,
apostrofi e ampersand. Il client esterno è compilato con i soli flag pkg-config
ed esegue kernel su v2/v3. Il secondo prefisso usa directory personalizzate e
staging DESTDIR, poi viene distribuito e provato senza fallback ai sorgenti.

Il database viene letto con un parser JSON e tutti i suoi comandi sono eseguiti
in modalità **syntax-only**, su release e ASan. Questo verifica flag, quoting,
header e risoluzione degli include; non costituisce un'esecuzione sanitizer.
Le esecuzioni sanitizer del software sono registrate separatamente sotto.

## Gate finali locali

| Comando | Esito | Log |
|---|---|---|
| `make test` | 0; 1049 controlli, oltre agli smoke test | [test-release.txt](test-release.txt) |
| `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 make PROFILE=asan test` | 0 fuori sandbox; stessi 1049 controlli | [test-asan-unrestricted.txt](test-asan-unrestricted.txt) |
| `make test-install` | 0; 12 controlli | [install-after.txt](install-after.txt) |
| `make test-compdb` | 0; 121 controlli | [compdb-after.txt](compdb-after.txt) |
| `make compdb` | 0 anche nel workspace | [compdb-workspace.txt](compdb-workspace.txt) |
| `make docs` | 0; PDF, man(1/3/5), Info | [docs.txt](docs.txt) |
| `make -C doc -f doc.mk measure` | 0; CSV identico, provenienza aggiornata | [wire-data.txt](wire-data.txt) |
| `make doc` dopo aggiornamento conteggi/caption | 0 | [doc-final.txt](doc-final.txt) |

I 1049 controlli comprendono 299 libreria, 122 trasporto, 262 kernel pubblici,
6 guasti iniettati, 253 DSL/compiler, 46 CLI e 61 benchmark/CLI. Il conteggio
descrive i casi registrati e non una percentuale di copertura.

Il primo tentativo sanitizer, in [test-asan.txt](test-asan.txt), si interrompe
per l'incompatibilità dichiarata da LeakSanitizer con `ptrace` nella sandbox.
È stato conservato e seguito dalla riesecuzione completa fuori sandbox:
nessuna disattivazione di LSan, ASan o UBSan. Gli errori di pipeline intenzionale
visibili su stderr nei log CLI sono casi negativi attesi, con suite exit 0.

Il nuovo target `test-compdb` è incluso nella CI. I risultati remoti sono
consultabili nel run GitHub Actions del commit finale; gli esiti della tabella
qui sopra sono quelli locali e non vengono presentati come un nuovo run CI.

## Provenienza e documenti

- [sources.sha256](sources.sha256) identifica sorgenti e configurazioni del
  gruppo; [checks.json](checks.json) registra verifica hash, diagnostiche PDF
  e corrispondenza dei sorgenti alla misura wire.
- `doc/data/wire-cost.csv` è rimasto identico. La misura è stata ripetuta
  esplicitamente perché `lib/lib.mk`, incluso negli hash di provenienza, cambia.
- Nel report LaTeX sono aggiornati soltanto i conteggi software/installazione
  e il periodo delle evidenze nella caption della tabella. Il PDF finale è
  stato ricostruito e le pagine interessate ispezionate.
- Il report generale e i log della revisione precedente restano storici.
  `TASKS.md` contiene gli stati aggiornati: A01/A02/A17 verificati, A07 parziale.
- Nessuna modifica RTL/vincoli e nessun nuovo risultato GHDL/Vivado locale
  attribuito a questo gruppo. A18 (invalidazione oggetti al cambio flag) resta
  aperto; la generazione del database non modifica quella dipendenza di build.
