# 01 — Documentazione ufficiale AMD: dove guardare

Indice ragionato della documentazione AMD per questo progetto. Non è un elenco di
link: è ordinato per **quando ti serve**, e distingue quello che hai già in locale
da quello che sta online.

Tutti i percorsi locali e le URL di questo file sono stati verificati su questa
installazione (2025.2) il 10 settembre 2026.

---

## 0. La regola: prima il locale, poi l'online

Tre livelli, in ordine di affidabilità:

| Livello | Perché è il migliore | Rischio |
|---|---|---|
| **1. L'artefatto generato** (`syn/vhdl/*.vhd`, `*_hw.h`, i report) | è *il tuo* progetto, non un esempio. Non può essere sbagliato o di un'altra versione | nessuno |
| **2. La doc installata in locale** | è esattamente la 2025.2 che stai usando | nessuno |
| **3. docs.amd.com** | completa e navigabile | il default è la versione *più recente*, non necessariamente la tua |

Questa gerarchia non è pedanteria: la trappola `flow_target` che abbiamo in
`docs/02` §5 esiste proprio perché i tutorial online sono quasi tutti di versioni
precedenti.

---

## 1. Cosa hai già installato (offline, versione esatta)

### 1a. La documentazione dell'API Python — `vitis -s`

È quella che usa [`scripts/create_workspace.py`](../scripts/create_workspace.py),
ed è **installata in locale**: 13 MB di HTML generato con Sphinx, 190 simboli
documentati.

```bash
xdg-open /tools/Xilinx/2025.2/Vitis/cli/api_docs/build/html/index.html
```

Titolo: *"Vitis Python CLI 2025.2 documentation"*. Due pagine sole:
`vitis.html` (il package `vitis`) e `xsdb.html` (il debugger).

Le classi che riguardano noi, con le firme verificate:

```python
client.create_hls_component(name, platform=None, part=None,
                            cfg_file=None, template=None)

HLSComponent.run(operation)      # operation: una stringa, vedi sotto
Component.report()               # stampa le informazioni del component

ConfigFile.set_value(section='', key='', value='')   # sostituisce
ConfigFile.add_lines(section, [righe])               # aggiunge
ConfigFile.get_value(...)  .get_sections()  .remove(...)
```

I valori validi di `operation` (estratti dagli esempi ufficiali, non dedotti):

| `run(...)` | Equivalente GUI | Equivalente `make` |
|---|---|---|
| `'C_SIMULATION'` | *C SIMULATION → Run* | `make csim` |
| `'SYNTHESIS'` | *C SYNTHESIS → Run* | `make csynth` |
| `'CO_SIMULATION'` | *C/RTL COSIMULATION → Run* | `make cosim` |
| `'PACKAGE'` | *PACKAGE → Run* | `make ip` |
| `'IMPLEMENTATION'` | *IMPLEMENTATION → Run* | (non ancora usato) |

### 1b. Gli esempi Python ufficiali — il nostro flusso, scritto da AMD

```text
/tools/Xilinx/2025.2/Vitis/cli/examples/
├── README.md                              elenco commentato di tutti gli esempi
├── accelerated/hls_bottom_up_uc1.py       component da un .cfg esistente
├── accelerated/hls_bottom_up_uc2.py       component + .cfg costruito via API
└── test_srcs/hls_config.cfg               un hls_config.cfg minimo ufficiale
```

`hls_bottom_up_uc1.py` è **44 righe** e fa esattamente quello che fa il nostro
script: crea il client, imposta il workspace, crea un component HLS da un file di
configurazione, lancia la sintesi. Leggilo affiancato al nostro
`create_workspace.py`: la differenza è che noi scriviamo `hls_config.cfg` come
testo commentato, mentre `uc2.py` lo costruisce chiamando `set_value` /
`add_lines`. Entrambe le vie sono legittime e producono lo stesso file — noi
abbiamo scelto il testo perché è documentazione leggibile in un diff.

> Nota terminologica: AMD chiama **"bottom-up flow"** esattamente quello che
> stiamo facendo — partire dalla IP HLS e portarla poi in Vivado. Il "top-down"
> parte dalla piattaforma. Serve saperlo per cercare nella doc con le parole
> giuste.

### 1c. Gli header della libreria HLS — la verità sui tipi

```text
/tools/Xilinx/2025.2/Vitis/include/
├── ap_axi_sdata.h     ap_axis / ap_axiu / hls::axis  <- il nostro pkt_t
├── hls_stream.h       hls::stream<T>
├── ap_int.h           ap_int<N> / ap_uint<N>
├── ap_fixed.h         ap_fixed<W,I>            <- servirà al gradino 1.6
└── ... (hls_math.h, hls_vector.h, hls_task.h, ...)
```

Quando vuoi sapere **quali campi ha davvero `ap_axis`**, la risposta definitiva
non è un manuale: è il blocco di commento in testa alla definizione del tipo.

```bash
sed -n '80,100p' /tools/Xilinx/2025.2/Vitis/include/ap_axi_sdata.h
```

Dice, testualmente:

```text
Signals: DATA, DEST, ID, KEEP, LAST, STRB, USER
  All signals are optional:
    LAST is enabled by default
    DEST, ID, & USER are disabled by default
    DATA, KEEP, & STRB are enabled by default for non-void DATA type
```

Ed ecco il primo fatto che leggendo l'header si scopre e che nessun tutorial dice:

```bash
grep -nE "^using (ap_axis|ap_axiu|qdma_axis)|^struct axis" \
     /tools/Xilinx/2025.2/Vitis/include/ap_axi_sdata.h
```

```cpp
struct axis {                                    // riga 99: il tipo VERO
using ap_axis  = hls::axis<ap_int<WData>,  ...>  // riga 321: un alias
using ap_axiu  = hls::axis<ap_uint<WData>, ...>  // riga 329: un alias
```

**`ap_axis` non è una struct: è un alias di `hls::axis`.** Il tipo primario è
`hls::axis<T, WUser, WId, WDest, EnableSignals, StrictEnablement>`, dove `T` è il
*tipo* del dato (non la sua larghezza in bit). Quindi il nostro
`ap_axis<32,0,0,0>` è, per il compilatore, `hls::axis<ap_int<32>, 0, 0, 0>`.

Da notare il **quinto** parametro di template, `EnableSignals`, che nella forma
`ap_axis<W,0,0,0>` resta al suo default
`(AXIS_ENABLE_KEEP | AXIS_ENABLE_LAST | AXIS_ENABLE_STRB)`: è il campo di bit che
decide quali segnali del protocollo esistono. È il motivo per cui la nostra IP ha
TKEEP e TSTRB anche se non li usiamo — e la prova che si possono spegnere
lasciando TLAST. Non facciamolo ora: è annotato qui perché è una cosa che si
*vede* leggendo l'header, non un passo della scala.

Questo è il file che il compilatore include davvero. Un manuale può essere di
un'altra versione; questo no.

### 1d. Librerie di dominio ed esempi, già installati

Trovati cercando materiale per le domande raccolte in `docs/05`. Utile sapere che
esistono, prima di riscrivere a mano qualcosa che c'è già.

| Percorso | Cosa è |
|---|---|
| `Vitis/include/hls_fir.h` | libreria HLS che avvolge il **FIR Compiler IP** di AMD: l'equivalente di istanziare il core del vendor invece di scrivere il tap delay line a mano |
| `Vitis/include/fir/fir_compiler_v7_2_bitacc_cmodel.h` | il modello bit-accurate dietro `hls::fir` |
| `Vitis/include/ap_fixed.h` | `ap_fixed<W,I,Q,O>` — gradino 1.6 |
| `Vitis/include/hls_vector.h` | tipo vettoriale, per il parallelismo di `docs/05` §1 |
| `Vitis/samples/template_window_class/src/fir.cpp` | un FIR con tap delay line — **attenzione: è AI Engine** (`adf::input_buffer`), non HLS. Il pattern è identico, il target no |
| `Vitis/samples/aie_system_examples/` | esempi di sistema AI Engine |

La distinzione AIE/HLS su quel `fir.cpp` vale la pena tenerla a mente: cercando
"fir" nell'installazione, il primo risultato leggibile è codice per una
architettura diversa. Il `#include "adf.h"` in testa è il segnale.

---

## 2. I documenti online, per numero

Sapere quale numero copre cosa è metà del lavoro: cercare "AXI stream HLS" su
Google restituisce dieci versioni diverse dello stesso paragrafo.

| Doc | Titolo | Cosa ci trovi | Quando |
|---|---|---|---|
| **[UG1399](https://docs.amd.com/r/en-US/ug1399-vitis-hls)** | Vitis High-Level Synthesis User Guide | **il manuale principale**: pragma, interfacce, tipi, ottimizzazione, chiavi del `.cfg` | sempre |
| **[UG1448](https://docs.amd.com/r/2025.2-English/ug1448-hls-guidance)** | Vitis HLS Messaging | il significato di ogni messaggio `HLS 200-xxx` del log, con la spiegazione estesa e la risoluzione | quando il tool stampa un codice |
| **[UG1553](https://docs.amd.com/r/en-US/ug1553-vitis-ide/Creating-an-HLS-Component)** | Vitis Unified IDE and Common Command-Line Reference | la GUI e i comandi `v++` / `vitis-run` | quando ti serve un flag della CLI |
| **[UG1400](https://docs.amd.com/r/en-US/ug1400-vitis-embedded/Vitis-Python-API)** | Vitis Unified Software Platform (embedded) | l'API Python, versione online | in alternativa alla doc locale §1a |
| **[UG1702](https://docs.amd.com/r/en-US/ug1702-vitis-accelerated-reference/Creating-Vitis-HLS-Components)** | Vitis Accelerated Reference | creazione di component HLS nel flusso accelerato | Fase 6 |
| **[UG1037](https://www.amd.com/content/dam/xilinx/support/documents/ip_documentation/axi_ref_guide/latest/ug1037-vivado-axi-reference-guide.pdf)** | Vivado Design Suite: AXI Reference Guide (PDF) | **come AMD usa AXI**: TKEEP/TSTRB/TLAST, convenzioni, limiti delle IP | per capire i segnali dello stream |
| **[UG896](https://docs.amd.com/r/en-US/ug896-vivado-ip/Verification-IP)** | Vivado Design Suite: Designing with IP | catalogo IP, packaging, Verification IP | Fase 4-5 |
| **[PG267](https://docs.amd.com/r/en-US/pg267-axi-vip)** | AXI Verification IP | il VIP AXI4-Lite per scrivere i registri in simulazione | Fase 5 |
| **[PG277](https://docs.amd.com/v/u/en-US/pg277-axi4stream-vip)** | AXI4-Stream Verification IP | il VIP AXI4-Stream, master e slave con backpressure | Fase 5 |

---

## 3. Le pagine che servono **adesso** (gradino 1.1)

Poche e precise. Sono le uniche da leggere a questo punto della scala:

| Domanda | Pagina |
|---|---|
| Cosa fa esattamente `#pragma HLS INTERFACE`, tutti i `mode=` | [pragma HLS interface](https://docs.amd.com/r/en-US/ug1399-vitis-hls/pragma-HLS-interface) |
| Panoramica: quali interfacce può avere una IP HLS | [Interfaces of the HLS Design](https://docs.amd.com/r/en-US/ug1399-vitis-hls/Interfaces-of-the-HLS-Design) |
| **`ap_ctrl_hs`: cos'è, e le alternative** (`ap_ctrl_chain`, `ap_ctrl_none`) | [Block-Level Control Protocols](https://docs.amd.com/r/en-US/ug1399-vitis-hls/Block-Level-Control-Protocols) |
| Come `hls::stream` diventa TVALID/TREADY, e perché serve `ap_axis` per TLAST | [How AXI4-Stream is Implemented](https://docs.amd.com/r/en-US/ug1399-vitis-hls/How-AXI4-Stream-is-Implemented) |
| Le chiavi di `hls_config.cfg` (`syn.file`, `tb.cflags`, `clock`, ...) | [Defining the HLS Config File](https://docs.amd.com/r/en-US/ug1399-vitis-hls/Defining-the-HLS-Config-File) · [HLS Config File Commands](https://docs.amd.com/r/en-US/ug1399-vitis-hls/HLS-Config-File-Commands) |
| **Tradurre un tutorial vecchio**: `config_*` Tcl → chiave `.cfg` | [Tcl to Config File Command Map](https://docs.amd.com/r/en-US/ug1399-vitis-hls/Tcl-to-Config-File-Command-Map) |

L'ultima riga è la versione ufficiale e completa della tabella che abbiamo in
[`docs/02`](02_flusso_build.md) §4 — la nostra ha solo le righe che usiamo.

**Ordine consigliato:** *Block-Level Control Protocols* prima di tutto. È la
pagina che spiega i quattro pin che vedi nella entity del gradino 1.1, ed è il
concetto su cui è costruita tutta la scala didattica.

---

## 4. Le pagine dei gradini e delle fasi successive

Elencate per non doverle cercare al momento giusto. **Non leggerle in anticipo**:
qui il metodo è vedere l'hardware prima della spiegazione.

<details>
<summary>Gradino 1.2 — <code>s_axilite</code> e il banco registri</summary>

- [AXI4-Lite Interface](https://docs.amd.com/r/en-US/ug1399-vitis-hls/AXI4-Lite-Interface)
- [Control Clock and Reset in AXI4-Lite Interfaces](https://docs.amd.com/r/en-US/ug1399-vitis-hls/Control-Clock-and-Reset-in-AXI4-Lite-Interfaces)
  — **la pagina sui clock multipli**: contiene il vincolo "AXI4-Lite clock must
  be synchronous to `ap_clk`, derived from the same master generator clock" e la
  nota sulla CDC slice generata automaticamente da Vivado IP Integrator.
  Commentata in `docs/00` §5, "Un solo `ap_clk` per tutto"
- [Clock and Reset Ports](https://docs.amd.com/r/en-US/ug1399-vitis-hls/Clock-and-Reset-Ports)
  — `ap_clk`, `ap_rst_n`, e l'opzione `syn.interface.clock_enable` per `ap_ce`
- [Customizing AXI4-Lite Slave Interfaces in IP Integrator](https://docs.amd.com/r/en-US/ug1399-vitis-hls/Customizing-AXI4-Lite-Slave-Interfaces-in-IP-Integrator)
</details>

<details>
<summary>Gradini 1.5-1.6 — pipeline e fixed point</summary>

- [pragma HLS pipeline](https://docs.amd.com/r/en-US/ug1399-vitis-hls/pragma-HLS-pipeline)
- Tipi a precisione arbitraria e `ap_fixed`: cercare *"Arbitrary Precision"* in UG1399
- In locale: `/tools/Xilinx/2025.2/Vitis/include/ap_fixed.h`
</details>

<details>
<summary>Fase 3 — cosimulation · Fase 5 — block design con VIP</summary>

- [Registered AXI4-Stream Interfaces](https://docs.amd.com/r/en-US/ug1399-vitis-hls/Registered-AXI4-Stream-Interfaces)
- [PG267 — AXI VIP in Vivado IP Integrator](https://docs.amd.com/r/en-US/pg267-axi-vip/AXI-VIP-in-Vivado-IP-Integrator) e [Test Bench](https://docs.amd.com/r/en-US/pg267-axi-vip/Test-Bench)
- [PG277 — AXI4-Stream VIP](https://docs.amd.com/v/u/en-US/pg277-axi4stream-vip)
</details>

---

## 5. Due cose che fanno risparmiare tempo

### 5a. Come si fissa la versione in una URL di docs.amd.com

```text
https://docs.amd.com/r/en-US/ug1399-vitis-hls/pragma-HLS-interface
                       ^^^^^  = "l'ultima versione", qualunque essa sia

https://docs.amd.com/r/2025.2-English/ug1399-vitis-hls/pragma-HLS-interface
                       ^^^^^^^^^^^^^^ = fissata alla NOSTRA versione
```

Sostituendo `en-US` con `2025.2-English` blocchi la pagina sulla versione che hai
installato. Serve quando una pagina descrive una chiave o un default che nella tua
versione si comporta diversamente. Funziona con qualsiasi versione
(`2020.2-English`, `2023.2-English`, ...), il che è comodo per capire *quando* una
cosa è cambiata.

**Che il problema sia reale e non teorico l'ho verificato scrivendo questo file:**
aprendo `docs.amd.com/r/en-US/ug1448-hls-guidance` la pagina che è arrivata era la
**2026.1**, non la nostra 2025.2. Per questo la riga di UG1448 nella tabella §2 è
l'unica già pinnata: è il documento che si consulta con il log del *tuo* tool
davanti, e leggerlo in un'altra versione è il modo più diretto per inseguire un
messaggio che non esiste.

Le altre righe della tabella sono lasciate su `en-US` di proposito, perché servono
a navigare e leggere; quando invece stai verificando un dettaglio preciso —
il default di una chiave, i bit di un registro — pinna.

### 5b. Il tool ti dà il link da solo

Nei log di `make csim` / `make csynth` compaiono righe come questa — è un estratto
vero del nostro log:

```text
INFO: [HLS 200-1505] Using flow_target 'vivado'
Resolution: For help on HLS 200-1505 see docs.amd.com/access/sources/dita/topic
            ?Doc_Version=2025.2%20English&url=ug1448-hls-guidance
            &resourceid=200-1505.html
```

Ogni messaggio con un codice `[HLS 200-xxx]` ha una pagina dedicata in UG1448,
**già puntata alla versione giusta**. Quando un warning non ti è chiaro, la prima
mossa non è Google: è cercare `Resolution:` nel log.

Idem per la CLI: `v++`, `vitis-run` e i `config_*` **non supportano `-h`**. Il modo
di scoprire le opzioni valide è passare un flag inesistente e leggere l'elenco che
stampa l'errore (vedi `CLAUDE.md`, "Trappole già incontrate").

---

## 6. Fuori da AMD: gli standard

AMD implementa AXI, non lo definisce. Le specifiche sono di ARM e si scaricano
gratuitamente da `developer.arm.com` (registrazione richiesta):

- **AMBA AXI4-Stream Protocol Specification** (ARM IHI 0051) — il protocollo dei
  nostri `s_axis`/`m_axis`: handshake TVALID/TREADY, semantica di TLAST, TKEEP e
  TSTRB, cosa è lecito e cosa no;
- **AMBA AXI Protocol Specification** (ARM IHI 0022) — AXI4 e AXI4-Lite, i cinque
  canali del nostro futuro `s_axi_ctrl`.

Vale la pena leggere almeno il capitolo sull'handshake di IHI 0051: è tre pagine,
e chiarisce una regola che il codice HLS nasconde — **una volta alzato, TVALID non
può essere abbassato prima che TREADY arrivi**. Se un giorno scrivessi un master
AXI-Stream a mano in VHDL, è la regola che si viola per prima.

---

## 7. Esempi da leggere (repository ufficiali)

| Dove | Cosa |
|---|---|
| [Xilinx/Vitis-HLS-Introductory-Examples](https://github.com/Xilinx/Vitis-HLS-Introductory-Examples) | esempi minimi per argomento, inclusi **AXI Master, AXI Lite e AXI Stream**. Il più utile per noi: un esempio = un concetto |
| [Xilinx/Vitis-Tutorials](https://github.com/Xilinx/Vitis-Tutorials) | tutorial completi. Vedi `Getting_Started/Vitis_HLS/`. Usa il branch della **tua** versione |
| [Xilinx/HLS](https://github.com/Xilinx/HLS) | il front-end LLVM di Vitis HLS, open source. Per curiosità o per capire un comportamento del compilatore |

Attenzione al branch: quei repository hanno un ramo per versione (`2024.1`,
`2025.1`, ...). Il `master` non è detto corrisponda alla tua installazione.

---

## Fonti

Verificate il 10 settembre 2026 contro l'installazione locale e docs.amd.com:

- [UG1399 — Vitis High-Level Synthesis User Guide](https://docs.amd.com/r/en-US/ug1399-vitis-hls)
  ([pragma HLS interface](https://docs.amd.com/r/en-US/ug1399-vitis-hls/pragma-HLS-interface),
  [Interfaces of the HLS Design](https://docs.amd.com/r/en-US/ug1399-vitis-hls/Interfaces-of-the-HLS-Design),
  [Block-Level Control Protocols](https://docs.amd.com/r/en-US/ug1399-vitis-hls/Block-Level-Control-Protocols),
  [How AXI4-Stream is Implemented](https://docs.amd.com/r/en-US/ug1399-vitis-hls/How-AXI4-Stream-is-Implemented),
  [AXI4-Lite Interface](https://docs.amd.com/r/en-US/ug1399-vitis-hls/AXI4-Lite-Interface),
  [Defining the HLS Config File](https://docs.amd.com/r/en-US/ug1399-vitis-hls/Defining-the-HLS-Config-File),
  [HLS Config File Commands](https://docs.amd.com/r/en-US/ug1399-vitis-hls/HLS-Config-File-Commands),
  [Tcl to Config File Command Map](https://docs.amd.com/r/en-US/ug1399-vitis-hls/Tcl-to-Config-File-Command-Map),
  [Vitis HLS Command Reference](https://docs.amd.com/r/en-US/ug1399-vitis-hls/Vitis-HLS-Command-Reference))
- [UG1553 — Creating an HLS Component](https://docs.amd.com/r/en-US/ug1553-vitis-ide/Creating-an-HLS-Component)
- [UG1400 — Vitis Python API](https://docs.amd.com/r/en-US/ug1400-vitis-embedded/Vitis-Python-API) ·
  [Managing Vitis IDE Components through Python APIs](https://docs.amd.com/r/en-US/ug1400-vitis-embedded/Managing-Vitis-IDE-Components-through-Python-APIs)
- [UG1702 — Creating Vitis HLS Components](https://docs.amd.com/r/en-US/ug1702-vitis-accelerated-reference/Creating-Vitis-HLS-Components)
- [UG1037 — Vivado Design Suite: AXI Reference Guide (PDF)](https://www.amd.com/content/dam/xilinx/support/documents/ip_documentation/axi_ref_guide/latest/ug1037-vivado-axi-reference-guide.pdf)
- [UG896 — Verification IP](https://docs.amd.com/r/en-US/ug896-vivado-ip/Verification-IP)
- [PG267 — AXI Verification IP](https://docs.amd.com/r/en-US/pg267-axi-vip) ·
  [PG277 — AXI4-Stream VIP](https://docs.amd.com/v/u/en-US/pg277-axi4stream-vip)
- [Xilinx/Vitis-HLS-Introductory-Examples](https://github.com/Xilinx/Vitis-HLS-Introductory-Examples) ·
  [Xilinx/Vitis-Tutorials](https://github.com/Xilinx/Vitis-Tutorials) ·
  [Xilinx/HLS](https://github.com/Xilinx/HLS)
