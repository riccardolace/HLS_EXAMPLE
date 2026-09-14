# 05 — Capacità e limiti di HLS: cosa c'è fuori da questo progetto

`axis_scaler` è volutamente minuscolo: un campione entra, un campione esce. È la
scelta giusta per imparare le **interfacce**, ma lascia aperta una domanda
naturale — *e tutto il resto? parallelismo, filtri, macchine a stati, scelta dei
bit?*

Questo documento raccoglie le risposte a quelle domande, nate durante il gradino
1.4. **Non fa parte della scala didattica**: nessuno di questi esperimenti ha
toccato `src/` o `tb/`. Sono componenti HLS separati, sintetizzati in scratchpad
solo per rispondere con numeri veri invece che a memoria.

I numeri qui sotto vengono tutti da build reali su `xcvc1902-vsva2197-2MP-e-S`
a 4 ns, la stessa parte e lo stesso clock del progetto — quindi sono
confrontabili con quelli dei gradini in `docs/00`.

> **Perché vale la pena leggerlo comunque.** Tre di questi risultati smentiscono
> l'intuizione che verrebbe naturale a chi progetta in VHDL. Uno smentisce quello
> che stavo per scrivere io prima di sintetizzare.

---

## 1. Più campioni per colpo di clock

`II = 1` significa "un'iterazione del loop per ciclo", non "un campione per
ciclo". Se servono N campioni per ciclo, non si accelera il loop — **si allarga
il bus**.

- La porta AXI4-Stream porta N campioni per beat: `TDATA` largo `N*32` bit invece
  di 32. In C è un `ap_axis<32*N,0,0,0>`, e i campioni si estraggono a fette con
  `.range(hi,lo)`.
- Il loop che li elabora tutti va **srotolato** (`UNROLL`), non pipelinato: il
  tool istanzia N moltiplicatori fisici che lavorano nello stesso ciclo.
- Sull'array locale serve `#pragma HLS ARRAY_PARTITION variable=... complete`.
  Senza, l'array finisce in una BRAM con 1-2 porte e non riesci a leggerne N
  elementi nello stesso ciclo: il parallelismo chiesto con `UNROLL` viene
  strozzato dalla memoria.

Il costo scala come ti aspetti — è la stessa legge già vista al gradino 1.3, dove
una sola moltiplicazione 32×32 è costata 4 DSP. Vedi §7 per quanto si può
allargare davvero una porta.

---

## 2. Finestre scorrevoli: i filtri

Un FIR ha bisogno di ricordare gli ultimi N campioni. Il pattern si chiama *tap
delay line* e nel tool c'è un esempio installato — `Vitis/samples/template_window_class/src/fir.cpp`,
che è codice **AI Engine** (`adf::input_buffer`, target i tile AIE di Versal, non
la logica programmabile), ma il nucleo è identico a quello che scriveresti in HLS:

```cpp
ELEMENT_TYPE tapDelayLine[NUM_COEFFS];      // la finestra: persiste fra i campioni

for (int i = NUM_COEFFS-1; i > 0; i--)
    tapDelayLine[i] = tapDelayLine[i - 1];  // scorrimento
tapDelayLine[0] = nuovo_campione;

int32 y = 0;
for (int i = 0; i < NUM_COEFFS; i++)
    y += coeffs[i] * tapDelayLine[i];       // il MAC sulla finestra
```

Gli ingredienti in HLS:

| Serve | Perché |
|---|---|
| un array che sopravvive fra le iterazioni | è la finestra stessa |
| `static`, se deve sopravvivere anche **fra i pacchetti** | lo stream vero non sa dove il DMA ha messo `TLAST`; senza `static` il filtro vedrebbe un transitorio finto a ogni burst |
| `ARRAY_PARTITION ... complete` sulla finestra | il MAC deve leggere tutti i tap nello stesso ciclo |
| un buffer circolare, se N è grande | evita di spostare N registri a ogni campione |

La `static` è esattamente il meccanismo che arriva al **gradino 1.7**. Una
finestra scorrevole è quello stesso strumento usato per un altro scopo.

Nota sulla latenza: anche se ogni uscita "guarda indietro" di N campioni, una
volta riempita la finestra **l'II resta 1** — la finestra è uno shift register
che scorre, non una rilettura di memoria. Latenza e throughput restano
indipendenti, come al gradino 1.1.

C'è anche la via del fornitore: `Vitis/include/hls_fir.h` è una libreria HLS che
avvolge il **FIR Compiler IP** di AMD — l'equivalente di istanziare il core del
vendor invece di scrivere il tap delay line a mano.

---

## 3. Folding: scegliere quante risorse pagare

La domanda: *posso fare un filtro più piccolo che lavora in due passate, per
risparmiare moltiplicatori?* Sì, ma **la leva ovvia è quella sbagliata**.

Banco di prova: FIR a 8 tap, 64 campioni, `int` a 32 bit, quattro varianti che
differiscono solo per il pragma sul loop dei tap.

| Variante | Pragma | DSP | FF | LUT | Stati FSM | Loop esterno | Cicli totali |
|---|---|---|---|---|---|---|---|
| **A** niente | *(nessuno)* | 32 | 2055 | 1391 | 3 | pipelined, **II=1** | 66 |
| **B** unroll pieno | `UNROLL` | 32 | 2055 | 1391 | 3 | pipelined, **II=1** | 66 |
| **C** folding "a mano" | `UNROLL factor=4` | 16 | 1177 | 990 | 6 | **non pipelined** | **1280** |
| **D** folding col budget | `UNROLL` + `ALLOCATION ... limit=4` | 16 | 1132 | 915 | 2 | pipelined, **II=2** | **132** |

### 3a. A e B sono la stessa cosa: l'unroll annidato è automatico

Risorse, timing, II, latenza e numero di stati **identici**. Il VHDL generato
differisce solo nella numerazione dei segnali interni (`grp_fu_239` contro
`grp_fu_210`: l'ordine con cui il tool ha enumerato le 8 moltiplicazioni), non
nella struttura.

> **Un loop annidato con conteggio di iterazioni costante, dentro un loop
> pipelinato, viene srotolato dal tool da solo.**

È il fratello della trappola dell'auto-pipeline già nota (`docs/00` §4): dalla
2020.2 HLS pipelina i loop da solo, e — meno noto — srotola da solo anche i
sotto-loop piccoli, perché un loop dentro una pipeline dev'essere appiattito per
poter essere schedulato.

### 3b. `UNROLL factor=N` non dimezza il tempo: rompe il pipelining

La variante C fa esattamente quello che chiedevi: due passate da 4 tap invece di
8 in un colpo. I DSP si dimezzano (32 → 16, coerente: 4 tap × 4 DSP per
moltiplicazione). Ma il loop esterno **perde completamente il pipelining**:

```text
|  Loop Name |   min   |   max   | Iteration Latency | II achieved | Pipelined |
|- campione  |     1280|     1280|        20         |      -      |    no     |
```

Da ~1 ciclo per campione a ~20. Non 2× più lento: **19×**, per risparmiare 2× in
DSP. Il tool non riesce più a fondere il mini-loop dei tap dentro quello dei
campioni, e lo tratta come una sotto-pipeline separata (`fir_probe_Pipeline_tap`,
visibile nella tabella *Instance* del report) che deve finire prima che
l'iterazione successiva possa cominciare. L'FSM sale da 3 a 6 stati proprio per
sequenziare quelle chiamate.

### 3c. La leva giusta è `ALLOCATION`

```cpp
#pragma HLS UNROLL                                       // struttura: resta tutto parallelo
#pragma HLS ALLOCATION operation instances=mul limit=4   // budget: 4 moltiplicatori fisici
acc += window[i] * coeffs[i];
```

Qui **non si tocca la struttura del loop** — resta quella di A/B, quindi il tool
può ancora appiattirla nella pipeline esterna. Si dichiara solo quante istanze
fisiche dell'operatore sei disposto a pagare. Risultato:

- stessi 16 DSP della variante C;
- il loop esterno **resta pipelinato**, con **II=2** — il minimo teorico
  (8 moltiplicazioni ÷ 4 unità = 2 turni), non un numero a caso;
- 132 cicli invece di 1280: il costo è **proporzionale** al risparmio, non
  catastrofico;
- l'FSM scende a 2 stati, la più semplice delle quattro varianti.

E la condivisione è verificabile nel VHDL: lo **stesso** moltiplicatore fisico
riceve due coppie di operandi diverse in due turni, attraverso un mux pilotato
dallo stato della pipeline —

```vhdl
mul_32s_32s_32_2_1_U1 : ... port map (din0 => grp_fu_153_p0, din1 => grp_fu_153_p1, ...);

grp_fu_153_p0 <= in_r_read_reg_509   when (turno 1)
                 ..._int_window_4    when (turno 2);
grp_fu_153_p1 <= coeffs_0_read_reg_445  when (turno 1)
                 coeffs_5               when (turno 2);
```

Quel mux **è** la logica di condivisione che in VHDL scriveresti a mano con un
contatore e un `case`. Non l'ha scritta nessuno: è il risultato dello *scheduling*
e del *binding*, le due fasi classiche della sintesi ad alto livello.

> **La regola.** La forma del loop decide la **struttura**; `ALLOCATION` decide il
> **budget**. Per il compromesso area/throughput usa il budget e lascia la
> struttura in pace.

---

## 4. Le macchine a stati

FSM esplicita, scritta come la penseresti in VHDL — `enum`, `switch`, stati
nominati:

```cpp
enum stato_t { IDLE, CARICA, SPOSTA, FINE };

void fsm_probe(bool avvio, bool &occupato, bool &pronto, ap_uint<8> &uscita)
{
#pragma HLS INTERFACE mode=ap_ctrl_none port=return
    static stato_t stato = IDLE;
    static ap_uint<3> contatore = 0;
    switch (stato) { case IDLE: ... case CARICA: ... }
}
```

Costo: **0 DSP, 6 FF, 34 LUT**, percorso critico stimato 1,423 ns. Magro quanto
scriverlo a mano. Tre cose da sapere.

### 4a. `ap_ctrl_none`: un modello di esecuzione diverso

Tutto `axis_scaler` usa `ap_ctrl_hs`: *una chiamata = una transazione*. Una FSM
persistente non ha transazioni, gira per sempre dal reset. `ap_ctrl_none` toglie
**tutti** i pin di controllo del blocco:

```text
|ap_clk    |   in|    1|  ap_ctrl_none|     fsm_probe|  return value|
|ap_rst_n  |   in|    1|  ap_ctrl_none|     fsm_probe|  return value|
```

Solo clock e reset. Il corpo della funzione **è** ciò che accade a ogni colpo di
clock: è la traduzione più diretta di `process(clk) ... end process`.

### 4b. Quattro stati software, uno stato hardware

```vhdl
constant ap_ST_fsm_state1 : STD_LOGIC_VECTOR (0 downto 0) := "1";   -- uno solo
```

L'intero `switch` è logica combinatoria eseguita in un ciclo; gli unici registri
veri sono le `static`. Esattamente come in VHDL, dove il segnale `stato` è
l'unico registro e il `case` è combinatorio nello stesso process. **Non è lo
`switch` a costare cicli: sono le operazioni bloccanti** (una `.read()` su uno
stream) a forzare confini di scheduling.

### 4c. Il `static` decide se una variabile è un registro o un filo

`uscita`, passata per riferimento ma **non** `static`, è diventata **due porte
fisiche** — `uscita_i` in ingresso e `uscita_o` in uscita — invece di un registro
interno, perché la leggo e la scrivo ma non l'ho dichiarata persistente.

È la regola del gradino 1.4 vista dall'altro lato: **`static` = stato che vive
dentro il chip; argomento = filo che esce.**

### 4d. Quando HLS è scomodo per una FSM

Per una FSM piccola con tutto combinatorio in un ciclo, HLS è genuinamente
comodo. Diventa scomodo quando:

- le transizioni dipendono da `.read()`/`.write()` bloccanti sparsi nei vari
  `case` — lo scheduler deve dedurre i cicli da un flusso di controllo
  irregolare, e il risultato è meno prevedibile di un `case` scritto a mano;
- la FSM **è** l'oggetto del progetto (un controller di bus con arbitraggio,
  error recovery, molti casi speciali) invece di un dettaglio di un blocco che
  elabora dati.

---

## 5. HLS e VHDL: cosa è comodo dove

In senso stretto HLS produce RTL, quindi qualunque circuito sincrono è
raggiungibile in linea di principio. Ma i confini pratici sono reali:

| | HLS | VHDL |
|---|---|---|
| Domini di clock | **uno solo** per componente | multi-clock e CDC nativi |
| Primitive fisiche (BUFG, IDELAY, ODDR, MMCM, modalità interne dei DSP) | non si istanziano dentro il C | istanziazione diretta |
| Protocollo d'interfaccia | catalogo fisso (`axis`, `s_axilite`, `m_axi`, `ap_fifo`, `ap_hs`, `ap_none`, `ap_vld`…) | qualunque protocollo, bit per bit |
| Dove finisce un registro | indiretto, tramite pragma e scheduler | lo decidi tu |
| FSM con molte transizioni irregolari | esprimibile, via via scomodo | il caso d'uso naturale |
| Aritmetica su flussi regolari di dati | **il caso d'uso naturale** | lavoro manuale |
| Generazione del protocollo AXI e del banco registri | gratis (vedi gradini 1.2–1.4) | 300-400 righe da scrivere e verificare |
| Esplorazione area/throughput | cambia un pragma, risintetizza, confronta | ridisegnare il datapath a mano |
| Virgola fissa con arrotondamento/saturazione | parametri di template (`ap_fixed`) | logica scritta a mano |

Il confine più netto è il **clock singolo**: dentro una funzione HLS non esiste
CDC asincrono vero (vedi `docs/00` §5, dove `clock=` sul bundle AXI4-Lite si
rivela un vincolo di progetto, non un attraversamento di dominio). Per CDC vero o
per primitive fisiche si scende di livello: block design Vivado, o VHDL attorno
al blocco HLS.

In pratica i progetti seri **mescolano**: un core di calcolo generato da HLS,
avvolto da glue VHDL o inserito in un block design per la parte
control-dominated e per le interfacce fisiche. È esattamente la direzione della
Fase 5 di questo progetto.

---

## 6. Quantizzazione: dove si decide quanti bit

Misura, stessa operazione `y = x * gain` del gradino 1.3, cambiando solo la
larghezza degli operandi:

| Operandi | DSP |
|---|---|
| 32 × 32 (l'`axis_scaler` fino al 1.5) | **4** |
| 32 × 16 (l'`axis_scaler` dal 1.6, `gain` in Q2.14) | **2** |
| 16 × 16 | **1** |

Quattro volte meno silicio per la stessa moltiplicazione. **La larghezza dei bit
decisa prima di arrivare a HLS determina quanto hardware pagherai.** La riga
di mezzo è stata misurata al gradino 1.6 (`docs/00` §9) ed era stata prevista
prima di sintetizzare: il DSP58 di Versal è un moltiplicatore 27×24, quindi un
operando a 32 bit costa due tagli e uno a 16 bit uno solo — 2×1 = 2 DSP.

Sono due domande diverse, con due strumenti diversi:

| Domanda | Dominio | Strumento |
|---|---|---|
| *Quanti bit mi servono per non perdere precisione?* | numerico, statistico | MATLAB (Fixed-Point Designer, `fi`) o Python vettorizzato |
| *Dato quel formato, come lo implemento al meglio?* | architetturale | **HLS** |

La prima domanda vuole far girare l'algoritmo su migliaia di segnali
rappresentativi con vari `<W,I>` e politiche di arrotondamento, misurando
SNR/errore massimo/bias. Farlo ricompilando un `.cpp` HLS per ogni combinazione è
lento e scomodo: la C simulation serve a **verificare** un design, non a
scandagliarne centinaia.

La seconda è quella che HLS fa meglio di chiunque: cambi `ap_fixed<W,I,...>`, o
il fattore di unroll, o `ALLOCATION`, risintetizzi in 20-30 secondi e confronti
il report. È già in programma come **Fase 2b** (`docs/04_esperimenti.md`).

Flusso tipico, quindi in due passi:

1. **MATLAB/Python** → trovi `<W,I>` e la politica minimi che rispettano il
   budget d'errore;
2. **HLS**, con quel formato congelato in `ap_fixed<W,I,AP_RND,AP_SAT>` (il
   **gradino 1.6**) → esplori parallelo, folded, quanti DSP, quale II.

Non è rigidamente a senso unico: il vincolo hardware retroagisce sulla scelta
numerica — un formato che sta in un solo DSP invece che in quattro è spesso
motivo sufficiente per scendere di qualche bit, se il budget d'errore lo
permette.

Attenzione a un dettaglio che si presenterà al gradino 1.6: con `ap_fixed` e
arrotondamento, **l'ordine delle somme parziali può cambiare l'ultimo bit**.
Con gli interi no (la somma è associativa modulo 2³²), ma un accumulo ad albero
e uno sequenziale su virgola fissa non sono necessariamente bit-identici. È un
fatto della progettazione di filtri, non una stranezza di HLS.

---

## 7. Parallelismo alto, serializzatori e gearbox

### 7a. Quanto si può allargare una porta AXI4-Stream

HLS non impone un tetto proprio: `ap_axis<W,...>` è un `ap_int<W>`, e TKEEP/TSTRB
vengono dimensionati in proporzione. Verificato a 8 campioni per beat:

```vhdl
in_r_TDATA  : IN  STD_LOGIC_VECTOR (255 downto 0);   -- 8 x 32 bit
in_r_TKEEP  : IN  STD_LOGIC_VECTOR (31 downto 0);    -- 256/8, un bit per byte
```

Il limite pratico è **a valle**: interconnect e DMA che devono instradare quei
bit, e la congestione di routing quando il bus diventa molto largo. Nel mondo
reale AMD usa bus da 512 o 1024 bit per kernel di rete ad alto throughput.
Otto o dieci campioni per beat sono ben dentro la zona comoda.

### 7b. Parallelismo diverso in ingresso e in uscita

Si può, ed è sorprendentemente naturale. Deserializzatore, 8 campioni/beat in
ingresso e 1 in uscita, scritto nel modo più ovvio — **una** lettura, **otto**
scritture:

```cpp
beat_largo: while (!ultimo) {
    pkt_largo_t beat = in.read();                              // 1 lettura
    for (int i = 0; i < NPAR; i++) {
        pkt_stretto_t campione;
        campione.data = beat.data.range(32*(i+1)-1, 32*i);     // affetta
        out.write(campione);                                    // 8 scritture
    }
}
```

Nessun contatore esplicito, nessuna FSM scritta a mano. Il report:

```text
|  Loop Name  | Iteration Latency | II achieved | target | Pipelined |
|- beat_largo |         9         |      8      |    1   |    yes    |
```

**Il loop resta pipelinato, ma l'II sale automaticamente a 8.** Non l'ha chiesto
nessuno: lo scheduler l'ha dedotto dal fatto che una porta `hls::stream` accetta
**una sola transazione per ciclo**, quindi otto `.write()` sullo stesso canale
non possono essere simultanee. Il risultato è un deserializzatore corretto —
consuma un beat largo ogni 8 cicli ed emette un campione stretto per ciclo — cioè
il *gearbox* che in VHDL scriveresti con un contatore e un mux.

Costo: **0 DSP** (è solo instradamento di bit), 237 FF, 182 LUT, percorso critico
0,761 ns — lo stesso valore pulito del gradino 1.1, perché non c'è aritmetica sul
percorso dati.

Il serializzatore (N stretti in ingresso → 1 largo in uscita) è lo speculare:
accumuli N letture in un buffer in N cicli, poi una sola scrittura. Stesso
vincolo fisico, applicato alla porta d'ingresso. *(Non sintetizzato: la simmetria
discende dallo stesso vincolo verificato sopra, ma non è una misura.)*

### 7c. Bus largo unico o N porte separate?

| Situazione | Scelta |
|---|---|
| Gli N campioni sono **sempre disponibili insieme**, stesso flusso logico | una porta larga: un solo TVALID/TREADY per tutti |
| Gli N canali sono **logicamente indipendenti**, con timing sfalsato | N porte `axis` separate (N pragma distinti), oppure… |
| …molti canali logici su un solo bus fisico | `TID`/`TDEST`: i parametri `WId`/`WDest` di `ap_axis<W,WUser,WId,WDest>` |

Un bus largo unico **accoppia** i canali: uno in ritardo blocca il beat intero.
I `0` che stanno in `ap_axis<32,0,0,0>` nel nostro `src/axis_scaler.hpp` — con il
commento *«mettere 0 significa che questo segnale non esiste»* — sono proprio
`WId` e `WDest`, cioè il meccanismo per distinguere più canali su una porta sola.

---

## Riepilogo degli esperimenti

Tutti su `xcvc1902-vsva2197-2MP-e-S`, clock 4 ns, `flow_target=vivado`.

| Esperimento | Cosa dimostra | Numero da ricordare |
|---|---|---|
| FIR 8 tap, nessun pragma vs `UNROLL` | i loop annidati piccoli si srotolano da soli | risultati identici |
| FIR `UNROLL factor=4` | il folding "a mano" rompe il pipelining esterno | 19× più lento per 2× di DSP |
| FIR `ALLOCATION ... limit=4` | il budget di risorse è la leva giusta | II=2, 132 cicli, 16 DSP |
| FSM `enum`+`switch`, `ap_ctrl_none` | le FSM si fanno, e costano poco | 4 stati C → 1 stato RTL |
| `y = x*gain` a 16 bit | la quantizzazione decide il silicio | 1 DSP contro 4 |
| Deserializzatore 256→32 bit | in/out con parallelismo diverso | II=8 dedotto dal tool |

Nessuno di questi ha toccato `src/`, `tb/` o il workspace del progetto.
