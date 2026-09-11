# 02 — Il flusso di build: GUI e riga di comando

Regola del progetto: **la GUI è lo strumento principale**, la riga di comando è
lo stesso identico progetto visto da un'altra angolazione. Ogni riga di questo
documento mette le due cose una accanto all'altra.

---

## 1. Il panorama dei tool nella 2025.2

Se cerchi tutorial online li troverai quasi tutti con `vitis_hls -f run_hls.tcl`.
**Su questa installazione quel binario non esiste più.** Dalla 2023.2 AMD ha
unificato tutto nel Vitis Unified IDE. Il motore di sintesi è lo stesso di
sempre; sono cambiate le porte d'accesso.

| Via d'accesso | Comando | Quando si usa |
|---|---|---|
| **GUI** | `vitis -w workspace` | esplorare report, Schedule Viewer, waveform |
| API Python | `vitis -s script.py` | creare/riconfigurare il progetto in modo riproducibile |
| CLI | `v++ -c --mode hls --config ...`<br>`vitis-run --mode hls --csim/--cosim/--package --config ...` | build ripetibili, log completi, `make` |
| CLI Tcl legacy | `vitis-run --mode hls --tcl run_hls.tcl` | leggere e tradurre i tutorial pre-2023 |

Tutte agiscono sullo **stesso `hls_config.cfg`**. Non c'è un "progetto GUI" e un
"progetto CLI".

---

## 2. Corrispondenza fra bottoni e comandi

Nel pannello del component, nella GUI, trovi queste voci in quest'ordine. Sono le
stesse dei bersagli del `Makefile`:

| Nella GUI | Da riga di comando | Cosa fa davvero |
|---|---|---|
| *C SIMULATION → Run* | `make csim` | Compila `src/` + `tb/` con clang e li esegue **sul PC**. Nessun hardware, nessun clock. Verifica l'algoritmo |
| *C SYNTHESIS → Run* | `make csynth` | Traduce il C in RTL (Verilog **e** VHDL). Produce i report |
| *C/RTL COSIMULATION → Run* | `make cosim` | Riesegue lo stesso testbench, ma stavolta contro l'RTL vero in un simulatore, con clock e handshake |
| *PACKAGE → Run* | `make ip` | Impacchetta l'RTL come IP per il catalogo Vivado |
| — | `make entity` | Scorciatoia: stampa la entity VHDL generata |
| — | `make workspace` | Ricrea il progetto da zero |

L'ordine non è casuale ed è la disciplina professionale del flusso: **non si
sintetizza finché la C simulation non è verde**, perché trovare un bug in
simulazione C costa secondi, in cosimulation minuti, in laboratorio giorni.

Il `Makefile` entra nella cartella del component prima di lanciare i comandi
(`cd workspace/axis_scaler`) perché i percorsi dentro `hls_config.cfg` sono
relativi al file stesso. E usa `--work_dir axis_scaler`, lo stesso valore che sta
in `vitis-comp.json`: così GUI e CLI scrivono nella **stessa** cartella e non ti
ritrovi a leggere report vecchi.

---

## 3. Il journal: la GUI ti scrive i comandi da sola

```text
workspace/_ide/workspace_journal.py
```

Ogni azione fatta nella GUI viene trascritta qui come comando Python. È lo stesso
principio del `vivado.jou` che registra il Tcl in Vivado.

Esempio reale, prodotto da una creazione di component:

```python
client = vitis.create_client()
client.set_workspace(path="workspace")
comp = client.create_hls_component(name = "axis_scaler")
cfg = client.get_config_file(path=".../hls_config.cfg")
cfg.set_value(key="part", value="xcvc1902-vsva2197-2MP-e-S")
cfg.add_lines(values=["syn.top=axis_scaler"])
```

**Usalo come strumento di apprendimento**: fai una cosa cliccando, poi apri il
journal e guarda come si scrive. È il modo più veloce per passare da "so dove si
clicca" a "so cosa fa".

---

## 4. `hls_config.cfg` ↔ comandi Tcl

I tutorial vecchi sono pieni di comandi `config_*`. Non sono un'altra tecnologia:
sono **le stesse opzioni** con un'altra sintassi. Tabella di conversione:

| Tcl (tutorial pre-2023) | `hls_config.cfg` (oggi) |
|---|---|
| `set_part {xcvc1902-...}` | `part=xcvc1902-...` |
| `create_clock -period 4.0` | `clock=4.0` (nanosecondi) |
| `set_top axis_scaler` | `syn.top=axis_scaler` |
| `add_files src/x.cpp` | `syn.file=../../src/x.cpp` |
| `add_files -tb tb/x_tb.cpp` | `tb.file=../../tb/x_tb.cpp` |
| `add_files -tb ... -cflags "-I../src"` | `tb.cflags=-I../../src` |
| `config_rtl -reset control` | `syn.rtl.reset=control` |
| `config_rtl -reset_level low` | `syn.rtl.reset_level=low` |
| `config_cosim -random_stall` | `cosim.random_stall=1` |
| `config_cosim -trace_level all` | `cosim.trace_level=all` |
| `config_export -vendor riclab.io` | `package.ip.vendor=riclab.io` |
| `config_export -format ip_catalog` | `package.output.format=ip_catalog` |
| `open_solution -flow_target vivado` | `flow_target=vivado` |
| `config_interface -s_axilite_sw_reset` | `syn.interface.s_axilite_sw_reset=1` |

Regola generale: il comando Tcl `config_<gruppo> -<opzione> <valore>` corrisponde
alla chiave `<gruppo>.<opzione>=<valore>`, con il prefisso di sezione (`syn.`,
`cosim.`, `package.`) davanti.

---

## 5. Il tranello di `flow_target`

Il wizard *New HLS Component* della GUI scrive di default:

```ini
flow_target=vitis
package.output.format=xo
```

Cioè produce un **kernel `.xo`** per il flusso `v++` / extensible platform, **non**
una IP per il catalogo di Vivado. Il sintomo è che cerchi lo `.zip` da importare
in Vivado e non lo trovi.

Per una IP di catalogo serve:

```ini
flow_target=vivado
package.output.format=ip_catalog
```

Il nostro `create_workspace.py` lo imposta già correttamente. **Se in futuro crei
un component dal wizard grafico, questa è la prima cosa da controllare.**

Nota: le due impostazioni non sono in conflitto per sempre. Alla Fase 6, quando
vorremo la stessa IP come kernel per l'hardware emulation, useremo un secondo
component con `flow_target=vitis`. Stessi sorgenti, confezione diversa.

---

## 6. Dove finiscono le cose

Dopo `make csynth`, dentro `workspace/axis_scaler/axis_scaler/hls/`:

| Percorso | Contenuto |
|---|---|
| `syn/vhdl/axis_scaler.vhd` | **L'RTL VHDL generato.** Il file da leggere per capire cosa hanno prodotto i pragma |
| `syn/verilog/` | lo stesso RTL in Verilog |
| `syn/report/axis_scaler_csynth.rpt` | il report di sintesi (timing, latenza, II, risorse, interfacce) |
| `csim/build/` | l'eseguibile della simulazione C |
| `impl/ip/` | l'IP impacchettata: `component.xml`, HDL, **e i driver C** |
| `impl/ip/drivers/*/src/*_hw.h` | **la mappa registri generata** — la sorgente di verità, mai copiarla a mano |

Che HLS generi anche il **VHDL**, non solo il Verilog, è una fortuna per chi
viene da lì: la entity generata è perfettamente leggibile e si presta a essere
confrontata gradino per gradino con quello che abbiamo scritto in C.
