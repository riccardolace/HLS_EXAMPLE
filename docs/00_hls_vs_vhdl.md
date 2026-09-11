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
