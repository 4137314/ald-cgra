# Modularizzazione del report — 23 settembre 2026

Il report usa file per argomento, figure con disegno e contenitore separati,
tabelle autonome e un preambolo condiviso. La mappa operativa è in
[`doc/STRUCTURE.md`](../../../STRUCTURE.md); le istruzioni di manutenzione
sono in [`doc/AGENTS.md`](../../../AGENTS.md). Il task è R7 in `TASKS.md`.

## Verifiche eseguite

Comandi dalla root della repository, salvo dove indicato nei log.

| Verifica | Comando o metodo | Evidenza |
|---|---|---|
| Conservazione del contenuto durante l'estrazione | Confronto dei token di `pdftotext -layout` prima/dopo; nove pagine, sola spaziatura diversa | [equivalence.txt](equivalence.txt), [report.txt](report.txt) |
| Tutte le anteprime | `make -C doc -f doc.mk -j2 figures tables sections`: 8 figure, 5 tabelle, 10 sezioni | [previews.txt](previews.txt) |
| Singolo argomento | `make -C doc -f doc.mk part UNIT=protocol/fused-execute` | [part.txt](part.txt) |
| Nuovo paragrafo Vivado e sezione contenente | `make -C doc -f doc.mk section-implementation part UNIT=implementation/validation` | [vivado-unit.txt](vivado-unit.txt) |
| Build incrementale | Seconda invocazione di `figure-pe`, nessuna compilazione LaTeX necessaria | [incremental.txt](incremental.txt) |
| Indipendenza e dipendenze annidate | Snapshot senza PDF/aux del report: `figure-pe` compila; una modifica al suo `diagram.tex` fa ricompilare; ripetere senza modifiche salta pdfLaTeX | [isolated-preview.txt](isolated-preview.txt) |
| Report finale | `make doc`, dopo l'aggiunta dei risultati Vivado | [report-final.txt](report-final.txt), [latex.txt](latex.txt), [bibtex.txt](bibtex.txt) |
| Tabella aggiornata | `make -C doc -f doc.mk table-verification` | [table-final.txt](table-final.txt) |
| Pacchetto Nix | `nix build --no-link --print-out-paths path:SNAPSHOT#doc`, input copiati esplicitamente, senza artefatti di build | [nix.txt](nix.txt) |

Tutte le verifiche finali sono riuscite. Controllate visivamente le anteprime
PE, FSM e tabella dei comandi, e le pagine finali del report. I log finali
LaTeX/BibTeX non presentano riferimenti/citazioni irrisolti o box fuori margine.
Il PDF corrente è `doc/build/main.pdf`; `doc/main.pdf` resta la copia storica.

La prova di equivalenza riguarda il solo spostamento dei sorgenti. La successiva
aggiunta dei risultati Vivado modifica intenzionalmente il contenuto: i run
effettivi e i rispettivi limiti sono in [`../vivado/`](../vivado/README.md).
Questo ciclo non modifica C, RTL o vincoli e non ripete i precedenti test
funzionali come nuove evidenze.

## Tentativi iniziali e limiti

- [preview-smoke.txt](preview-smoke.txt) conserva il tentativo iniziale:
  bibliografia vuota nelle unità senza citazioni e riferimenti sovrascritti
  dagli hook di hyperref. Entrambi corretti prima delle verifiche finali.
- [nix-initial-snapshot-error.txt](nix-initial-snapshot-error.txt) conserva
  il fallimento del primo snapshot di prova: un filtro ricorsivo ometteva
  anche `tables/verification`. Il copia-snapshot è stato corretto; il build
  finale include questa directory. Non era un errore del target del report.
- Le anteprime importano facoltativamente i riferimenti dall'ultimo
  `build/main.aux`; senza una build completa possono comparire `??` per
  riferimenti esterni. La numerazione locale non è quella di pubblicazione.
  Una richiesta di anteprima non avvia una build completa del report.
- Il Nixpkgs fissato segnala la deprecazione futura di `texlive.combine`;
  la build attuale riesce. La migrazione è registrata in `TASKS.md`.

## Provenienza

[versions.txt](versions.txt) registra i tool usati. [sources.sha256](sources.sha256)
identifica gli input finali del report e le istruzioni di build/manutenzione;
i percorsi sono relativi alla root. [artifacts.sha256](artifacts.sha256)
identifica il PDF finale e i dati generati (artefatti in `doc/build/`).
Gli hash della prova Vivado sono separati e identificano il codice hardware
effettivamente misurato, non il solo commit di base del working tree.
