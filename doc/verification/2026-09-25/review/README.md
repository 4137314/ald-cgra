# Evidenze della revisione generale

Data: **25 settembre 2026**. Sorgenti: commit
`c8a3c412d62e2f71a7cf2929c3d30cad9cc1a979`.
Report: [review-2026-09-25.md](../../../review-2026-09-25.md).

## Nuove prove

```sh
python3 doc/verification/2026-09-25/review/probes.py
make doc
```

`probes.py` è un raccoglitore di osservazioni, **non una suite che considera
corretto il comportamento osservato**. Usa il binario già costruito in
`sw/build/cgra`, ne registra l'hash e impiega file temporanei. L'installazione
avviene in una copia temporanea dei sorgenti, senza modificare la build
dell'utente o installare nel sistema.

| Caso | Risultato osservato |
|---|---|
| `ping -d sim -o FILE` | Exit 0; risultato su stdout; file preesistente svuotato |
| `show add -d sim -o FILE` | Exit 0; risultato su stdout; file preesistente svuotato |
| `ping -d sim unexpected` | Argomento aggiuntivo ignorato, exit 0 |
| `run add ... --cols 4` | Opzione inapplicabile ignorata, risultato 3, exit 0 |
| `--help` | Exit 0, tutto su stderr |
| `--version` | Opzione sconosciuta, exit 1 |
| Compilation database in directory con `"` | Script exit 0, JSON invalido |
| Installazione con `lib64` e `include-custom` | Installazione exit 0 e file corretti; pkg-config indica invece `lib` e `include` |

Output completi e versioni: [probes.json](probes.json).
Log della build/installazione isolata: [install-probe.txt](install-probe.txt).
Codex CLI 0.157.0 e Claude Code 2.1.282 rilevati in questa esecuzione.
Il warning Codex sulla creazione di alias PATH deriva dai permessi della
sessione; il comando `--version` è terminato con codice 0.

## Evidenza CI

Stato riletto con:

```sh
gh run view 35985319703 --json databaseId,headSha,status,conclusion,url,jobs \
  --jq '{id: .databaseId, head: .headSha, status, conclusion, url, jobs: [.jobs[] | {name, conclusion}]}'
```

Output in [ci-status.json](ci-status.json): commit analizzato corrispondente,
run concluso e job `build-and-test`/`nix` entrambi `success`.
Il primo tentativo dalla sandbox non raggiungeva api.github.com; la lettura
è riuscita con il permesso di rete richiesto tramite il tool.

Questa lettura non ha avviato nuove pipeline. Log delle suite e riproduzione
GCC 13 sono in [../2026-09-24/ci](../../2026-09-24/ci/README.md).
Le nuove osservazioni dimostrano limiti di copertura della suite verde.

## Documenti e fonti

Le nove pagine di `doc/build/main.pdf` sono state renderizzate con `pdftoppm`
e ispezionate. È stato osservato lo sbilanciamento finale della bibliografia;
le figure e tabelle risultano generalmente leggibili. Il PDF storico
`doc/main.pdf` resta distinto e non è stato sovrascritto.

La build completa finale è registrata in [doc-build.txt](doc-build.txt);
[document-checks.json](document-checks.json) registra pagine, hash PDF e
diagnostiche rilevanti dei log LaTeX/BibTeX. Non sono stati modificati i
sorgenti LaTeX in questa revisione.

`sources.sha256` identifica i file analizzati di implementazione, build e
documentazione. Le fonti esterne primarie consultate sono collegate accanto
alle affermazioni nel report (AMD, GNU, documentazione Codex/Claude).

Non sono state ripetute qui le suite complete C/GHDL o la sintesi/STA: si
usano le evidenze esistenti, esplicitamente datate. I probe aggiuntivi e la
build del PDF sono le nuove esecuzioni di questo ciclo.
