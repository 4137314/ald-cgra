# Verifiche report e dati — 22–23 settembre 2026

Gruppo R1–R6, iniziato il 22 settembre e chiuso il 23.
[Registro task](../../../../TASKS.md). Il software di produzione e l'RTL non
sono cambiati in questo gruppo; le suite del gruppo C6 non vengono dichiarate
come rieseguite. Il runner C aggiunto serve esclusivamente all'esperimento.

## Comandi ed esiti

| Evidenza | Comando dalla root / contenuto | Esito |
|---|---|---|
| [measure.log](measure.log) | `make -C doc -f doc.mk measure` | exit 0, dieci righe verificate |
| [checks.log](checks.log) | Secondo run di `doc/build/measure_wire`, confronto CSV, hash sorgenti, formule e rifiuto di CSV alterato | superato |
| [build.log](build.log) | `make doc` | exit 0, nove pagine |
| [latex.log](latex.log) | Log finale pdfLaTeX | nessun riferimento irrisolto o box orizzontale fuori margine |
| [bibtex.log](bibtex.log) | BibTeX automatico | 23 riferimenti, zero warning |
| [info.log](info.log) | `make -C sw -f sw.mk doc` | exit 0 |
| [nix-build.log](nix-build.log) | `nix build --no-link --print-out-paths path:SNAPSHOT#doc` | exit 0 da copia senza artefatti generati |
| [pdfinfo.txt](pdfinfo.txt) | `pdfinfo doc/build/main.pdf` | titolo/metadati aggiornati, nove pagine |
| [environment.txt](environment.txt) | Versioni compilatore/Python/TeX/Nix | archiviate |
| [bibliography.md](bibliography.md) | Fonti primarie, correzioni e caveat dei metadati | 13 DOI in [crossref.json](crossref.json), 23 voci totali |

La copia Nix contiene `flake.nix`, `flake.lock` e tutti gli input del report
(main.tex, class/style, doc.mk, config/src/figures/bib/data/tools), senza
`build/`. È necessaria per includere anche i file nuovi non ancora aggiunti a
Git: non è stato fatto staging. La derivazione dichiara Python, usato dal
renderer, e non avvia misure C durante la build del PDF.

Il log Nix segnala `texlive.combine` deprecato nel nixpkgs fissato, da migrare
prima di un futuro aggiornamento. La build attuale riesce; il pacchetto host
e le altre piattaforme non sono verificati da questo esperimento.

Rimane un avviso `Underfull \vbox` dovuto all'impaginazione verticale.
Le pagine sono state renderizzate con `pdftoppm` e ispezionate: diagrammi,
formule, tabelle e bibliografia sono leggibili, senza contenuto tagliato.
Il trigger IEEE di bilanciamento va ricontrollato dopo modifiche consistenti.

## Ambito delle misure

[`doc/data/wire-cost.csv`](../../../data/wire-cost.csv) contiene cinque kernel
sul 4×4, ciascuno in v2 e v3. Il runner usa esclusivamente l'API pubblica,
confronta tutti gli output con riferimenti scalari e controlla un canary.
Ogni riga include CFG e calcolo di una singola chiamata, esclude l'ID iniziale
e non contiene retry. Gli input sono fissi e definiti in
[`measure_wire.c`](../../../tools/measure_wire.c).

Il renderer verifica conteggi analitici di transazioni e byte, compresi i tile
nonzero della Toeplitz, e l'hash del CSV. Il test di integrità ha alterato in
una copia temporanea il rapporto TX/RX mantenendo il totale invariato: le
formule restavano soddisfatte, ma l'hash ha correttamente rifiutato il dato.

[`wire-cost.json`](../../../data/wire-cost.json) registra compiler, timestamp,
commit di base, hash del runner/binario e dei sorgenti della libreria. Il
commit da solo non identifica il working tree. Gli hash sono stati confrontati
con i file correnti e un secondo run ha riprodotto esattamente tutte le righe.
Tabella e grafico del report derivano dallo stesso CSV.

Questi conteggi non misurano serializzazione reale, latenza USB, throughput
sulla scheda o vantaggio rispetto alla CPU. Dot, matvec densa e convoluzione
usano meno transazioni ma più byte in v3: il report descrive anche questo costo.

## Correzioni documentate

- Output stationary definito come accumulo locale; forwarding indipendente
  e tassonomia del dataflow distinti. MAC locale già possibile sul progetto.
- Ruoli dei PE e riduzione host espliciti; immediati caricabili via CFG anche
  all'interno della mesh. La convoluzione resta densa nella memoria host.
- Fmax storiche e risultati privi di raw report esclusi dalle prove attuali;
  limite del passo, vincoli e clock fisico distinti.
- Formule e diagrammi corretti, compresi reset/accumulatore del PE, capture
  dopo guardia iniziale, bypass EXEC rifiutato e snapshot dei prodotti.
- Una sola bibliografia, DOI PipeRench corretto, HyCUBE conferenza, FTDI
  pertinente alla latenza, versioni dei manuali visibili. Provenienza e licenza
  dei file IEEEtran in [`THIRD_PARTY.md`](../../../THIRD_PARTY.md).

`sources.sha256` identifica gli input del report e della misura, i manuali
aggiornati e il registro task. `artifacts.sha256` identifica PDF e asset
risultanti. Il PDF ricostruito resta `doc/build/main.pdf`; la vecchia copia
tracciata `doc/main.pdf` non è stata sovrascritta in questo gruppo (T4).
