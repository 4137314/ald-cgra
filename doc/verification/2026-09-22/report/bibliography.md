# Controllo bibliografia — 22 settembre 2026

Il report usa una sola fonte, `doc/bib/bib.bib`, elaborata con BibTeX e
`doc/IEEEtran.bst` (versione 1.14 originale). Il duplicato manuale `bib.tex`
è stato rimosso. I DOI sono conservati anche come URL per essere stampati
come collegamenti dallo stile IEEEtran. Numero/versione dei manuali sono
riportati anche in `note`, perché lo stile non stampa `number` per `manual`.

## Articoli con DOI

[crossref.json](crossref.json) archivia i metadati restituiti dalle API Crossref,
depositati dagli editori: 13 DOI risolti. Il controllo riguarda identità,
autori disponibili, sede, titolo/sottotitolo, anno e pagine disponibili.
Non equivale a una revisione integrale dei risultati di ogni pubblicazione.
Le liste complete di autori nei metadati non vengono abbreviate nel log.

| Chiave | DOI controllato | Nota |
|---|---|---|
| cgra-survey | 10.1145/3357375 | Titolo e sottotitolo separati nei metadati; anno online 2019 |
| adres | 10.1007/978-3-540-45234-8_7 | Contributo FPL in LNCS; 61–70 |
| morphosys | 10.1109/12.859540 | IEEE TC 49(5), 465–481 |
| tpu | 10.1145/3079856.3080246 | ISCA 2017, 1–12 |
| plasticine | 10.1145/3079856.3080256 | ISCA 2017, 389–402 |
| kung | 10.1109/MC.1982.1653825 | Computer 15(1), 37–46 |
| eyeriss | 10.1109/JSSC.2016.2616357 | JSSC 52(1), 127–138; anno del fascicolo 2017 |
| sze-survey | 10.1109/JPROC.2017.2761740 | Proc. IEEE 105(12), 2295–2329 |
| dyser | 10.1109/HPCA.2011.5749755 | HPCA 2011, 503–514 |
| hycube | 10.1145/3061639.3062262 | Corretto in `inproceedings`, DAC 2017 |
| cgra-me | 10.1109/ASAP.2017.7995277 | Autori verificati: include Allan Rui |
| piperench | 10.1109/ISCA.1999.765937 | Sostituisce il DOI errato 10.1145/300979.300982 |
| bhasker-timing | 10.1007/978-0-387-93820-2 | Libro Springer, completato con la pagina dell'editore |

Caveat dei metadati: Crossref indica 2019 per la pubblicazione online di
`cgra-survey` e 2020 per la stampa; si mantiene l'anno online. Le pagine
ACM sono numerate localmente negli endpoint (`1–39`, `1–6`), mentre la
bibliografia conserva la notazione con numero articolo (`118:1`, `45:1`).
Il sottotitolo Crossref di Plasticine contiene il refuso “Paterns”; il titolo
bibliografico mantiene “Patterns”. Non sono errori da copiare automaticamente.

[La pagina dell'autore di PipeRench](https://www.cs.cmu.edu/~seth/papers/goldstein-isca99.html)
conferma autori, ISCA e pagine, ma mostra anche un evidente anno 1990
incompatibile con il DOI e l'edizione: non è stato importato.
[La pagina Springer del libro STA](https://link.springer.com/book/10.1007/978-0-387-93820-2)
conferma titolo, autori e 2009; le intestazioni dei capitoli mantengono
l'ordine J. Bhasker, Rakesh Chadha utilizzato in bibliografia.

## Altre fonti e manuali

| Chiave | Fonte primaria consultata | Controllo |
|---|---|---|
| kung-leiserson | [Pubblicazioni Kung](https://eecs.harvard.edu/htk/publications/) e [capitolo dell'autore](https://www.eecs.harvard.edu/~htk/publication/1984-vlsi-for-pattern-recognition-and-image-processing.pdf), rif. 2.2 | Autori, titolo, SIAM 1979, curatori Duff/Stewart, 256–282; distinta dalla versione technical report |
| blelloch | [CMU](https://www.cs.cmu.edu/afs/cs.cmu.edu/project/scandal/public/papers/CMU-CS-90-190.html) | CMU-CS-90-190, novembre 1990 |
| golub-vanloan | [Johns Hopkins University Press](https://www.press.jhu.edu/books/title/10678/matrix-computations) | Autori, quarta edizione, 2013 |
| stevens-apue | [Addison-Wesley / InformIT](https://www.informit.com/store/advanced-programming-in-the-unix-environment-9780321638007) | Autori, terza edizione, 2013 |
| ghdl | [Repository ufficiale](https://github.com/ghdl/ghdl) | Titolo e progetto; rimossi anno 2024 non motivato e vecchia data di accesso |
| artix7 | [AMD DS180](https://docs.amd.com/api/khub/documents/2LByHkO~nSZXcei2D55fTg/content) | v2.6.1, 8 settembre 2020 |
| ug479 | [AMD UG479](https://docs.amd.com/api/khub/documents/gu4oRPFEh_Pm2uaAlfY6Kg/content) | v1.10, 27 marzo 2018 |
| ug903 | [AMD UG903, versione 2022.2](https://docs.amd.com/r/2022.2-English/ug903-vivado-using-constraints/Multicycle-Paths) | Titolo, versione e uso dei multicycle subordinato al controllo dei capture |
| ug949 | [AMD UG949, versione 2022.2](https://docs.amd.com/r/2022.2-English/ug949-vivado-design-methodology/When-and-Where-to-Use-a-Reset) | Titolo e versione; non usato per promettere miglioramenti di timing |
| ftdi-latency | [Catalogo FTDI](https://ftdichip.com/document/application-notes/) e [AN232B-04](https://ftdichip.com/wp-content/uploads/2024/03/AN232B_04_DataThroughput_LatencyandHandshaking.pdf) | v1.2, 18 marzo 2024; riferimento pertinente per buffering/latenza, sostituisce FT2232H |

Per R1 la tassonomia è stata confrontata anche con il
[manoscritto degli autori Sze et al.](https://eems.mit.edu/wp-content/uploads/2017/03/2017_arxiv_sze.pdf),
sezione sui dataflow: in output stationary il partial sum resta locale.
Il report non attribuisce al TPU una specifica tassonomia non dimostrata e
non deduce l'impossibilità generale dell'output stationary sul progetto.

Le verifiche dei link non sono un requisito di rete della build: il report
si ricostruisce dai file locali. Alcuni viewer AMD/FTDI non hanno restituito
il testo tramite apertura diretta; le versioni dei PDF sono state riscontrate
nelle pagine/anteprime del produttore. Nessun testo integrale di articolo o
manuale è redistribuito nel repository.
