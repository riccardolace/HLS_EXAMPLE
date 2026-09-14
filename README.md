# axis_scaler — una IP AXI4-Stream costruita in Vitis HLS

Progetto didattico: si costruisce, un pezzo per volta, una IP con interfaccia
dati **AXI4-Stream** e banco registri **AXI4-Lite** (configurazione, stato,
controllo, interrupt), fino a farla entrare nel catalogo IP di Vivado come una
IP AMD.

**Stato attuale: gradino 1.4 — primo registro di stato.**

---

## Quickstart

```bash
make workspace     # crea il workspace Vitis e il component HLS
make gui           # apre la GUI:  vitis -w workspace
make csim          # simulazione C  (equivale a C SIMULATION > Run nella GUI)
make csynth        # sintesi        (equivale a C SYNTHESIS  > Run)
make entity        # stampa la entity VHDL generata
```

`make` senza argomenti elenca tutti i bersagli.

Il flusso normale di lavoro è: **si lavora nella GUI**, e il `Makefile` serve per
rifare tutto da zero, per i log completi e per capire quale comando corrisponde a
quale bottone.

---

## Mappa del progetto

```text
src/         sorgenti che diventano hardware  (sintetizzati)
tb/          testbench, gira solo sul PC      (non sintetizzato)
scripts/     creazione del workspace Vitis
workspace/   il progetto Vitis: si apre con la GUI
docs/        la spiegazione passo passo
```

I sorgenti stanno **fuori** dal workspace di proposito: restano indipendenti dal
tool, leggibili e comodi da versionare. Il `hls_config.cfg` li referenzia con
percorsi relativi.

---

## Documentazione

| File | Contenuto |
|---|---|
| [docs/00_hls_vs_vhdl.md](docs/00_hls_vs_vhdl.md) | Il modello mentale: cosa cambia rispetto al VHDL, tabella di traduzione, il modello di esecuzione `ap_ctrl_hs`, e cosa abbiamo osservato a ogni gradino |
| [docs/01_riferimenti_amd.md](docs/01_riferimenti_amd.md) | La documentazione ufficiale AMD: cosa è già installato in locale (API Python, esempi, header), quale UG/PG copre cosa, e le poche pagine che servono a questo gradino |
| [docs/02_flusso_build.md](docs/02_flusso_build.md) | GUI e riga di comando affiancate, il journal, la conversione `hls_config.cfg` ↔ Tcl, dove finiscono i file generati, come si monta un esperimento isolato |
| [docs/05_capacita_e_limiti_hls.md](docs/05_capacita_e_limiti_hls.md) | Cosa c'è **fuori** da questo progetto: parallelismo, filtri a finestra, folding delle risorse, macchine a stati, quantizzazione, serializzatori. Tutto misurato, niente a memoria |

I sorgenti in `src/` e `tb/` sono commentati riga per riga: sono parte della
documentazione, non solo codice.

---

## La scala didattica

Ogni gradino aggiunge **una cosa sola**, poi si sintetizza e si guarda cosa è
cambiato nell'hardware generato.

| # | Cosa si aggiunge | Cosa si scopre | Stato |
|---|---|---|---|
| 1.1 | Pass-through, solo pragma `axis` | I pin `ap_start`/`ap_done`/`ap_idle`/`ap_ready` esistono davvero | ✅ fatto |
| 1.2 | `s_axilite` su `return` | Quei pin lasciano il bordo del modulo: nascono `s_axi_ctrl` + `interrupt`, e i registri CTRL/GIER/IER/ISR | ✅ fatto |
| 1.3 | Primo registro di configurazione | L'ordine degli argomenti C **è** la mappa registri. Nasce `gain` a 0x10, e il primo moltiplicatore | ✅ fatto |
| 1.4 | Primo registro di stato | Un output è un puntatore perché il C lo dice. Nasce `sample_count` a 0x18 e il bit `_ap_vld` a 0x1c, nello slot che al 1.3 era `reserved` | ✅ fatto |
| 1.5 | Controllo esplicito del pipeline | Che `PIPELINE II=1` non cambia niente (lo fa già il tool), e cosa si compra forzando `II=2` | — |
| 1.6 | `ap_fixed` per il guadagno | Fixed point, crescita dei bit, saturazione | — |
| 1.7 | Statistiche in variabili `static` | Registri che sopravvivono, e il reset | — |
| 1.8 | Registri a campi di bit, flag sticky | Come si scrive un registro industriale | — |
| 1.9 | Watchdog | Come un IP si difende da un `TLAST` mancante | — |

Poi: lettura dei report → esperimenti sulle direttive → cosimulation →
export come IP di catalogo → verifica in block design Vivado con AXI VIP.

---

## Ambiente

- Vivado + Vitis **2025.2** in `/tools/Xilinx/2025.2`
- Parte target: `xcvc1902-vsva2197-2MP-e-S` (VCK190, Versal AI Core)
- Clock: 4.0 ns (250 MHz)
