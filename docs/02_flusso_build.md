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

Dopo `make cosim` si aggiunge la cartella `sim/`:

| Percorso | Contenuto |
|---|---|
| `sim/report/axis_scaler_cosim.rpt` | esito (`Pass`/`Fail`) e latenze min/avg/max misurate |
| `sim/report/verilog/result.transaction.rpt` | **latenza di ogni singola transazione** — una riga per chiamata della top function nel testbench |
| `sim/report/verilog/lat.rpt` | gli stessi numeri in forma di variabili, comodi da parsare |
| `sim/verilog/` | il testbench RTL generato e i file del simulatore |
| `sim/tv/` | i *test vector*: gli stimoli registrati dalla simulazione C e riapplicati all'RTL |
| `sim/axis_scaler_cosim_random_stall.json` | la configurazione degli stalli (di default `delay == 0`, cioè disattivati) |

Che HLS generi anche il **VHDL**, non solo il Verilog, è una fortuna per chi
viene da lì: la entity generata è perfettamente leggibile e si presta a essere
confrontata gradino per gradino con quello che abbiamo scritto in C. Occhio però:
**la cosim di default simula il Verilog**, non il VHDL (nel report la riga `VHDL`
dice `NA`). Per simulare il file che stai leggendo serve `cosim.rtl=vhdl`.

---

## 7. Ispezionare cosa è cambiato: GUI e CLI affiancate

Dal gradino 1.2 in poi il lavoro vero non è "lanciare la sintesi": è **leggere
l'artefatto generato e confrontarlo con quello di prima**. Queste sono le
operazioni che servono a farlo, nelle due forme.

| Cosa vuoi vedere | Nella GUI | Da riga di comando |
|---|---|---|
| Modificare un pragma di interfaccia | Apri `src/axis_scaler.cpp` nell'editor e scrivilo a mano. In alternativa, col cursore sulla funzione, pannello **HLS DIRECTIVES** → `+` → `INTERFACE`: la GUI scrive il pragma nel sorgente | è una modifica al file, nessun comando |
| **Tabella delle interfacce** (le porte RTL vere) | *Flow* → **C SYNTHESIS → Reports → Synthesis** → sezione *Interface* | `grep -A40 '== Interface' workspace/axis_scaler/axis_scaler/hls/syn/report/axis_scaler_csynth.rpt` |
| **La entity VHDL generata** | *Explorer* → `axis_scaler/hls/syn/vhdl/axis_scaler.vhd` (doppio click) | `make entity` |
| Confronto entity prima/dopo | copia la entity in un file prima di risintetizzare, poi *Compare With* | `sed -n '/^entity axis_scaler is/,/^end;/p' .../axis_scaler.vhd > entity_1.2.vhd` e poi `diff -u entity_1.1.vhd entity_1.2.vhd` |
| Risorse (FF/LUT/DSP) | *Reports → Synthesis* → *Utilization Estimates* | `sed -n '/Utilization Estimates/,/^$/p' .../axis_scaler_csynth.rpt` |
| **La mappa registri** `_hw.h` | *Explorer* → `axis_scaler/hls/impl/ip/drivers/axis_scaler_v1_0/src/xaxis_scaler_hw.h` | `cat $(find workspace -path '*impl/ip/drivers*' -name '*_hw.h')` |
| I moduli RTL generati (quali file nuovi sono comparsi) | *Explorer* → cartella `syn/vhdl/` | `ls workspace/axis_scaler/axis_scaler/hls/syn/vhdl/` |
| Cosa ha deciso il tool sulle direttive | *Explorer* → `syn/inferred_directives.ini` | `cat workspace/axis_scaler/axis_scaler/hls/syn/inferred_directives.ini` |

**Abitudine da prendere subito**: prima di risintetizzare, salva da parte la
entity e il report del gradino corrente. Senza il "prima" non esiste il
confronto, e il confronto è il punto di tutto il metodo. Questo progetto lo fa in
`/tmp`, ma va bene qualunque posto.

**Attenzione ai numeri di riga nei nomi.** HLS incorpora la riga del sorgente
nei nomi dei segnali (`phi_ln203_reg_123` = il `while` alla riga 203). Basta
spostare un commento perché decine di segnali cambino nome senza che cambi un
filo — è successo al gradino 1.5, con 86 righe di diff su un RTL identico. Per
confrontare due build del top:

```bash
diff <(sed -E 's/ln[0-9]+/lnX/g' prima/axis_scaler.vhd) \
     <(sed -E 's/ln[0-9]+/lnX/g' dopo/axis_scaler.vhd)
```

I sotto-moduli (`_ctrl_s_axi`, `_mul_*`, `_regslice_*`) non hanno questo
problema e si confrontano con `diff` o `cmp` direttamente. Nella GUI, *Compare
With* non normalizza: leggi il diff sapendo che `lnNNN` non è una differenza.

**Quando compare un file nuovo in `syn/vhdl/`, i report si sdoppiano.** Al
gradino 1.7 il loop è stato estratto in un modulo a sé, e da allora
`syn/report/` contiene un `axis_scaler_<Modulo>_csynth.rpt` per ogni modulo
oltre al report del top: le tabelle *Register*, *Expression* e *Multiplexer* del
top contano solo la logica del top, e quella del sotto-modulo compare nel top
come una riga di *Instance*. Per il confronto con il gradino precedente vanno
sommate. Nella GUI: *Reports → Synthesis* ha un menu a tendina con i moduli.

**Il log dice cose che il report non dice.** I `WARNING` di
`workspace/axis_scaler/axis_scaler/logs/hls_compile.log` (nella GUI: pannello
*Output*, oppure il file in *Explorer*) vanno letti a ogni sintesi: al 1.7 il
fatto centrale del gradino — *«Register 'tot_campioni' is power-on
initialization»* — stava lì e in nessun report.

### Una asimmetria utile da conoscere

Con `flow_target=vivado`, **`make csynth` esegue anche il packaging dell'IP**. Nel
log lo vedi:

```text
INFO: [IMPL 213-8] Exporting RTL as a Vivado IP.
INFO: Add axi4lite interface s_axi_ctrl
INFO: Add interrupt interface interrupt
INFO: Created IP archive .../xilinx_com_hls_axis_scaler_1_0.zip
```

Quindi dopo una semplice sintesi hai già `component.xml`, lo `.zip` per il
catalogo e i driver C con `_hw.h`. `make ip` (*PACKAGE → Run*) serve a rifare
esplicitamente quel passo, tipicamente dopo aver cambiato i metadati
`package.ip.*` (vendor, versione, taxonomy) — cosa che faremo in Fase 4.

Nota sui nomi: lo `.zip` si chiama ancora `xilinx_com_hls_axis_scaler_1_0.zip`
perché non abbiamo ancora impostato `package.ip.vendor`. Il VLNV definitivo
(`riclab.io:hls:axis_scaler:1.0`) arriverà in Fase 4.

---

## 8. L'esperimento isolato: provare una variante senza toccare il progetto

Capita spesso di doversi rispondere a una domanda del tipo *"e se invece
usassi…?"* senza sporcare `src/`, senza far ripartire il workspace e senza
perdere gli artefatti del gradino in corso. Le risposte di `docs/05` sono state
prodotte tutte così.

La ricetta è un **component HLS usa-e-getta**, fatto di due file soli:

```bash
SCRATCH=/tmp/prova           # o la cartella che preferisci

mkdir -p $SCRATCH/src
cp src/axis_scaler.hpp $SCRATCH/src/
# ... poi modifica la variante che ti interessa in $SCRATCH/src/
```

Il `hls_config.cfg` è identico a quello del progetto, con **una sola
differenza**: i percorsi dei sorgenti sono assoluti invece che relativi, così il
file non dipende da dove si trova.

```ini
part=xcvc1902-vsva2197-2MP-e-S

[hls]
flow_target=vivado
package.output.format=ip_catalog
syn.file=/tmp/prova/src/axis_scaler.cpp     # <- assoluto
syn.top=axis_scaler
clock=4.0
syn.rtl.reset=control
syn.rtl.reset_level=low
```

Nota che **manca `tb.file`**: per la sola sintesi non serve un testbench, e
ometterlo rende l'esperimento più rapido da montare. Ovviamente questo vale solo
per gli esperimenti: nel progetto vero la regola resta *non si sintetizza finché
`make csim` non è verde*.

**Variante senza toccare il sorgente.** Se la cosa da provare è un pragma di
ottimizzazione (`PIPELINE`, `UNROLL`, `ALLOCATION`…), non serve nemmeno
modificare il `.cpp`: la direttiva si scrive nel `cfg`, nella stessa forma che
la GUI produce scegliendo *Config file* come destinazione nel pannello
*HLS DIRECTIVES*:

```ini
syn.directive.pipeline=axis_scaler/copia_pacchetto II=2
syn.directive.allocation=axis_scaler/copia_pacchetto instances=mul limit=1 type=operation
```

Così lo sweep del gradino 1.5 è stato sette cartelle con lo **stesso**
sorgente e sette `cfg` che differiscono per una riga — e il "prima" è
garantito identico per costruzione. Se invece la variante deve girare in
**cosim** (per misurare i cicli, non solo leggerli nel report), servono anche
`tb.file` e `tb.cflags` con percorsi assoluti, e poi
`vitis-run --mode hls --cosim --config hls_config.cfg --work_dir prova`:
circa un minuto per componente.

Poi:

```bash
source /tools/Xilinx/2025.2/Vitis/settings64.sh
cd /tmp/prova && v++ -c --mode hls --config hls_config.cfg --work_dir prova
```

Una sintesi di un componente piccolo costa **20-30 secondi**. I risultati
finiscono in `/tmp/prova/prova/hls/`, con la stessa struttura del progetto
(`syn/vhdl/`, `syn/report/`, `impl/ip/drivers/`), quindi tutte le tecniche di
ispezione del §7 valgono identiche.

### Cosa guardare, in ordine di utilità

```bash
RPT=$(find /tmp/prova/prova/hls/syn/report -name "*_csynth.rpt")

grep -E "ap_clk  \|" $RPT              # percorso critico stimato
grep -A3 "Loop Name" $RPT              # II, iteration latency, pipelined sì/no
grep "^|Total " $RPT                   # DSP / FF / LUT
grep -c "constant ap_ST_" /tmp/prova/prova/hls/syn/vhdl/*.vhd   # stati della FSM
```

Quell'ultimo conteggio degli stati è più informativo di quanto sembri: è il modo
più rapido per accorgersi che il tool ha cambiato **struttura**, non solo
dimensioni (vedi `docs/05` §3, dove 3 stati contro 6 distinguono una pipeline che
regge da una che si è rotta).

### Le due regole di questo metodo

1. **Una variante = un componente.** Non si modifica il componente esistente e
   poi si torna indietro: si creano cartelle sorelle (`prova_a`, `prova_b`) e si
   confrontano i report. Così il "prima" esiste ancora quando serve.
2. **Gli esperimenti non entrano in `git`.** Vivono in scratchpad e spariscono
   con la sessione. Quello che resta è il *risultato*, scritto in `docs/` con i
   numeri veri — perché fra sei mesi servirà la conclusione, non i file.
