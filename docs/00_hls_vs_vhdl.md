# 00 — HLS visto da chi scrive VHDL

Questo documento serve a spostare il modello mentale, non a insegnare il C++.
Cresce insieme al progetto: ogni gradino aggiunge una sezione.

---

## 1. La differenza di fondo

In VHDL **descrivi una struttura**. Scrivi i registri, la macchina a stati, chi
pilota cosa in quale ciclo di clock. Il sintetizzatore traduce quasi
letteralmente ciò che hai scritto.

In HLS **descrivi un comportamento e dei vincoli**. Il tool decide da solo
quanti registri servono, come è fatta la macchina a stati e quante operazioni
stanno in un colpo di clock. Tu governi quella decisione con:

- il **periodo di clock** (`clock=4.0` nel nostro `hls_config.cfg`) — il vincolo
  temporale entro cui deve incastrare le operazioni;
- la **parte target** — quanto sono veloci i LUT e i DSP che ha a disposizione;
- le **direttive** (`#pragma HLS ...`) — i suggerimenti su come vuoi la
  micro-architettura;
- i **tipi di dato** — `ap_int<12>` produce davvero 12 fili, non 16.

Corollario pratico: lo stesso identico codice C, cambiando solo il periodo di
clock, produce RTL diverso. È il tool che rinegozia lo scheduling. Non succede
mai in VHDL, ed è la cosa che disorienta di più all'inizio.

---

## 2. Tabella di traduzione

| VHDL | HLS | Nota |
|---|---|---|
| `entity` | la *top function* | Il nome della funzione diventa il nome del modulo |
| porte della entity | argomenti della funzione | Il **tipo** di porta lo decidi con `#pragma HLS INTERFACE` |
| `signal` dentro un `process` | variabile locale | Vive solo dentro una "esecuzione" |
| `signal` che deve ricordare tra un ciclo e l'altro | variabile **`static`** | Diventa un registro che sopravvive tra le chiamate → gradino 1.7 |
| `constant` | `static const` / `#define` / parametro di template | |
| `std_logic_vector(31 downto 0)` | `ap_uint<32>` / `ap_int<32>` | `ap_int` ha il segno |
| `signed` / `unsigned` | `ap_int<N>` / `ap_uint<N>` | Aritmetica a larghezza arbitraria |
| `sfixed` / `ufixed` | `ap_fixed<W,I>` / `ap_ufixed<W,I>` | Con arrotondamento e saturazione configurabili → gradino 1.6 |
| `resize(...)` | assegnazione a un tipo più stretto | La conversione avviene all'assegnamento |
| `process` con FSM scritta a mano | un `while` / `for` | La FSM la genera il tool |
| istanza di un componente | chiamata a una funzione | |
| FIFO + logica di handshake | `hls::stream<T>` | Handshake generato automaticamente |
| `if rst='0' then ... end if` | `syn.rtl.reset` + `#pragma HLS reset` | **Non è automatico**: vedi gradino 1.7 |
| nome del `process` nei report | **etichetta del loop** | `copia_pacchetto:` compare nei report. Etichettare sempre |

---

## 3. Il modello di esecuzione: la cosa da capire prima di tutto

Un tuo modulo VHDL tipico è **free-running**: dopo il reset è vivo, reagisce ai
segnali d'ingresso a ogni colpo di clock, per sempre.

Una IP HLS **no**. Il protocollo di default si chiama `ap_ctrl_hs` ("handshake") e
funziona così:

```text
  qualcuno alza ap_start
        │
        ▼
  il blocco esegue UNA VOLTA il corpo della funzione
  (per noi: elabora un pacchetto intero, fino a TLAST)
        │
        ▼
  alza ap_done  →  "ho finito"
        │
        ▼
  torna in ap_idle e aspetta il prossimo ap_start
```

**Una chiamata della funzione C = una transazione dell'IP.** Questa equivalenza è
la chiave di tutto: nel testbench scrivi `axis_scaler(s_axis, m_axis);`, e in
hardware quella riga corrisponde a "alza `ap_start`, aspetta `ap_done`".

Da qui discendono conseguenze che sembrano arbitrarie finché non hai in mente
questo schema:

- perché i registri di configurazione vengono **campionati all'`ap_start`** (e
  quindi non puoi cambiare il guadagno a metà pacchetto);
- perché i registri di stato vengono **aggiornati all'`ap_done`**;
- perché per fare streaming continuo serve `auto_restart`;
- perché il driver software fa sempre: *scrivi la config → `ap_start` → aspetta
  `ap_done` → leggi lo stato*.

Esistono altri protocolli (`ap_ctrl_chain`, `ap_ctrl_none` per un blocco davvero
free-running). Li vedremo, ma `ap_ctrl_hs` è quello che rende una IP HLS pilotabile
da software, ed è quello che vogliamo.

---

## 4. Cosa abbiamo osservato al gradino 1.1

Il codice sintetizzato era un pass-through di tre righe, con soltanto due pragma
`axis` e un `ap_ctrl_hs` esplicito. La entity VHDL generata:

```vhdl
entity axis_scaler is
port (
    ap_clk        : IN  STD_LOGIC;                          -- mai dichiarato da noi
    ap_rst_n      : IN  STD_LOGIC;                          -- mai dichiarato da noi
    ap_start      : IN  STD_LOGIC;                          -- ┐
    ap_done       : OUT STD_LOGIC;                          -- │ il protocollo
    ap_idle       : OUT STD_LOGIC;                          -- │ ap_ctrl_hs,
    ap_ready      : OUT STD_LOGIC;                          -- ┘ in fili
    s_axis_TVALID : IN  STD_LOGIC;                          -- ┐
    s_axis_TREADY : OUT STD_LOGIC;                          -- │
    s_axis_TDATA  : IN  STD_LOGIC_VECTOR (31 downto 0);     -- │ dal pragma
    s_axis_TKEEP  : IN  STD_LOGIC_VECTOR (3 downto 0);      -- │ axis su s_axis
    s_axis_TSTRB  : IN  STD_LOGIC_VECTOR (3 downto 0);      -- │
    s_axis_TLAST  : IN  STD_LOGIC_VECTOR (0 downto 0);      -- ┘
    m_axis_TDATA  : OUT STD_LOGIC_VECTOR (31 downto 0);     -- ┐
    m_axis_TVALID : OUT STD_LOGIC;                          -- │ dal pragma
    m_axis_TKEEP  : OUT STD_LOGIC_VECTOR (3 downto 0);      -- │ axis su m_axis
    m_axis_TSTRB  : OUT STD_LOGIC_VECTOR (3 downto 0);      -- │ (TREADY è IN,
    m_axis_TLAST  : OUT STD_LOGIC_VECTOR (0 downto 0) );    -- ┘  arriva da valle)
end;
```

Tre osservazioni.

**a) `ap_clk` e `ap_rst_n` non li abbiamo mai nominati.** In HLS il clock è
implicito: c'è un solo dominio, e il tool ce lo mette. Non esiste l'equivalente
del tuo `clk : in std_logic` da dichiarare.

**b) I sei pin di controllo esistono davvero.** `ap_start`, `ap_done`, `ap_idle`,
`ap_ready` non sono un'astrazione software: sono fili. Finché nessuno alza
`ap_start`, questo blocco non legge un solo campione anche se `s_axis_TVALID` è
alto da un'ora. **È esattamente qui che una IP HLS smette di somigliare a un tuo
modulo VHDL.** Al gradino 1.2 li faremo sparire dentro un banco registri
AXI4-Lite cambiando una riga.

**c) `TREADY` ha la direzione "sbagliata" rispetto agli altri.** Su `s_axis` è
`OUT`, su `m_axis` è `IN`. Ovvio se ci pensi — il consumatore dice al produttore
quando è pronto — ma è il segnale che rende AXI4-Stream un protocollo con
backpressure, e quindi quello che può bloccare la tua IP.

### La sorpresa: il loop è già in pipeline

Il report di sintesi dice:

```text
    |     Loop Name     | Iteration Latency | II achieved | Pipelined |
    |- copia_pacchetto  |         2         |      1      |    yes    |
```

**II = 1 senza che abbiamo scritto `#pragma HLS PIPELINE`.** Dalla 2020.2 Vitis HLS
mette in pipeline i loop da solo quando può (opzione `syn.compile.pipeline_loops`).
Il pragma serve ancora, ma per *forzare* o *cambiare* una scelta, non per
ottenere il comportamento base.

Da sapere, perché quasi tutti i tutorial in giro sono precedenti a quel cambio e
ti fanno credere che senza pragma il loop sia sequenziale.

Altri due dettagli del report:

- `Iteration Latency = 2`, `II = 1`. Sono cose diverse: ogni campione impiega 2
  cicli ad attraversare il blocco (latenza), ma ne entra uno nuovo **ogni** ciclo
  (throughput). Come una catena di montaggio.
- La latenza totale è `?`. Il tool non sa quanti campioni ha un pacchetto: il
  loop finisce su `TLAST`, che è un dato, non una costante. Si può dare una stima
  al tool con `#pragma HLS LOOP_TRIPCOUNT` — serve solo a rendere leggibili i
  report, non cambia l'hardware.

Risorse usate: **4 FF, 12 LUT, 0 DSP, 0 BRAM**. Un pass-through è essenzialmente
due register slice AXI.

### La cosimulation: la prima volta che l'hardware viene eseguito

Al gradino 1.1 abbiamo lanciato anche la **C/RTL Cosimulation**, che nel piano
sarebbe una fase successiva. Vale la pena registrare cosa aggiunge, perché
completa il quadro dei tre passi:

| Passo | Dove gira | Cosa verifica | Cosa **non** vede |
|---|---|---|---|
| C Simulation | sul PC, con clang | l'**algoritmo** | tempo, clock, handshake, interfacce |
| C Synthesis | non esegue nulla | traduce C → RTL e **stima** | se funzioni davvero |
| C/RTL Cosimulation | simulatore (xsim) | l'**RTL vero**, con clock e protocollo | — |

Il punto da fissare: **la sintesi non verifica niente.** È un traduttore con un
preventivo allegato. Se un pragma di interfaccia è sbagliato, `csynth` passa lo
stesso e produce numeri ottimi su un circuito che in un sistema vero si pianta.
La cosim è il primo momento in cui qualcuno *esegue* il tuo hardware: riprende
`axis_scaler_tb.cpp`, ne registra gli stimoli, li applica all'RTL nel simulatore
e confronta le uscite. Ecco perché il testbench deve essere auto-verificante —
l'esito della cosim **è** il valore di ritorno del tuo `main()`.

Il report (`sim/report/axis_scaler_cosim.rpt`):

```text
|   RTL    | Status |  Latency: min 2 | avg 10 | max 18 |  Total Execution: 28
|   Verilog|  Pass  |
|      VHDL|    NA  |
```

**Attenzione alla riga `VHDL: NA`: la cosim ha simulato il Verilog, non il VHDL.**
È il default. HLS genera entrambi i linguaggi ma ne simula uno solo. Se vuoi che
la simulazione giri esattamente sul file che stai leggendo, si imposta
`cosim.rtl=vhdl` in `hls_config.cfg`.

Il report più istruttivo è però `sim/report/verilog/result.transaction.rpt`:

```text
                    latency     interval
transaction 0:            2            1
transaction 1:           10            9
transaction 2:           18            x
```

**Quelle tre transazioni sono i tre pacchetti del testbench**: 1, 8 e 17
campioni. Qui si vede il legame diretto fra una riga di C e i cicli di clock
consumati dall'hardware: `prova_pacchetto(17)` costa 18 cicli.

Ed è qui che i due report si completano a vicenda: la sintesi scriveva `?` alla
voce *Latency*, perché non può sapere quanto è lungo un pacchetto deciso da
`TLAST` a runtime. La cosim quel numero lo **misura**, perché i pacchetti veri
glieli abbiamo dati noi.

> **Una domanda lasciata aperta.** Il modello "N campioni → N+1 cicli" torna per
> il primo pacchetto (1→2) e per il terzo (17→18), **ma non per il secondo**
> (8→10, non 9). C'è un ciclo di bolla che i report non spiegano. Non inventiamo
> una spiegazione: è esattamente il tipo di domanda a cui si risponde guardando
> le waveform, e lo riprenderemo in Fase 3.

### Perché non si vedono ancora le waveform

Dopo la cosim non esiste **nessun file di waveform**: gli unici `.wcfg` presenti
sono template di configurazione (quali segnali mostrare), non dati registrati.

Il motivo è che in `hls_config.cfg` non c'è **nessuna chiave `cosim.*`**, quindi
valgono tutti i default — e il default non traccia niente. Per ottenerle servono:

```ini
cosim.wave_debug=1        # apre il visualizzatore
cosim.trace_level=all     # traccia tutti i segnali
```

Non le attiviamo adesso di proposito: rallentano la simulazione e sono il cuore
della Fase 3, dove aggiungeremo anche `cosim.random_stall=1` per stressare la
backpressure e vedere la IP fermarsi davvero. (Allo stato attuale lo stall è a
`delay == 0` su tutte le porte, cioè disattivato: i numeri qui sopra sono
"puliti", senza ritardi artificiali.)

---

## 5. Cosa abbiamo osservato al gradino 1.2

Modifica: **una riga**, il pragma di controllo del blocco.

```diff
- #pragma HLS INTERFACE mode=ap_ctrl_hs port=return
+ #pragma HLS INTERFACE mode=s_axilite  port=return bundle=ctrl
```

Nient'altro. Algoritmo identico, testbench identico, header identico.

### La entity: il diff vero

```diff
 entity axis_scaler is
+generic (
+    C_S_AXI_CTRL_ADDR_WIDTH : INTEGER := 4;
+    C_S_AXI_CTRL_DATA_WIDTH : INTEGER := 32 );
 port (
     ap_clk : IN STD_LOGIC;
     ap_rst_n : IN STD_LOGIC;
-    ap_start : IN STD_LOGIC;
-    ap_done : OUT STD_LOGIC;
-    ap_idle : OUT STD_LOGIC;
-    ap_ready : OUT STD_LOGIC;
     ...  (tutti i segnali s_axis_* / m_axis_* invariati)
+    s_axi_ctrl_AWVALID : IN STD_LOGIC;
+    s_axi_ctrl_AWREADY : OUT STD_LOGIC;
+    s_axi_ctrl_AWADDR : IN STD_LOGIC_VECTOR (C_S_AXI_CTRL_ADDR_WIDTH-1 downto 0);
+    s_axi_ctrl_WVALID/WREADY/WDATA/WSTRB      -- canale di scrittura dato
+    s_axi_ctrl_ARVALID/ARREADY/ARADDR         -- canale di lettura indirizzo
+    s_axi_ctrl_RVALID/RREADY/RDATA/RRESP      -- canale di lettura dato
+    s_axi_ctrl_BVALID/BREADY/BRESP            -- canale di risposta
+    interrupt : OUT STD_LOGIC );
 end;
```

Quattro pin via, diciassette dentro, più un `interrupt`. Ed è comparso un
**generic**: `C_S_AXI_CTRL_ADDR_WIDTH = 4`. Quattro bit di indirizzo = 16 byte =
esattamente i quattro registri da 32 bit che abbiamo ora. **Da tenere d'occhio al
gradino 1.3:** quando aggiungeremo un registro di configurazione questo numero
dovrà crescere, ed è la conferma fisica che la mappa registri si allarga.

### La cosa che inganna: i quattro pin non sono spariti

`grep` sui segnali interni di `axis_scaler.vhd`:

```vhdl
signal ap_start : STD_LOGIC;
signal ap_done  : STD_LOGIC;
signal ap_idle  : STD_LOGIC;
signal ap_ready : STD_LOGIC;
```

Ci sono ancora tutti. Sono diventati fili **interni**, tra il banco registri e la
logica di calcolo:

```vhdl
ctrl_s_axi_U : component axis_scaler_ctrl_s_axi
port map (
    AWVALID => s_axi_ctrl_AWVALID,
    ...
    ACLK     => ap_clk,
    ARESET   => ap_rst_n_inv,      -- nota: lo slave AXI vuole reset ATTIVO ALTO,
    ACLK_EN  => ap_const_logic_1,  -- HLS inserisce l'inverter da solo
    ap_start => ap_start,          -- ┐
    interrupt=> interrupt,         -- │ i quattro segnali di prima,
    ap_ready => ap_ready,          -- │ ora cablati al banco registri
    ap_done  => ap_done,           -- │
    ap_idle  => ap_idle);          -- ┘
```

Lo dice anche il log di sintesi, in modo inequivocabile:

```text
INFO: [RTGEN 206-500] Setting interface mode on function 'axis_scaler'
                      to 's_axilite & ap_ctrl_hs'.
INFO: [RTGEN 206-100] Bundling port 'return' to AXI-Lite port ctrl.
```

`s_axilite` **&** `ap_ctrl_hs`, non "invece di". Il modello di esecuzione del
gradino 1.1 — *una chiamata della funzione = una transazione dell'IP* — è
esattamente lo stesso. È cambiato solo **chi** alza `ap_start`: prima un altro
modulo RTL, ora una scrittura sul bus. Questa è la frase da portarsi dietro:

> `s_axilite` su `return` non cambia il protocollo del blocco, cambia il modo di
> raggiungerlo.

### Il file nuovo: il banco registri

In `syn/vhdl/` è comparso `axis_scaler_ctrl_s_axi.vhd`, **14,4 kB**: la macchina
a stati dello slave AXI4-Lite, la decodifica degli indirizzi, i registri e la
logica di interrupt. È il codice che in VHDL avresti scritto o incollato a mano —
tipicamente 300-400 righe, con almeno un bug nella gestione di `WSTRB`.

Vale la pena aprirlo e leggerne tre pezzi, perché sono idiomi di registro che
ritroverai in ogni IP AMD.

**1) `ap_start` è *clear on handshake* (COH), non un normale bit R/W:**

```vhdl
if (w_hs = '1' and waddr = ADDR_AP_CTRL and WSTRB(0) = '1' and WDATA(0) = '1') then
    int_ap_start <= '1';
elsif (ap_ready = '1') then
    int_ap_start <= int_auto_restart;   -- clear on handshake/auto restart
end if;
```

Scrivi 1, e l'hardware lo riazzera da solo quando il blocco parte. Non devi
riscriverci 0: se lo facessi rischieresti di annullare l'avvio. Il software fa
"scrivi 1 e dimenticalo". E si vede anche il ruolo di `auto_restart` (bit 7):
se è alto, `ap_start` **non** si azzera → il blocco riparte da solo, che è come
si fa streaming continuo.

**2) `WSTRB` viene davvero rispettato.** Quel `WSTRB(0) = '1'` nella condizione
è la gestione delle scritture parziali: se il processore scrive un solo byte, i
byte non selezionati non vengono toccati. È esattamente il pezzo che si sbaglia
scrivendo lo slave a mano.

**3) Il meccanismo di interrupt.**

#### Come si usano, in pratica

Quattro registri, ciascuno con un ruolo preciso:

| Registro | Off | Ruolo |
|---|---|---|
| `GIER` | 0x04 | abilitazione generale: se è 0, il pin `interrupt` non si alza mai, qualunque cosa sia successo |
| `IER` | 0x08 | seleziona **quali eventi** verranno registrati: bit 0 = `ap_done` (elaborazione finita), bit 1 = `ap_ready` (blocco pronto a ripartire) |
| `ISR` | 0x0C | **registra l'impulso** dell'evento avvenuto: il bit corrispondente viene messo a 1 dall'hardware e vi resta finché il software non lo azzera |
| `CTRL` | 0x00 | bit 0 `ap_start` per avviare; bit 1 `ap_done` per il polling, in alternativa all'interrupt |

La sequenza d'uso, nell'ordine corretto:

```text
1. write IER  = 0x1     // registra gli eventi di tipo ap_done
2. write GIER = 0x1     // abilita l'uscita sul pin
3. write CTRL = 0x1     // ap_start: l'IP parte
   ... al termine dell'elaborazione ISR(0) va a 1 e il pin interrupt si alza ...
4. write ISR  = 0x1     // azzera il bit: il pin torna basso
```

I punti 1 e 2 **devono precedere il punto 3**: `IER` decide se l'impulso viene
registrato nell'istante in cui avviene, e quell'istante dura un solo ciclo di
clock. Abilitare dopo l'avvio significa perdere l'evento.

Per azzerare un bit di `ISR` gli si scrive **1**, non 0. È la convenzione *toggle
on write* (TOW): la scrittura di 1 inverte il bit, che essendo a 1 torna a 0.
Documentata come tale nell'header `xaxis_scaler_hw.h` generato.

#### Chi osserva il pin `interrupt`

Il pin si alza quando **`GIER = 1` e almeno un bit di `ISR` è a 1**. Non dipende
da altro.

Va però chiarito un punto: il software non legge il pin. `interrupt` è un filo
fisico come `ap_start`, e va collegato a un **controller di interrupt**, un
componente hardware separato che sta fra la IP e il processore. È quel controller
a rilevare la transizione e a interrompere l'esecuzione della CPU, dirottandola
su una funzione di gestione registrata dal driver.

```text
ap_done  (impulso di 1 ciclo)
   └─> ISR(0) = 1            se IER(0) era già a 1
         └─> pin interrupt    se GIER = 1
               └─> controller di interrupt (esterno alla IP)
                     └─> la CPU viene interrotta, esegue l'handler
                           └─> l'handler legge ISR, poi ci scrive 1 per azzerarlo
```

La differenza rispetto al polling è tutta qui: con il polling il software
interroga ripetutamente `CTRL` bit 1; con l'interrupt il software fa altro e
viene avvisato.

**Nel nostro progetto quel controller non esiste ancora.** La IP termina al pin,
che è presente e corretto ma non collegato a nulla. Il collegamento arriverà
quando l'IP verrà inserita in un block design con un processore.

#### Perché il meccanismo è fatto così

Parti da un fatto fisico, verificato tracciando `ap_done` fino alla sua origine
nel top-level (`axis_scaler.vhd`): **`ap_done` è un impulso di un solo ciclo di
clock**, non un livello che resta alto. Il blocco finisce di elaborare, alza
`ap_done` per un ciclo, e lo riabbassa (a meno di `auto_restart`, che qui non
usiamo).

Un impulso di un ciclo è un problema per un processore: se il software non sta
guardando esattamente in quel ciclo — ed è la norma, un processore fa un milione
di altre cose — l'evento è perso. Serve qualcosa che "ricordi" che l'impulso c'è
stato, finché qualcuno non se ne accorge. Quel qualcosa è un **latch**: un bit
che l'hardware accende da solo e che resta acceso finché il software non lo
spegne esplicitamente.

**Scoperta tracciando l'RTL per intero: il banco registri ne costruisce DUE, in
parallelo, dallo stesso impulso** — uno per il polling, uno per l'interrupt:

```vhdl
-- Percorso 1: il bit CTRL[1] che leggi quando fai polling
task_ap_done      <= ap_done ...;                    -- (con auto_restart=0, è ap_done tal quale)
if (task_ap_done = '1') then int_task_ap_done <= '1';        -- si accende da solo
elsif (ar_hs='1' and raddr=ADDR_AP_CTRL) then int_task_ap_done <= '0';  -- si spegne LEGGENDO (COR)

-- Percorso 2: il bit ISR(0) che leggi/aspetti quando usi l'interrupt
if (int_ier(0) = '1' and ap_done = '1') then int_isr(0) <= '1';         -- si accende SOLO se IER era già alto
elsif (w_hs='1' and waddr=ADDR_ISR and WDATA(0)='1') then
    int_isr(0) <= int_isr(0) xor WDATA(0);           -- si spegne SCRIVENDOCI 1 (TOW)

-- Il pin fisico, ricalcolato ogni ciclo:
if (int_gie = '1' and (int_isr(0) or int_isr(1)) = '1') then int_interrupt <= '1';
else int_interrupt <= '0';
end if;
```

Due latch indipendenti, due modi di spegnersi diversi (leggere per uno,
scrivere 1 per l'altro), e **solo il secondo passa da `IER`**. Questo smentisce
la formula che si legge in giro — `interrupt = GIER & (ISR & IER)` — che
suggerisce un unico AND fra i tre. Nel VHDL vero, `IER` non tocca mai l'uscita:
decide solo se l'impulso *entra* nel latch `ISR`. Una volta dentro, il pin
dipende soltanto da `GIER` e da `ISR`.

**In sequenza, con i tempi reali** (`interrupt` è un segnale registrato, quindi
segue `ISR` con un ciclo di ritardo, che a sua volta segue `ap_done` con un
ciclo — due registri in cascata):

```text
ciclo:          N        N+1       N+2
ap_done:        1        0         0      <- l'impulso, un solo ciclo
ISR(0):         0        1         1      <- si accende un ciclo dopo (se IER=1 era già scritto)
interrupt:      0        0         1      <- si accende un ciclo dopo ISR
```

**Esempio A — la sequenza corretta.** Config e abilitazione *prima* di partire:

```text
1. write IER  = 0x1     // "mi interessano gli eventi di tipo ap_done"
2. write GIER = 0x1     // "e voglio che arrivino sul pin fisico"
3. write CTRL = 0x1     // ap_start=1 — l'IP parte
   ... elabora il pacchetto ...
   ... ap_done pulsa un ciclo, ISR(0) si accende, interrupt sale ...
4. (il pin sveglia la CPU, entra nel gestore di interrupt)
5. write ISR = 0x1      // spegne il latch (toggle on write)
   ... interrupt scende il ciclo dopo ...
6. leggi il risultato
```

**Esempio B — l'errore che perde l'evento per sempre.** Stessa sequenza, ordine
sbagliato:

```text
1. write CTRL = 0x1     // ap_start=1 — IER è ancora 0!
   ... ap_done pulsa: la condizione "IER(0)='1' and ap_done='1'" è FALSA ...
   ... ISR(0) resta a 0, per sempre: quell'impulso non esiste più ...
2. write IER  = 0x1     // troppo tardi
3. write GIER = 0x1
   ... nessun interrupt arriverà mai per questa transazione ...
```

Da qui la regola pratica: **gli interrupt si abilitano prima di `ap_start`, mai
dopo.**

**Esempio C — l'errore opposto: credere che disabilitare spenga.**

```text
   ... interrupt è alto (GIER=1, ISR(0)=1) ...
1. write IER = 0x0      // "disabilito" i futuri eventi ap_done
   ... ISR(0) non viene toccato da questa scrittura: resta 1 ...
   ... interrupt resta alto: la condizione dipende da GIER e ISR, non da IER ...
```

`IER` decide cosa entrerà in futuro, non ripulisce cosa è già dentro. Per
spegnere un interrupt pendente ci sono solo due vie: pulire `ISR` (scrivendoci
1, il modo corretto e mirato) oppure azzerare `GIER` (che maschera *tutti* gli
interrupt, anche quelli legittimi che arriveranno dopo — va bene solo se vuoi
tacitare l'intera IP).

**Se non ti serve l'interrupt, non serve nessuno di questi quattro registri
tranne `CTRL`.** Il percorso 1 (`task_ap_done` → `CTRL[1]`, *clear on read*)
esiste comunque e basta per il polling:

```text
write CTRL = 0x1              // ap_start
loop: read CTRL until bit1==1 // quella stessa lettura lo consuma (COR)
```

| Registro | Si accende quando | Si spegne quando | Serve per |
|---|---|---|---|
| `CTRL` bit1 | `ap_done` pulsa | **lo leggi** (COR) | polling |
| `IER` | scrivi tu | scrivi tu | decidere quali eventi armare `ISR` |
| `ISR` | `ap_done`/`ap_ready` pulsa, **e** il bit `IER` corrispondente era già alto | **ci scrivi 1** (TOW) | far salire il pin `interrupt` |
| `GIER` | scrivi tu | scrivi tu | interruttore generale, maschera il pin senza toccare `ISR` |
| `interrupt` (pin) | `GIER=1` e almeno un bit di `ISR` è alto | `GIER=0`, oppure pulisci tutto `ISR` | sveglia la CPU |

### Il costo in risorse

|  | FF | LUT | DSP | BRAM | II | Estimated |
|---|---|---|---|---|---|---|
| 1.1 | 4 | 12 | 0 | 0 | 1 | 0.761 ns |
| 1.2 | **40** | **52** | 0 | 0 | **1** | **0.761 ns** |

(«Estimated» è la colonna omonima della tabella *Timing* del report: la stima
del **percorso critico**, da confrontare con il target di 4,00 ns. Non è lo
slack — più piccola è, meglio è.)

Il report attribuisce l'aumento a un'unica istanza:

```text
|   Instance   |   Module   | BRAM_18K| DSP| FF | LUT| URAM|
|ctrl_s_axi_U  |ctrl_s_axi  |        0|   0|  36|  40|    0|
```

36 FF e 40 LUT per un banco registri con quattro registri: è il prezzo onesto
di un'interfaccia AXI4-Lite. **Ma timing e throughput sono identici**: stesso
percorso critico stimato, stesso `II = 1`, stessa *iteration latency* di 2 cicli. Il controllo
software è un blocco a lato, non è sul percorso dei dati. È un fatto
architetturale che vale la pena notare: rendere una IP pilotabile da software non
la rallenta.

### La C simulation è cieca alle interfacce

`make csim` è passato senza toccare il testbench, con output **identico** al
gradino 1.1. Non è una svista: in simulazione C non esistono bus, indirizzi né
`ap_start`. Chiamare `axis_scaler(s_axis, m_axis)` *è* l'equivalente astratto di
"scrivi 1 in `CTRL` bit 0 e aspetta `CTRL` bit 1".

> La C simulation verifica l'**algoritmo**, non le **interfacce**.

Se sbagli un pragma di interfaccia, csim resta verde. Le interfacce le verificano
la C/RTL cosimulation (Fase 3) e la simulazione del block design con gli AXI VIP
(Fase 5), dove i registri verranno scritti davvero attraverso il bus.

### Il primo `_hw.h`

```c
// 0x0 : Control signals
//       bit 0  - ap_start (Read/Write/COH)
//       bit 1  - ap_done (Read/COR)
//       bit 2  - ap_idle (Read)
//       bit 3  - ap_ready (Read/COR)
//       bit 7  - auto_restart (Read/Write)
//       bit 9  - interrupt (Read)
// 0x4 : Global Interrupt Enable Register
// 0x8 : IP Interrupt Enable Register (Read/Write)
// 0xc : IP Interrupt Status Register (Read/TOW)
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XAXIS_SCALER_CTRL_ADDR_AP_CTRL 0x0
#define XAXIS_SCALER_CTRL_ADDR_GIE     0x4
#define XAXIS_SCALER_CTRL_ADDR_IER     0x8
#define XAXIS_SCALER_CTRL_ADDR_ISR     0xc
```

Il prefisso `XAXIS_SCALER_CTRL_` viene dal nome del bundle: `bundle=ctrl` →
`..._CTRL_...`. Ecco perché conviene scegliere il nome del bundle e congelarlo:
finisce nei nomi del driver software, non solo nella porta del block design.

Le sigle in fondo sono il vocabolario dei registri hardware AMD e vanno lette con
attenzione, perché descrivono **comportamenti**, non permessi:

| Sigla | Significato | Conseguenza per il software |
|---|---|---|
| COH | *clear on handshake* | scrivi 1 e basta, l'hardware azzera da sé (`ap_start`) |
| COR | *clear on read* | **leggere è distruttivo**: `ap_done` letto una volta è consumato |
| TOW | *toggle on write* | per pulire un bit ci scrivi **1**, non 0 (`ISR`) |
| SC | *self clear* | si azzera da solo dopo un ciclo |

`COR` su `ap_done` merita un avvertimento: se nel debug leggi `CTRL` "per
guardare" e poi il driver lo rilegge, il secondo lettore trova `ap_done` a zero e
aspetta per sempre. È un classico.

Nota di flusso: **questo header è già stato generato da `make csynth`**, non è
servito `make ip`. Con `flow_target=vivado` il packaging gira automaticamente in
coda alla sintesi (nel log: `INFO: [IMPL 213-8] Exporting RTL as a Vivado IP`), e
lo stesso file compare in tre copie identiche:

```text
impl/ip/drivers/axis_scaler_v1_0/src/xaxis_scaler_hw.h    <- quella dell'IP
impl/misc/drivers/axis_scaler_v1_0/src/xaxis_scaler_hw.h
.autopilot/db/driver/src/xaxis_scaler_hw.h                <- interna al tool
```

Accanto ci sono già anche `xaxis_scaler.h` / `.c` (il driver bare-metal) e
`xaxis_scaler_linux.c`. Li leggeremo in Fase 4, quando la mappa registri sarà
completa.

### Un solo `ap_clk` per tutto: e se volessi clock diversi?

Guardando la entity si nota una cosa che vale la pena tenere a mente: **c'è un
solo `ap_clk`**, e lo condividono il percorso dati (`s_axis`/`m_axis`) e il bus
di controllo (`s_axi_ctrl`). È il comportamento di default di un component HLS:
un dominio di clock, uno solo.

Nella pratica la domanda nasce subito. Se la IP serve ad accelerare un
algoritmo, vuoi "pompare" sullo stream alla frequenza più alta possibile,
mentre il bus di controllo — che tocchi quattro volte per transazione — non ha
nessun bisogno di correre. Frequenze diverse per le due interfacce.

**Si può fare, e HLS ha un parametro apposta.** Ma il vincolo che ci sta dietro
è la parte importante da ricordare.

#### 1. AXI4-Lite su clock separato, dentro HLS

Il pragma di interfaccia accetta `clock=`:

```cpp
#pragma HLS INTERFACE mode=s_axilite port=gain bundle=ctrl clock=ctrl_clk
```

Dalla documentazione ufficiale (UG1399, *pragma HLS interface*):

> *"By default, the AXI4-Lite interface clock is the same clock as the system
> clock. This option is used to specify a separate clock for an AXI4-Lite
> interface."*

Se i registri stanno in un `bundle`, basta indicare `clock=` su **un solo**
membro del bundle.

Sembra la soluzione completa, ma non lo è, e il perché sta in due righe della
pagina *Control Clock and Reset in AXI4-Lite Interfaces*:

> *"AXI4-Lite interface clock must be synchronous to the clock used for the
> synthesized logic (`ap_clk`). That is, both clocks must be derived from the
> same master generator clock."*
>
> *"AXI4-Lite interface clock frequency must be equal to or less than the
> frequency of the clock used for the synthesized logic (`ap_clk`)."*

**Cioè: non sono due clock indipendenti.** Devono uscire dallo stesso
MMCM/PLL, con un rapporto di frequenza definito (in pratica una divisione
pulita), e AXI4-Lite non può mai essere più veloce del clock del datapath.

Questo spiega la domanda naturale — *"e i sincronizzatori sui registri?"*.
Passare un registro multi-bit fra due domini asincroni richiede più di un
doppio flip-flop per bit: serve garantire che tutti i bit arrivino **insieme**
(handshake, o aggiornamento solo quando il valore è stabile), altrimenti il
lato lento può campionare una combinazione di bit vecchi e nuovi che non è mai
esistita.

La documentazione **non descrive nessuna logica di sincronizzazione inserita da
HLS** per questo caso: né sincronizzatori, né handshake. E il motivo è
esattamente il vincolo qui sopra — imponendo che i due clock nascano dallo
stesso generatore, la relazione di fase fra i fronti è nota e stabile
(caso *mesocrono*), e il problema di metastabilità non si presenta per
costruzione. **Il vincolo non è una scocciatura burocratica: è il meccanismo
stesso con cui il problema viene evitato.** Garantirlo a monte, nel clocking
wizard, è responsabilità di chi progetta il sistema, non del tool.

#### 2. AXI4-Lite su un clock davvero indipendente

Se i due clock devono essere scorrelati (oscillatori diversi, o domini che non
condividono il PLL), la soluzione non sta più dentro HLS ma un livello sopra.
Dalla stessa pagina:

> *"Vivado IP integrator will automatically generate a clock domain crossing
> (CDC) slice that performs the same function as the control clock described
> below, making use of the option unnecessary."*

Collegando `s_axi_ctrl` a un segmento di interconnect con un clock diverso,
**è Vivado IP Integrator a inserire da sé una vera slice di CDC** fra
l'interconnect e la IP — un componente verificato, esterno all'RTL generato da
HLS. A quel punto `clock=` non serve nemmeno più: lo dice il documento.

#### 3. Lo stream stesso fra due domini scorrelati

Caso diverso e più pesante: non il controllo, ma **i dati** che devono
attraversare un confine di clock — una IP a monte a una frequenza, il DMA o la
IP a valle a un'altra, senza relazione fra le due.

Questo non si risolve con un pragma dentro una singola top function: le porte
`axis` vivono su `ap_clk`, punto. Si risolve istanziando nel block design un
componente dedicato, l'**AXI4-Stream Clock Converter** (della AXI4-Stream
Infrastructure IP Suite, PG085), che è una FIFO asincrona con i sincronizzatori
progettati e verificati per quel compito.

#### In sintesi

| Scenario | Dove si risolve | Meccanismo |
|---|---|---|
| AXI4-Lite più lento, **sincrono** e derivato dallo stesso generatore | dentro HLS | `clock=` sul bundle. Nessun CDC reale: è un vincolo di progetto che evita il problema |
| AXI4-Lite su clock **realmente indipendente** | block design | CDC slice inserita automaticamente da Vivado IP Integrator |
| **Lo stream** fra due domini scorrelati | block design | AXI4-Stream Clock Converter (IP dedicata, FIFO asincrona) |

La nostra `axis_scaler` sta nel caso più semplice — un `ap_clk` per tutto — e
così resta: il multi-clock non fa parte della scala didattica. È annotato qui
perché è la prima domanda che si pone chi viene dall'hardware guardando quella
entity, e perché il vincolo del generatore comune è la cosa da ricordare.

---

## 6. Cosa abbiamo osservato al gradino 1.3

Modifica: **un argomento e un pragma**.

```diff
  void axis_scaler(hls::stream<pkt_t> &s_axis,
-                  hls::stream<pkt_t> &m_axis);
+                  hls::stream<pkt_t> &m_axis,
+                  int                 gain);

+ #pragma HLS INTERFACE mode=s_axilite port=gain bundle=ctrl

- // pass-through
+ campione.data = campione.data * gain;
```

È il primo gradino in cui il codice C produce **aritmetica**, non solo fili e
handshake. E il primo in cui cambia la firma della funzione.

### Il registro è nato a 0x10 — e non l'abbiamo deciso noi

`xaxis_scaler_hw.h`, generato:

```c
// 0x10 : Data signal of gain
//        bit 31~0 - gain[31:0] (Read/Write)
// 0x14 : reserved

#define XAXIS_SCALER_CTRL_ADDR_GAIN_DATA 0x10
#define XAXIS_SCALER_CTRL_BITS_GAIN_DATA 32
```

Da nessuna parte, nel nostro codice, è scritto `0x10`. L'offset lo ha scelto
HLS in base a due regole:

1. gli offset `0x00`–`0x0C` sono riservati al blocco di controllo standard AMD
   (`CTRL`/`GIER`/`IER`/`ISR`), quindi i registri utente partono da `0x10`;
2. a parità di tutto il resto, conta **l'ordine degli argomenti della funzione
   C**: il primo scalare mappato su `s_axilite` prende il primo offset libero.

> **L'ordine degli argomenti C *è* la mappa registri.**
> Scambiare due argomenti scambia due indirizzi. Il codice compila, l'IP parte,
> e il driver software scrive nel registro sbagliato — un bug che non dà
> nessun segnale, perché formalmente non c'è niente di illegale.

Da qui due regole di progetto, che valgono da adesso in poi:

- l'ordine degli argomenti si **congela** e si documenta;
- gli offset **non si scrivono a mano**: si leggono da `xaxis_scaler_hw.h`.

Nota il `0x14 : reserved`. Nel VHDL generato quello slot ha già un nome:

```vhdl
constant ADDR_GAIN_DATA_0 : INTEGER := 16#10#;
constant ADDR_GAIN_CTRL   : INTEGER := 16#14#;
```

Per un ingresso resta inutilizzato. Al gradino 1.4, con il primo registro di
**stato**, uno slot `_CTRL` come questo ospiterà il bit `_ap_vld`.

### La entity: nessun pin nuovo, ma lo spazio indirizzi raddoppia

```diff
 entity axis_scaler is
 generic (
-    C_S_AXI_CTRL_ADDR_WIDTH : INTEGER := 4;
+    C_S_AXI_CTRL_ADDR_WIDTH : INTEGER := 5;
     C_S_AXI_CTRL_DATA_WIDTH : INTEGER := 32 );
```

**È l'unica riga cambiata in tutta la entity.** Vale la pena fermarsi un
momento: al gradino 1.2 avevamo previsto che questo numero sarebbe cresciuto, ed
è cresciuto. 4 bit indirizzavano 16 byte (i quattro registri di controllo); con
`gain` a `0x10` e il suo slot `_CTRL` a `0x14` servono più di 16 byte, quindi il
bus passa a 5 bit = 32 byte.

E soprattutto: **un registro di configurazione non aggiunge pin al modulo.**
Aggiunge spazio di indirizzamento dentro un bus che c'era già. È la differenza
pratica fra "parametro configurabile da software" e "porta hardware": in VHDL
avresti dovuto scegliere fra un `generic` (fisso alla sintesi) e una porta in
più (un altro fascio di fili da instradare). Qui la terza via — un registro —
costa indirizzi, non piedini.

Il log di sintesi lo riassume:

```text
INFO: [RTGEN 206-500] Setting interface mode on port 'axis_scaler/gain'
                      to 's_axilite & ap_none'.
INFO: [RTGEN 206-100] Bundling port 'gain' and 'return' to AXI-Lite port ctrl.
```

Stesso schema del gradino 1.2 (`s_axilite & ap_ctrl_hs`): `s_axilite` dice *da
dove si raggiunge*, `ap_none` dice *che protocollo ha il segnale interno* —
nessuno, è un valore stabile, senza handshake. Ed entrambi finiscono nello
stesso `bundle=ctrl`, cioè nello stesso banco.

### Il file nuovo: un moltiplicatore, con il nome che si spiega da solo

In `syn/vhdl/` è comparso un quinto file:

```text
axis_scaler_mul_32s_32s_32_2_1.vhd
                └─┬─┘ └─┬─┘ └┬┘ │ │
                  │     │    │  │ └─ variante
                  │     │    │  └─── 2 stadi di pipeline (2 cicli di latenza)
                  │     │    └────── uscita a 32 bit
                  │     └─────────── secondo operando: 32 bit signed
                  └───────────────── primo operando:  32 bit signed
```

HLS non ha scritto `a * b` sperando che il sintetizzatore se la cavi: ha
**istanziato un modulo dedicato**, dimensionato sugli operandi reali e
pipelinato su 2 stadi per rientrare nel periodo di clock. È il tipo di scelta
che in VHDL avresti dovuto fare tu, decidendo a mano quanti stadi di registri
mettere e dove.

Il modulo porta anche un attributo che vale la pena notare:

```vhdl
attribute keep_hierarchy of axis_scaler_mul_32s_32s_32_2_1: entity is "yes";
```

HLS chiede a Vivado di **non sciogliere questa gerarchia** durante la sintesi
logica, per non perdere il mapping sui DSP che ha pianificato.

### Il costo: ora c'è silicio vero

| | DSP | FF | LUT | II | Iter. latency | Estimated | Fmax |
|---|---|---|---|---|---|---|---|
| 1.2 | 0 | 40 | 52 | 1 | 2 | 0,761 ns | 1314 MHz |
| 1.3 | **4** | **223** | **166** | **1** | **4** | **2,238 ns** | **447 MHz** |

Il dettaglio per istanza dice dove sono finite le risorse:

```text
|        Instance       |       Module       | BRAM_18K| DSP| FF | LUT | URAM|
|ctrl_s_axi_U           |ctrl_s_axi          |        0|   0|  74|  104|    0|
|mul_32s_32s_32_2_1_U1  |mul_32s_32s_32_2_1  |        0|   4|  46|   42|    0|
```

Tre letture, in ordine di importanza.

**a) Sono comparsi 4 DSP.** Una moltiplicazione 32×32 con segno non entra in un
singolo DSP58: il tool la decompone in quattro moltiplicazioni parziali più le
somme. È il primo gradino in cui il nostro C consuma una risorsa *aritmetica*
dedicata e non solo logica generica.

**b) Il banco registri è cresciuto**, da 36/40 a 74/104 FF/LUT: sono il registro
`gain` a 32 bit e la logica di decodifica del suo indirizzo. Coerente:
un registro in più costa circa 32 flip-flop più il contorno.

**c) Il timing è peggiorato molto, e non è un problema.** Il percorso critico
stimato passa da 0,761 ns a 2,238 ns, cioè la Fmax stimata crolla da 1314 a
447 MHz. La ragione è
strutturale: al gradino 1.2 il banco registri stava **a lato** del percorso dati,
mentre il moltiplicatore ci sta **in mezzo**. Ma 447 MHz sono comunque quasi il
doppio dei 250 MHz che abbiamo chiesto, quindi il vincolo è rispettato con
margine e il report dice `All loop constraints were satisfied`.

**La cosa importante è che `II` è rimasto 1.** La *iteration latency* è
raddoppiata (2 → 4 cicli: sono i due stadi del moltiplicatore che si aggiungono),
ma continua a entrare un campione **ogni** ciclo. Latenza e throughput sono
grandezze indipendenti: abbiamo allungato la catena di montaggio, non
rallentato il nastro.

### Il registro in VHDL: `wmask`, cioè i byte enable

La logica di scrittura di `gain` nel banco registri:

```vhdl
if (w_hs = '1' and waddr = ADDR_GAIN_DATA_0) then
    int_gain(31 downto 0) <= (UNSIGNED(WDATA(31 downto 0)) and wmask(31 downto 0))
                          or ((not wmask(31 downto 0)) and int_gain(31 downto 0));
end if;
```

`wmask` è costruito espandendo `WSTRB` a livello di bit. Tradotto: *i bit
selezionati prendono il valore nuovo, gli altri conservano il vecchio*. È la
gestione corretta delle scritture parziali — se il processore scrive un solo
byte del registro, gli altri tre non vengono toccati. È il pezzo che si sbaglia
quasi sempre scrivendo uno slave AXI4-Lite a mano.

### Il testbench: il primo golden model vero

Fino al 1.2 il risultato atteso era banalmente l'ingresso. Ora il testbench
deve saper calcolare per conto proprio, e qui compare una regola di metodo che
varrà per tutti i gradini successivi:

> **Il golden model non deve assomigliare al DUT.**

Scrivere nel testbench la stessa riga del sorgente sintetizzato non verifica
nulla: se è sbagliata, è sbagliata identica nei due posti e il test passa. Nel
nostro `tb/` il modello prende un'altra strada — aritmetica `long long` a 64 bit
in C puro, poi troncamento esplicito ai 32 bit bassi — senza passare dai tipi
`ap_int`. Se i due percorsi coincidono, il comportamento è quello che crediamo.

Sette casi, su due dimensioni ortogonali: lunghezze del pacchetto (1, 8, 17) e
valori del gain (1, 0, negativo, enorme). Due meritano una nota:

- **`gain = 1`** è il caso identità: l'IP deve comportarsi esattamente come il
  pass-through del gradino 1.2. È un controllo di non-regressione fra gradini.
- **`gain = 100000000`** manda il prodotto fuori dai 32 bit. Non ci aspettiamo
  un risultato matematicamente giusto: ci aspettiamo **esattamente i 32 bit
  bassi**, che è ciò che l'hardware produce, ed è l'equivalente di
  `y <= resize(x * gain, 32);` in VHDL.

Quel test documenta il troncamento invece di evitarlo. Quando al gradino 1.8
aggiungeremo la saturazione, **dovrà fallire** — e il fatto che fallisca sarà la
prova che la saturazione funziona.

---

## 7. Cosa abbiamo osservato al gradino 1.4

Modifica: **un argomento con un asterisco, un pragma, e una riga in fondo**.

```diff
  void axis_scaler(hls::stream<pkt_t> &s_axis,
                   hls::stream<pkt_t> &m_axis,
-                  int                 gain);
+                  int                 gain,
+                  int                *sample_count);

+ #pragma HLS INTERFACE mode=s_axilite port=sample_count bundle=ctrl

+ int conteggio = 0;
  copia_pacchetto:
  while (!ultimo) {
      ...
+     conteggio++;
  }
+ *sample_count = conteggio;
```

È il primo gradino in cui l'informazione torna **indietro**: dall'hardware al
software, senza passare dallo stream.

Il log di sintesi lo dice in una riga, e mette i due registri uno accanto
all'altro:

```text
INFO: [RTGEN 206-500] Setting interface mode on port 'axis_scaler/gain'
                      to 's_axilite & ap_none'.
INFO: [RTGEN 206-500] Setting interface mode on port 'axis_scaler/sample_count'
                      to 's_axilite & ap_vld'.
```

Stesso pragma, stesso bundle, protocollo interno **diverso**: `ap_none` per
l'ingresso, `ap_vld` per l'uscita. Non l'abbiamo chiesto noi da nessuna parte:
il tool l'ha dedotto dall'asterisco nella firma della funzione.

### Lo slot `_CTRL` non è più "reserved"

`xaxis_scaler_hw.h`, diff rispetto al gradino 1.3:

```diff
  // 0x10 : Data signal of gain
  //        bit 31~0 - gain[31:0] (Read/Write)
  // 0x14 : reserved
+ // 0x18 : Data signal of sample_count
+ //        bit 31~0 - sample_count[31:0] (Read)
+ // 0x1c : Control signal of sample_count
+ //        bit 0  - sample_count_ap_vld (Read/COR)
+ //        others - reserved

+ #define XAXIS_SCALER_CTRL_ADDR_SAMPLE_COUNT_DATA 0x18
+ #define XAXIS_SCALER_CTRL_BITS_SAMPLE_COUNT_DATA 32
+ #define XAXIS_SCALER_CTRL_ADDR_SAMPLE_COUNT_CTRL 0x1c
```

Tre cose, in ordine.

**a) `gain` è rimasto a 0x10.** Abbiamo aggiunto l'argomento nuovo *in fondo*
alla firma, e infatti nessun offset esistente si è mosso. È la regola
**append-only** delle mappe registri: si aggiunge in coda, non si rimescola,
esattamente come si estende un protocollo di rete o un formato di file. Più
avanti (esperimento B, qui sotto) vedremo cosa succede quando la si viola.

**b) `sample_count` è `(Read)`, `gain` era `(Read/Write)`.** Il software può
leggere il registro di stato ma non scriverlo. Anche questo non l'abbiamo
dichiarato: discende dall'asterisco.

**c) Lo slot 0x1c esiste davvero, e contiene un bit.** Al gradino 1.3 avevamo
notato `0x14 : reserved` e lasciato la domanda aperta. Ora si vede a cosa serve
quella coppia di slot: HLS alloca **8 byte per ogni registro utente**, dato +
controllo. Per un ingresso il secondo non serve e resta vuoto; per un'uscita
ospita `<nome>_ap_vld`, documentato `Read/COR`.

`COR` è il vocabolario del gradino 1.2: **clear on read**. Leggere quel bit lo
consuma. Vale la stessa avvertenza di `ap_done`: se lo leggi "per guardare" in
debug, il driver che lo rilegge dopo lo trova a zero.

### La entity non cambia — e nemmeno il generic

```text
diff entity axis_scaler (1.3)  ->  entity axis_scaler (1.4):  nessuna differenza
```

**Zero righe cambiate.** Al gradino 1.3 avevamo previsto che `C_S_AXI_CTRL_ADDR_WIDTH`
sarebbe cresciuto, e in effetti era passato da 4 a 5. Stavolta la previsione era
che *non* cresce, e il conto lo diceva prima della sintesi: 5 bit indirizzano 32
byte, cioè 0x00–0x1F, e il nostro registro più alto sta a 0x1C. Ci stiamo dentro
esattamente. Il VHDL lo conferma:

```vhdl
constant ADDR_SAMPLE_COUNT_DATA_0 : INTEGER := 16#18#;
constant ADDR_SAMPLE_COUNT_CTRL   : INTEGER := 16#1c#;
constant ADDR_BITS                : INTEGER := 5;     -- invariato
```

> Un registro — di configurazione o di stato — non aggiunge **piedini**.
> Aggiunge **indirizzi** dentro un bus che c'era già, e solo quando gli
> indirizzi finiscono cresce il bus.

Dove invece qualcosa cambia è un livello più sotto, nella entity del banco
registri. Ed è il diff più istruttivo del gradino:

```diff
 entity axis_scaler_ctrl_s_axi is
 port (
     ...
     gain                  :out  STD_LOGIC_VECTOR(31 downto 0);
+    sample_count          :in   STD_LOGIC_VECTOR(31 downto 0);
+    sample_count_ap_vld   :in   STD_LOGIC;
     ap_start              :out  STD_LOGIC;
```

Guarda le direzioni, **dal punto di vista del banco registri**:

| | direzione | chi scrive | chi legge |
|---|---|---|---|
| `gain` | `out` | il canale di scrittura AXI (WDATA) | il datapath |
| `sample_count` | `in` | il datapath | il canale di lettura AXI (RDATA) |

Lo stesso pragma, la stessa parola `bundle=ctrl`, e due fili che vanno in
direzioni opposte. È la prova fisica che **il pragma dice dove, il C dice in che
verso**.

E c'è un filo in più: `sample_count_ap_vld`. Il dato da solo non basta.

### Il VHDL del banco registri: due processi speculari

Vale la pena metterli uno sotto l'altro, perché sono lo stesso registro visto
da due lati.

```vhdl
-- gain: il BUS scrive, sotto maschera di byte (gradino 1.3)
if (w_hs = '1' and waddr = ADDR_GAIN_DATA_0) then
    int_gain <= (UNSIGNED(WDATA) and wmask) or ((not wmask) and int_gain);
end if;

-- sample_count: il DATAPATH scrive, quando alza il suo valido
if (sample_count_ap_vld = '1') then
    int_sample_count <= UNSIGNED(sample_count);
end if;
```

Nessun `wmask` nel secondo: i byte enable servono a gestire le scritture
parziali che arrivano dal processore, e il datapath non fa scritture parziali —
deposita sempre la parola intera.

Poi c'è il terzo processo, quello nuovo per davvero:

```vhdl
process (ACLK)
begin
    if (ACLK'event and ACLK = '1') then
        if (ARESET = '1') then
            int_sample_count_ap_vld <= '0';
        elsif (ACLK_EN = '1') then
            if (sample_count_ap_vld = '1') then
                int_sample_count_ap_vld <= '1';
            elsif (ar_hs = '1' and raddr = ADDR_SAMPLE_COUNT_CTRL) then
                int_sample_count_ap_vld <= '0';   -- clear on read
            end if;
        end if;
    end if;
end process;
```

**Questo l'abbiamo già visto.** È lo stesso identico idioma di `int_task_ap_done`
del gradino 1.2 (§5): un latch che l'hardware accende con un impulso di un ciclo
e che il software spegne leggendolo. Stessa struttura, stesso motivo — un
impulso di un ciclo di clock è invisibile a un processore, quindi qualcuno deve
ricordarselo.

Messa così, la cosa si semplifica parecchio: `_ap_vld` **non è un concetto
nuovo**. È il `TVALID` di AXI4-Stream, o il `ap_done` del blocco, applicato a un
singolo registro. In tutti e tre i casi la domanda a cui risponde è la stessa:
*quello che sto guardando, è roba vera?*

Nota che il registro **dato** (0x18) non è COR: puoi rileggerlo quante volte
vuoi, resta lì. È solo il bit di validità (0x1c) che si consuma leggendolo.
Il che dà al software due modi di lavorare:

```text
polling classico:   aspetta CTRL[1] (ap_done), poi leggi 0x18
con il valido:      leggi 0x1c; se bit0 = 1, il valore a 0x18 è nuovo
```

### Quando viene scritto: la prova, non l'intenzione

La domanda del gradino era *perché il registro di stato si aggiorna all'`ap_done`
e non durante*. La risposta sta in due processi del top level, che vanno letti
insieme:

```vhdl
-- l'impulso che carica il registro di stato
sample_count_ap_vld_assign_proc : process(...)
    if ((ap_loop_exit_ready_pp0_iter2_reg = '1') and
        (phi_ln203_reg_123_pp0_iter1_reg = '0') and ...) then
        sample_count_ap_vld <= '1';

-- il segnale di fine transazione
ap_done_int_assign_proc : process(...)
    if ((ap_loop_exit_ready_pp0_iter2_reg = '1') and ...) then
        ap_done_int <= '1';
```

**Lo stesso termine, `ap_loop_exit_ready_pp0_iter2_reg`, comanda tutti e due.**
`phi_ln203` è il predicato di continuazione del `while` (riga 203 del sorgente),
quindi la condizione dice: *siamo all'iterazione in cui il loop non continua*,
cioè quella con `TLAST`.

E qui sta il punto che vale la pena non fraintendere:

> HLS non ha una regola "i registri di stato si aggiornano all'`ap_done`".
> Ha una regola molto più semplice: **il registro viene scritto dove sta
> l'assegnazione nel C**. Siccome la nostra assegnazione è l'ultima riga della
> funzione, lo scheduler la mette dove la funzione finisce — e quello è, per
> definizione, `ap_done`.

La differenza è pratica, non filosofica: significa che la posizione di quella
riga nel sorgente **è** una specifica temporale dell'hardware. Che è esattamente
la cosa che a chi viene dal VHDL sembra troppo bella per essere vera, e per
questo è meglio verificarla che crederci.

### Esperimento A — e se scrivessimo il registro dentro il loop?

Sintetizzata in scratchpad la variante con `*sample_count = conteggio;` dentro
il ciclo, tutto il resto identico. Il diff è tutto nella condizione del valido:

```vhdl
-- assegnazione DOPO il loop (il nostro 1.4): un impulso per TRANSAZIONE
if ((ap_loop_exit_ready_pp0_iter2_reg = '1') and (phi_ln203_reg_..._iter1_reg = '0') ...)

-- assegnazione DENTRO il loop:              un impulso per BEAT
if ((ap_enable_reg_pp0_iter2 = '1') and (phi_ln203_reg_..._iter1_reg = '1') ...)
```

Nel secondo caso sparisce il riferimento all'uscita dal loop e compare
`ap_enable_reg_pp0_iter2`, cioè "la pipeline sta lavorando": il valido pulsa a
**ogni campione**, non una volta sola.

Il risultato dell'esperimento è però più interessante di così, e in due modi
opposti a quello che verrebbe da pensare.

**Non costa di più.** Anzi:

| | FF | LUT |
|---|---|---|
| assegnazione fuori dal loop (1.4) | 298 | **343** |
| assegnazione dentro il loop | 299 | **313** |

Trenta LUT in meno. L'argomento "scrivilo fuori dal loop perché costa meno" è
semplicemente falso, e se l'avessimo scritto senza provarlo sarebbe finito in
questa documentazione come una cosa vera.

**E non è nemmeno sbagliato.** Il valore finale nel registro è lo stesso: HLS
genera esattamente *n* impulsi, uno per beat, e l'ultimo deposita *n*.

Quello che si rompe è il **significato del bit di validità**. `_ap_vld` è un
latch che si accende al primo impulso e resta acceso: se pulsa a ogni beat, si
accende al primo campione del pacchetto. Un software che lo legge per sapere se
il risultato è pronto riceve "sì" quando l'elaborazione è appena cominciata, e
va a leggere un conteggio parziale. Il bit smette di voler dire *il risultato è
pronto* e passa a voler dire *è passato almeno un campione* — che non è
l'informazione che serve a nessuno.

> La ragione per scrivere un registro di stato fuori dal loop è **semantica**,
> non di risorse. E vale la pena saperlo per il verso giusto: l'intuizione
> hardware ("un percorso attivo tutto il pacchetto deve costare") qui sbaglia.

### Esperimento B — e se usassimo il valore di ritorno?

L'altra domanda del gradino: *perché un'uscita deve essere un puntatore?* La
risposta non è "perché non si può fare altrimenti". Sintetizzata anche questa
variante, con `int axis_scaler(...)` che ritorna il conteggio:

```c
// xaxis_scaler_hw.h  --  variante con il valore di ritorno
// 0x10 : Data signal of ap_return
//        bit 31~0 - ap_return[31:0] (Read)
// 0x18 : Data signal of gain
//        bit 31~0 - gain[31:0] (Read/Write)
// 0x1c : reserved
```

Funziona: HLS crea un registro `ap_return`. Ma guarda cosa è successo davvero.

**1) `gain` si è spostato da 0x10 a 0x18.** Il valore di ritorno si prende il
primo offset utente, **prima di tutti gli argomenti**. Cambiando la firma da
puntatore a `return` abbiamo spostato l'indirizzo di un registro che non
avevamo toccato: è esattamente il bug del gradino 1.3, con la differenza che
qui nemmeno ci si accorge di aver riordinato qualcosa. Il codice compila, l'IP
parte, e il driver scrive il guadagno in un registro di sola lettura.

**2) Non c'è nessun `_ap_vld`.** Lo slot 0x14 resta vuoto, e il software non ha
nessun modo di sapere se il valore letto è fresco. Il registro di stato di
prima classe è quello col puntatore; `ap_return` è la versione povera.

**3) In compenso il momento della scrittura è esplicito**, e questo chiude il
discorso del paragrafo precedente nel modo più diretto possibile:

```vhdl
-- variante con il return: il banco registri usa ap_done COME ENABLE
if (ap_done = '1') then
    int_ap_return <= UNSIGNED(ap_return);
end if;
```

Non uno schedule che *coincide* con `ap_done`: proprio il segnale `ap_done`
cablato all'enable del registro.

Quindi i due meccanismi, affiancati:

| | chi carica il registro | c'è `_ap_vld`? | offset |
|---|---|---|---|
| `int *sample_count` | un impulso generato dallo scheduler dove sta l'assegnazione | **sì**, con latch COR | dopo gli argomenti precedenti |
| `return conteggio` | il segnale `ap_done`, cablato | no | **prima** di tutti gli argomenti |

E le tre ragioni per il puntatore, in ordine: il `return` è **uno solo** (avremo
sette registri di stato), non ha **valido**, e occupa un offset che **sposta
tutti gli altri**.

Un'ultima nota sulla parola `return`, che è un omonimo sfortunato: nel pragma

```cpp
#pragma HLS INTERFACE mode=s_axilite port=return bundle=ctrl
```

`port=return` **non** indica il valore restituito, indica la funzione nel suo
insieme, cioè il protocollo a livello di blocco (`ap_ctrl_hs`, gradino 1.2). Le
due cose finiscono nello stesso bundle ma non sono la stessa cosa, e nei forum
questa confusione gira parecchio.

### Il contatore è a 31 bit, non a 32

Dettaglio piccolo ma molto "HLS". Abbiamo scritto `int conteggio`, cioè 32 bit
con segno. Nel VHDL generato:

```vhdl
signal conteggio_fu_78     : STD_LOGIC_VECTOR (30 downto 0);          -- 31 bit
conteggio_1_fu_147_p2 <= std_logic_vector(unsigned(...) + unsigned(ap_const_lv31_1));
sample_count <= std_logic_vector(resize(unsigned(conteggio_fu_78), 32));
```

e nel report:

```text
|conteggio_1_fu_147_p2  |  +  | 0| 0| 31|   31  |   1  |     <- sommatore a 31 bit
|conteggio_fu_78        |     |31| 0| 31|                     <- 31 flip-flop
```

Il tool ha **ridotto la larghezza** del registro. Il ragionamento: la variabile
parte da 0 e viene solo incrementata, e in C l'overflow di un intero con segno è
comportamento indefinito — quindi il tool può assumere che non accada, e
concludere che il valore non è mai negativo. Bit 31 sempre zero ⇒ non serve
tenerlo. Quando il valore esce verso il banco registri viene esteso a 32 con uno
zero davanti.

È una differenza culturale netta rispetto al VHDL: lì se dichiari
`unsigned(31 downto 0)` ottieni 32 flip-flop, punto. Qui il tool ti dà quello che
riesce a dimostrare che ti serve. Comodo, ma con una conseguenza reale da sapere:
**questo contatore avvolge a 2^31, non a 2^32** — un pacchetto di 2,1 miliardi di
beat, che a 250 MHz sono 8,6 secondi di stream continuo. Remoto, non impossibile:
è il genere di limite che va scritto nella scheda tecnica di una IP, ed è
imparentato con il watchdog del gradino 1.9.

### Il costo

| | DSP | FF | LUT | II | Iter. latency | Estimated | Fmax |
|---|---|---|---|---|---|---|---|
| 1.2 | 0 | 40 | 52 | 1 | 2 | 0,761 ns | 1314 MHz |
| 1.3 | 4 | 223 | 166 | 1 | 4 | 2,238 ns | 447 MHz |
| 1.4 | 4 | **298** | **343** | **1** | **4** | **2,238 ns** | **446,8 MHz** |

**Il timing non si è mosso di un picosecondo.** Stesso percorso critico: è
ancora il moltiplicatore del gradino 1.3. Un sommatore a 31 bit e un registro in
più non lo toccano nemmeno da lontano. Anche `II` e *iteration latency* sono
invariati: il contatore ha una dipendenza portata dal loop (dipende da se stesso
all'iterazione precedente), ma un'addizione sta comodamente in un ciclo, quindi
l'incremento non ha costretto lo scheduler ad allargare l'II. Continua a entrare
un campione ogni colpo di clock.

Dove sono finiti i +75 FF e i +177 LUT:

```text
                            1.3           1.4          differenza
ctrl_s_axi_U            74 FF / 104 LUT   112 / 168    +38 FF / +64 LUT
Expression               0 FF /   6 LUT     0 /  51            +45 LUT
Multiplexer              0 FF /   4 LUT     0 /  70            +66 LUT
Register               103 FF /  10 LUT   140 /  12    +37 FF /  +2 LUT
```

- **ctrl_s_axi (+38 FF)**: il registro dato a 32 bit, il latch `_ap_vld`, e la
  decodifica dei due indirizzi nuovi. Coerente col gradino 1.3, dove `gain` era
  costato +38 FF esatti.
- **Expression (+45 LUT)**: 31 sono il sommatore, il resto sono le condizioni di
  controllo in più.
- **Register (+37 FF)**: 31 sono il contatore, gli altri il predicato di uscita
  del loop e i suoi registri di pipeline.
- **Multiplexer (+66 LUT)**: la voce più grossa, e la meno ovvia. Sono due
  multiplexer a 31 bit sul contatore:

  ```text
  |ap_sig_allocacmp_conteggio_load  | 32 LUT |
  |conteggio_fu_78                  | 32 LUT |
  ```

  Servono a scegliere fra "azzera" e "incrementa" all'inizio di ogni
  transazione. In VHDL quell'azzeramento l'avresti quasi certamente messo nel
  ramo di reset del process, che non costa logica. Qui non può stare lì, perché
  `conteggio` è un registro di **dato** e in `hls_config.cfg` abbiamo
  `syn.rtl.reset=control`: il reset globale tocca solo i registri di controllo.
  L'azzeramento deve quindi passare dal datapath, e passa da un mux.

  È una osservazione, non ancora una conclusione: al **gradino 1.7** faremo
  l'esperimento vero, `reset=control` contro `reset=state`, con il diff del
  VHDL sotto gli occhi.

Una curiosità del report, per non restarci male guardandolo: nella tabella
*Interface* la colonna `C Type` delle righe `s_axi_ctrl_*` è passata da
`scalar` a `pointer`. Non è cambiata l'interfaccia — è che quella colonna
descrive il bundle, e il bundle adesso contiene anche un argomento puntatore.

### Il testbench: verificare un'uscita che non sta sullo stream

Il testbench cresce in tre punti, e tutti e tre rispondono alla stessa regola:

> Una uscita non verificata è una uscita che non esiste.

**1) Il golden model del conteggio non usa `n_campioni`.** Sarebbe stato ovvio
scrivere `atteso = n_campioni`, ma è la stessa trappola del gradino 1.3: il
valore atteso verrebbe da dove viene lo stimolo, non da un calcolo indipendente.
Il modello conta invece i beat del vettore di stimoli fino al primo `TLAST`
incluso — cioè guarda il bus come lo guarderebbe un osservatore esterno. Se un
giorno uno stimolo mettesse `TLAST` a metà pacchetto, il DUT si fermerebbe lì e
il modello anche; con `n_campioni` il test darebbe la colpa alla IP.

**2) La variabile viene sporcata prima della chiamata**, con un valore
impossibile (`-999999`: un conteggio di campioni non è mai negativo).

```cpp
int sample_count = SPORCO;
axis_scaler(s_axis, m_axis, gain, &sample_count);
if (sample_count == SPORCO) { /* la IP non ha scritto l'uscita */ }
```

Se lo lasciassimo a zero, un DUT che non scrive mai il registro passerebbe il
test su ogni pacchetto vuoto. È la controparte software del bit `_ap_vld`, che
in hardware esiste per distinguere la stessa identica cosa: *valore vero* da
*valore mai scritto*.

**3) Due transazioni consecutive di lunghezza diversa** (3 poi 5), per fissare
che il conteggio **non si accumula**: `conteggio` è una variabile locale, quindi
riparte da zero a ogni `ap_start`, e il registro riporta la lunghezza
dell'ultimo pacchetto. Al gradino 1.7 introdurremo variabili `static` per le
statistiche cumulative, e questo test è il paletto che dice che quel cambiamento
non deve toccare *questo* registro.

Nove casi, tutti verdi:

```text
--- 1 campioni, gain = 2  (pacchetto minimo: TLAST sul primo beat) ---
  OK: 1 campioni scalati, TLAST e TKEEP intatti, sample_count = 1
...
--- 3 campioni, gain = 2  (prima transazione: sample_count deve dare 3) ---
  OK: 3 campioni scalati, TLAST e TKEEP intatti, sample_count = 3
--- 5 campioni, gain = 2  (seconda transazione: deve dare 5, non 8) ---
  OK: 5 campioni scalati, TLAST e TKEEP intatti, sample_count = 5

 RISULTATO: PASS  (0 errori)
```

Da ricordare però, perché è il limite di questo gradino: **la C simulation non
ha verificato niente di tutto quello che abbiamo appena letto nel VHDL**. Non
esistono 0x18 e 0x1c, non esiste `_ap_vld`, e `sample_count` è un puntatore a
una variabile dello stack. Che quel puntatore diventi un registro a 0x18 con un
bit di validità a 0x1c lo verificheranno la cosimulation (Fase 3) e la
simulazione del block design con gli AXI VIP (Fase 5).
