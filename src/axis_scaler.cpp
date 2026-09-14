// =============================================================================
//  axis_scaler.cpp  --  Implementazione della top function
// =============================================================================
//
//  GRADINO 1.7  --  LE STATISTICHE IN VARIABILI static
//
//  Fino al 1.6 la IP non ricordava niente da un pacchetto all'altro: ogni
//  ap_start ripartiva da zero, e sample_count riportava la lunghezza
//  dell'ULTIMO pacchetto (il test "3 poi 5, non 8" lo fissa).
//
//  Ora due variabili "static" accumulano fra le chiamate:
//
//        tot_campioni    quanti campioni sono usciti da quando la IP esiste
//        tot_pacchetti   quante transazioni sono state completate
//
//  In C "static" vuol dire "questa variabile vive per tutta la durata del
//  programma, non dello stack della funzione". In hardware vuol dire una cosa
//  molto precisa, ed e' il punto del gradino: un REGISTRO che nessun ap_start
//  azzera, e che ha un rapporto tutto suo con ap_rst_n. Quale, lo decide una
//  riga di hls_config.cfg (syn.rtl.reset), e questo gradino la prova in tutte
//  e tre le forme: control, state, all.
//
// =============================================================================
#include "axis_scaler.hpp"


void axis_scaler(hls::stream<pkt_t> &s_axis,
                 hls::stream<pkt_t> &m_axis,
                 gain_t              gain,
                 int                *sample_count,
                 unsigned int       *total_samples,
                 unsigned int       *packet_count)
{
    // =========================================================================
    //  I PRAGMA DI INTERFACCIA
    // =========================================================================
    //
    //  Non cambiano NIENTE di quello che il codice calcola: decidono con
    //  quale protocollo i dati entrano ed escono. In VHDL scrivi le porte e
    //  poi la logica di protocollo a mano; qui descrivi l'algoritmo e il
    //  pragma dice "questo argomento esponilo cosi'", l'handshake lo genera
    //  il tool.
    // -------------------------------------------------------------------------

    // --- Gli stream: porte AXI4-Stream SLAVE e MASTER (INVARIATO dal 1.1) ---
    //
    //  Generano TDATA[31:0] TVALID TREADY TKEEP[3:0] TSTRB[3:0] TLAST, con le
    //  direzioni invertite fra le due.
    //
#pragma HLS INTERFACE mode=axis port=s_axis
#pragma HLS INTERFACE mode=axis port=m_axis

    // --- Il controllo del blocco su AXI4-Lite (INVARIATO dal 1.2) -----------
    //
    //  Genera s_axi_ctrl, il pin interrupt e CTRL/GIER/IER/ISR a 0x00..0x0C.
    //  Non sostituisce ap_ctrl_hs: "una chiamata = una transazione" resta, e
    //  ap_start/ap_done diventano bit di un registro invece di pin.
    //
#pragma HLS INTERFACE mode=s_axilite port=return bundle=ctrl

    // --- Il registro di configurazione (INVARIATO dal 1.3, tipo dal 1.6) ----
    //
    //  gain a 0x10, 16 bit utili, campionato all'ap_start. Il registro
    //  trasporta i bit grezzi del Q2.14 (docs/00 par. 9).
    //
#pragma HLS INTERFACE mode=s_axilite port=gain bundle=ctrl

    // --- Il registro di stato per-pacchetto (INVARIATO dal 1.4) -------------
    //
    //  Stesso pragma di gain, ma l'argomento e' un PUNTATORE: registro a 0x18
    //  scritto dal datapath, piu' il bit sample_count_ap_vld a 0x1c
    //  (Read/COR), un latch acceso da un impulso e spento dalla lettura --
    //  lo stesso idioma di ap_done (docs/00 par. 7).
    //
#pragma HLS INTERFACE mode=s_axilite port=sample_count bundle=ctrl

    // --- I DUE REGISTRI DI STATO CUMULATIVI (la novita' del 1.7) ------------
    //
    //  Stessa riga di sample_count, due volte. Il pragma NON sa che dietro
    //  c'e' una static: produce esattamente quello che ha prodotto al 1.4, un
    //  registro a 32 bit + un bit _ap_vld ciascuno, nel banco registri.
    //
    //  Previsione sugli offset: 0x20/0x24 e 0x28/0x2c. E 0x2c e' oltre i 32
    //  byte che 5 bit di indirizzo coprono: ADDR_BITS 5 -> 6, e nella entity
    //  top C_S_AXI_CTRL_ADDR_WIDTH 5 -> 6.
    //  (Misurato: 0x28/0x2c e 0x38/0x3c -- ogni puntatore riserva 16 byte.
    //  ADDR_BITS 6 come previsto. Dettagli in axis_scaler.hpp e docs/00.)
    //
    //  Quello che la static cambia NON e' nel banco registri: e' nel
    //  datapath, dove sta la variabile. Vedi sotto.
    //
#pragma HLS INTERFACE mode=s_axilite port=total_samples bundle=ctrl
#pragma HLS INTERFACE mode=s_axilite port=packet_count  bundle=ctrl


    // =========================================================================
    //  LE VARIABILI static  --  LA MODIFICA DEL GRADINO 1.7
    // =========================================================================
    //
    //  Confronto con la riga  int conteggio = 0;  piu' sotto, che e' locale:
    //
    //        locale   -> un registro che riparte da 0 a OGNI ap_start.
    //                    Nell'RTL del 1.6 quel "riparte da 0" costa due mux
    //                    a 31 bit governati da ap_loop_init (docs/00 par. 7):
    //                    e' il phi-node della prima iterazione.
    //
    //        static   -> un registro che parte da 0 UNA volta sola, quando
    //                    il chip viene configurato, e poi non viene mai piu'
    //                    azzerato dalla funzione. Nessun ap_loop_init lo
    //                    tocca: previsione, sommatore -> registro, senza mux.
    //
    //  In VHDL e' la differenza fra una "variable" di process (che in
    //  pratica nessuno userebbe per questo) e un
    //
    //        signal tot_campioni : unsigned(31 downto 0) := (others => '0');
    //
    //  dichiarato nell'architecture, con l'aggiornamento sotto un enable.
    //  Nota il := (others => '0'): e' il valore INIZIALE, quello che la FPGA
    //  carica nei flip-flop alla configurazione (attributo INIT). Non e' un
    //  reset. La differenza fra i due e' TUTTO l'esperimento di questo
    //  gradino, e sta in una chiave di hls_config.cfg:
    //
    //        syn.rtl.reset=control   (il nostro)  ap_rst_n azzera SOLO la FSM
    //                                e i segnali di protocollo. Le static
    //                                hanno il := iniziale e basta: previsione,
    //                                NESSUN  if (ap_rst_n_inv = '1')  sui
    //                                loro processi.
    //
    //        syn.rtl.reset=state     come control, PIU' le static: un ramo di
    //                                reset sui due registri qui sotto, e
    //                                niente altro.
    //
    //        syn.rtl.reset=all       tutto: anche i registri di pipeline del
    //                                datapath (p_0_reg, prodotto_reg, y_reg,
    //                                conteggio...) prendono il ramo di reset.
    //
    //  E' una scelta di PROGETTO, non di stile. Nel banco registri la copia
    //  int_sample_count E' azzerata da ARESET (axis_scaler_ctrl_s_axi.vhd del
    //  1.6, riga 465). Con reset=control, dopo un impulso di ap_rst_n a meta'
    //  vita, il software leggerebbe 0 a 0x20 (copia resettata) e poi, dopo il
    //  pacchetto seguente, vecchio_totale + n (la static non lo era). Un
    //  registro che torna a zero e poi salta: e' il motivo per cui esistono
    //  reset=state, e in software il CLEAR_STATS del gradino 1.8.
    //
    //  Il = 0 e' obbligatorio per il ragionamento sopra: una static senza
    //  inizializzatore in C vale comunque 0, ma scriverlo rende esplicito
    //  qual e' il valore di INIT del flip-flop.
    // -------------------------------------------------------------------------
    static unsigned int tot_campioni  = 0;
    static unsigned int tot_pacchetti = 0;


    // =========================================================================
    //  L'ALGORITMO  (INVARIATO dal 1.6 dentro il loop)
    // =========================================================================

    bool ultimo = false;

    // -------------------------------------------------------------------------
    //  IL CONTATORE PER-PACCHETTO (INVARIATO dal 1.4)
    //
    //  Locale, NON static, di proposito: riparte da 0 a ogni chiamata e il
    //  registro riporta la lunghezza dell'ULTIMO pacchetto. E' il termine di
    //  paragone delle due static qui sopra: stesso sommatore, ma con i due
    //  mux di azzeramento che una static non ha.
    // -------------------------------------------------------------------------
    int conteggio = 0;

    // -------------------------------------------------------------------------
    //  L'etichetta e' il nome del loop nei report e nello Schedule Viewer.
    //  (Il nome "copia_pacchetto" resta per non falsare i confronti fra
    //  gradini, anche se ormai non copia soltanto.)
    // -------------------------------------------------------------------------
    copia_pacchetto:
    while (!ultimo) {

        // ---------------------------------------------------------------------
        //  PIPELINE (gradino 1.5, docs/00 par. 8): il pragma sta DENTRO il
        //  loop. Produce lo stesso RTL che il tool genera da solo con il
        //  default pipeline_loops=64; la riga blinda "un campione per clock"
        //  contro un cfg con pipeline_loops=0.
        //
        //  COMMENTATA DI PROPOSITO dal 1.6, per scelta di chi studia: il
        //  build e' fatto senza (Target II = NA, Final II = 1, RTL identico).
        //  Per riattivarla basta togliere le due barre.
        // ---------------------------------------------------------------------
//#pragma HLS PIPELINE II=1

        // --- LETTURA BLOCCANTE: TREADY alto, cattura al ciclo con TVALID ---
        pkt_t campione = s_axis.read();

        // --- .last e' TLAST -------------------------------------------------
        ultimo = (campione.last == 1);

        // --- IL CALCOLO (INVARIATO dal 1.6, docs/00 par. 9) -----------------
        //
        //  prodotto esatto a 48 bit (2 DSP), poi AP_RND (incrementatore sul
        //  bit 13) e AP_SAT (confronto bit 47..45 + mux) nell'assegnazione a
        //  out_t; l'ultima riga e' un filo.
        //
        prod_t prodotto = campione.data * gain;
        out_t  y        = prodotto;
        campione.data   = y;

        // --- SCRITTURA BLOCCANTE: l'intera struct, TLAST/TKEEP propagati ---
        m_axis.write(campione);

        // --- IL CONTEGGIO PER-PACCHETTO (INVARIATO dal 1.4) -----------------
        //
        //  Dopo la write, cosi' conta i campioni EMESSI (contera' al 1.9,
        //  quando un campione potra' essere letto e non emesso).
        //
        conteggio++;
    }

    // -------------------------------------------------------------------------
    //  GLI ACCUMULI: una volta per transazione, FUORI dal loop
    //
    //  Due righe, e la posizione e' la loro specifica temporale, come per
    //  sample_count al 1.4: l'assegnazione sta dove la funzione finisce,
    //  quindi lo scheduler la mette nel ciclo di ap_done. Previsione: i due
    //  registri static vengono caricati sotto la STESSA condizione
    //  (ap_loop_exit_ready_pp0_iter2_reg and phi = 0) che oggi alza
    //  sample_count_ap_vld e ap_done_int.
    //
    //  In hardware sono un sommatore a 32 bit (tot_campioni + conteggio, con
    //  conteggio a 31 bit esteso) e un incrementatore a 32 bit. Nessuno dei
    //  due sta nel loop: non toccano l'II ne' la iteration latency.
    //
    //  PERCHE' FUORI E NON DENTRO (tot_campioni++ a ogni beat darebbe lo
    //  stesso numero): dentro il loop sarebbe una dipendenza portata dal loop
    //  su una static in una pipeline a II=1 -- un concetto in piu', quello
    //  di min/max, che verra' dopo -- e il _ap_vld pulserebbe a ogni beat
    //  (esperimento A del 1.4). Un gradino, una cosa.
    //
    //  += e ++ su unsigned: se avvolgono, avvolgono modulo 2^32, definito.
    // -------------------------------------------------------------------------
    tot_campioni  += conteggio;
    tot_pacchetti += 1;

    // -------------------------------------------------------------------------
    //  LE SCRITTURE DEI REGISTRI DI STATO (sample_count INVARIATO dal 1.4)
    //
    //  Tre righe, una per registro, tutte nello stesso ciclo: tre impulsi
    //  _ap_vld contemporanei ad ap_done. sample_count resta per-pacchetto;
    //  gli altri due sono la COPIA della static verso il banco registri. Sono
    //  due registri per statistica: la static nel datapath, che accumula, e
    //  int_total_samples nel banco, che il bus legge. Il banco non "vede" la
    //  static, vede un valore e un impulso, come al 1.4.
    // -------------------------------------------------------------------------
    *sample_count  = conteggio;
    *total_samples = tot_campioni;
    *packet_count  = tot_pacchetti;
}


// =============================================================================
//  COSA GUARDARE DOPO AVER SINTETIZZATO QUESTO FILE
// =============================================================================
//
//  Le previsioni, scritte PRIMA di sintetizzare (il confronto e' in docs/00
//  par. 10). Base: il build 1.6 congelato, 2 DSP / 336 FF / 394 LUT, 2,726 ns,
//  II 1, iteration latency 4, 1 stato FSM, ADDR_BITS 5.
//
//  ESITO IN BREVE: le previsioni sulle static (3) e sul reset (6) sono
//  esatte, ctrl_s_axi 172/264 esatto. Sbagliate: gli offset (16 byte per
//  puntatore: 0x28 e 0x38), e la STRUTTURA -- la static fa estrarre il loop
//  in un modulo a se' (axis_scaler_Pipeline_copia_pacchetto, con
//  ap_start/ap_done/ap_idle/ap_ready come pin, i quattro del gradino 1.1) e
//  il top diventa una FSM a 4 stati: 521 FF / 623 LUT, iteration latency 3,
//  cosim N + 6. Il costo delle static in se' e' 64 FF / 66 LUT.
//
//  1) xaxis_scaler_hw.h: 0x20 total_samples (Read) + 0x24 _ap_vld (Read/COR),
//     0x28 packet_count + 0x2c. 0x10 e 0x18 fermi. BITS_..._DATA = 32.
//
//  2) La entity top CAMBIA: C_S_AXI_CTRL_ADDR_WIDTH da 5 a 6 (ADDR_BITS 6 nel
//     banco). Nessun pin nuovo. Nella entity del banco quattro porte "in":
//     total_samples, total_samples_ap_vld, packet_count, packet_count_ap_vld.
//
//  3) Nel top: due registri a 32 BIT PIENI (unsigned: niente stretta a 31
//     come conteggio), con := iniziale e SENZA ramo if (ap_rst_n_inv = '1')
//     (siamo in reset=control). Caricati sotto la stessa condizione di
//     sample_count_ap_vld. NESSUN mux nuovo nella tabella Multiplexer: i due
//     mux a 31 bit di conteggio restano, e sono di ap_loop_init, non del
//     reset.
//
//  4) Il report:  DSP 2, II 1, iter. latency 4, 1 stato, Estimated 2,726 ns
//     (percorso critico ancora nel moltiplicatore). Instance ctrl_s_axi
//     ~172 FF / ~264 LUT (+38/+64 per registro di stato, misurato al 1.4,
//     x2). Register ~304 FF (+64). Expression ~240 LUT (un + e un ++ a 32).
//     Multiplexer ~70. Totale ~476 FF / ~590 LUT. Stessi 5 file VHDL.
//
//  5) Cosim: N + 4 per transazione, invariata. Se e' N + 5, le somme dopo il
//     loop hanno preso un ciclo in piu': da annotare, non da nascondere.
//
//  6) L'esperimento reset (tre cartelle in scratchpad, stesso sorgente):
//       control  nessun ramo di reset sulle static
//       state    ramo di reset SOLO sulle due static; tutto il resto identico
//                a control, mux di conteggio compresi (=> la spiegazione di
//                docs/00 par. 7 sui mux era sbagliata e va corretta)
//       all      ramo di reset anche sui registri di pipeline del datapath
//     ctrl_s_axi.vhd identico nelle tre; LUT circa uguali (il reset sincrono
//     finisce sul pin SR del flip-flop, non in logica).
//
//  7) Testbench: il tb del 1.6 contro questo DUT NON COMPILA (4 argomenti
//     contro 6). Il mutante senza static (variabili locali) deve fallire
//     dalla SECONDA chiamata in poi, non dalla prima.
// =============================================================================
