# Vivado CLI reale — 23 settembre 2026

Vivado non era nel PATH, ma è installato in
`/opt/Xilinx/2026.1/Vivado/bin/vivado`. I run di questa directory usano quel
binario reale, versione 2026.1, SW build 6511674. Non sono test Tcl con mock.
Le precedenti verifiche funzionali RTL restano GHDL; qui non è stato usato xsim.

## Comandi eseguiti dalla root

```sh
make -C hw -f hw.mk synth VIVADO=/opt/Xilinx/2026.1/Vivado/bin/vivado
make -C hw -f hw.mk sta VIVADO=/opt/Xilinx/2026.1/Vivado/bin/vivado
```

Entrambi terminano con exit 0. Parametri: `BOARD=nexys_a7`,
`xc7a100tcsg324-1`, mesh 4×4, periodo **10.000 ns**, `STEP_DIV=2`,
floorplan disabilitato. Nessuna modifica RTL o ai vincoli per farli passare.

| Gate | Evidenza | Risultato |
|---|---|---|
| Sintesi | [synth-initial.txt](synth-initial.txt) | 0 errori, 0 critical warning |
| Risorse dopo sintesi | [utilization_synth_nexys_a7.rpt](utilization_synth_nexys_a7.rpt) | 5016 LUT, 1034 registri, 16 DSP48E1 |
| STA dopo route | [sta-initial.txt](sta-initial.txt), [timing_summary_nexys_a7.rpt](timing_summary_nexys_a7.rpt) | setup WNS +1.635 ns, hold WHS +0.138 ns |
| Percorsi setup | [timing_setup_nexys_a7.rpt](timing_setup_nexys_a7.rpt) | peggiore: distribuzione reset, requisito 10 ns |
| Percorsi hold | [timing_hold_nexys_a7.rpt](timing_hold_nexys_a7.rpt) | minimo +0.138 ns |
| Copertura di base | [check_timing_nexys_a7.rpt](check_timing_nexys_a7.rpt) | 0 endpoint interni unconstrained, 0 clock mancanti |

Il gate synth usa `flatten_hierarchy none`; il gate STA resintetizza con
l'impostazione default, poi esegue `opt_design`, `place_design`,
`phys_opt_design`, `route_design`. I numeri di risorse nella tabella
appartengono quindi al run separato di sintesi, non al netlist routed.
Le operazioni DSP indicate nel report preliminare sono A*B; il solo conteggio
non dimostra che il post-adder realizzi anche l'accumulo MAC.

## Avvisi e limiti

- Sintesi: avvisi sui bit riservati `cfg[31:26]` non utilizzati dai PE.
- STA: avviso sulla gerarchia poco adatta al floorplanning (non attivato).
- UART e pulsante asincroni, LED e uscita UART restano soggetti alle false path
  esplicite; il check riporta queste eccezioni I/O. L'assenza di endpoint
  interni unconstrained non è una prova generale di correttezza delle eccezioni.
- Verificato un solo part, una geometria e un periodo. Nessuna ricerca Fmax
  eseguita qui, nessun confronto controllato prima/dopo e nessuna misura di
  potenza. Il massimo teorico dei capture è 100 MHz / 2 prima degli overhead.
- Nessun bitstream programmato, collegamento fisico UART o test sulla scheda.
  Il requisito verificato è quello dei vincoli, non una frequenza modificata
  sull'oscillatore. Il task H5 resta parziale per sweep finale e copertura
  approfondita; l'accettazione su scheda rimane distinta.

## Provenienza

[run.json](run.json) contiene parametri, comandi, esiti e hash dei sorgenti.
[sources.sha256](sources.sha256) identifica RTL, XDC e script del working tree;
HEAD è soltanto il commit di base. [reports.sha256](reports.sha256) identifica
i sette report grezzi archiviati. [version.txt](version.txt) registra il tool.
Il checkpoint post-synth rimane un artefatto generato in `hw/build/vivado/`.
