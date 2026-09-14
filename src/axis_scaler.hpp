// =============================================================================
//  axis_scaler.hpp  --  Tipi e dichiarazione dell'IP
// =============================================================================
//
//  GRADINO 1.7 della costruzione incrementale.
//
//  LA NOVITA': due statistiche che si ACCUMULANO fra un pacchetto e l'altro,
//  cioe' le prime variabili "static" del progetto. In questo header cambia
//  solo il prototipo: due argomenti puntatore in piu', in fondo alla firma.
//
//        unsigned int *total_samples    campioni emessi da sempre
//        unsigned int *packet_count     pacchetti completati da sempre
//
//  Tutto il resto -- i tipi in virgola fissa, il pacchetto AXI4-Stream -- e'
//  INVARIATO dal gradino 1.6, e i commenti lunghi che lo spiegavano sono
//  compressi qui sotto con il rimando a docs/00.
//
// =============================================================================
#ifndef AXIS_SCALER_HPP
#define AXIS_SCALER_HPP

// -----------------------------------------------------------------------------
//  Gli header della libreria HLS (in /tools/Xilinx/2025.2/Vitis/include).
// -----------------------------------------------------------------------------
#include <ap_int.h>        // ap_int<N> / ap_uint<N> : interi di larghezza arbitraria
#include <ap_fixed.h>      // ap_fixed<W,I,Q,O>      : virgola fissa (gradino 1.6)
#include <hls_stream.h>    // hls::stream<T>         : la coda con handshake
#include <ap_axi_sdata.h>  // ap_axis / hls::axis    : il "pacchetto" AXI4-Stream


// =============================================================================
//  1) LARGHEZZA DEL DATO  (INVARIATO dal gradino 1.1)
// =============================================================================
//
//  L'unico posto in cui e' scritta la larghezza di TDATA: come una
//  "constant C_DATA_WIDTH : natural := 32;" in VHDL. TKEEP e TSTRB ne
//  discendono da soli.
//
static const int C_DATA_WIDTH = 32;


// =============================================================================
//  2) IL TIPO DEL PACCHETTO AXI4-STREAM  (INVARIATO dal gradino 1.1)
// =============================================================================
//
//  ap_axis<W, WUser, WId, WDest> e' UN beat: .data -> TDATA, .keep -> TKEEP,
//  .strb -> TSTRB, .last -> TLAST. I tre zeri dicono "TUSER/TID/TDEST non
//  esistono", non "larghezza zero". E' un alias di hls::axis<ap_int<W>,...>,
//  quindi .data e' con segno (docs/00 par. 4, docs/01).
//
//  Non usare hls::stream<ap_uint<32>>: genera un bus senza TLAST, e senza
//  TLAST un DMA non sa mai quando la trasferta finisce.
//
typedef ap_axis<C_DATA_WIDTH, 0, 0, 0> pkt_t;


// =============================================================================
//  3) I TIPI IN VIRGOLA FISSA  (INVARIATI dal gradino 1.6, docs/00 par. 9)
// =============================================================================
//
//  ap_fixed<W, I, Q, O>  <->  sfixed(I-1 downto -(W-I)) di ieee.fixed_pkg.
//  Q e O sono proprieta' del tipo di DESTINAZIONE e agiscono all'assegnazione;
//  fixed_pkg ha i default opposti (fixed_round, fixed_saturate) rispetto ad
//  ap_fixed (AP_TRN, AP_WRAP).
//
//  gain_t   Q2.14, 16 bit: range [-2, 2-2^-14]. Nessun modo Q/O perche' e' un
//           INGRESSO: il registro trasporta bit grezzi, la quantizzazione e'
//           lavoro del software (q2_14() nel testbench). Attenzione:
//           gain_t g = 2.0; non da' errore e avvolge a -2.0.
//  prod_t   Q34.14, 48 bit: il prodotto ESATTO di ap_int<32> * ap_fixed<16,2>.
//  out_t    32 bit interi con AP_RND (un incrementatore sul bit 13) e AP_SAT
//           (confronto sui bit 47..45 + mux 0x7FFFFFFF/0x80000000).
//           In VHDL: resize(prodotto, 31, 0, fixed_saturate, fixed_round).
//
typedef ap_fixed<16, 2>                  gain_t;
typedef ap_fixed<48, 34>                 prod_t;
typedef ap_fixed<32, 32, AP_RND, AP_SAT> out_t;


// =============================================================================
//  4) IL PROTOTIPO DELLA FUNZIONE TOP
// =============================================================================
//
//  La "top function" e' la entity: il nome diventa il modulo RTL, gli
//  argomenti diventano porte o, con s_axilite, registri. Gli stream si
//  passano SEMPRE per riferimento (sono canali fisici, non valori).
//
//  --------------------------------------------------------------------------
//  LA MODIFICA DEL GRADINO 1.7: due argomenti in piu', in fondo
//  --------------------------------------------------------------------------
//
//        unsigned int *total_samples
//        unsigned int *packet_count
//
//  Due PUNTATORI, come sample_count: quindi due registri di stato, leggibili
//  dal software, con il loro bit _ap_vld (regola del 1.4: la direzione la
//  decide il C, non il pragma). La differenza rispetto a sample_count non sta
//  nella firma, sta nel .cpp: il valore che ci finisce dentro viene da una
//  variabile "static", che non riparte da zero a ogni chiamata.
//
//  PERCHE' unsigned int E NON int. Due ragioni, entrambe hardware:
//
//    1) un totale cumulativo AVVOLGE davvero: 2^32 campioni a 250 MHz sono
//       17 secondi di stream continuo. Con unsigned l'avvolgimento e' definito
//       (modulo 2^32); con int e' comportamento indefinito;
//
//    2) proprio perche' l'overflow con segno e' indefinito, al 1.4 il tool ha
//       STRETTO conteggio a 31 bit (docs/00 par. 7): "non puo' mai diventare
//       negativo, quindi il bit 31 non serve". Su un accumulatore che deve
//       poter avvolgere e' la garanzia sbagliata. Con unsigned quella
//       deduzione non e' possibile: previsione, registri a 32 bit PIENI.
//
//  sample_count resta int: non lo tocchiamo (un gradino, una cosa), e in ogni
//  caso e' per-pacchetto -- il suo limite a 2^31 e' un pacchetto di 2,1
//  miliardi di beat, non un totale.
//
//  --------------------------------------------------------------------------
//  ORDINE = MAPPA REGISTRI  (regola dal 1.3, append-only)
//  --------------------------------------------------------------------------
//
//  Aggiunti in coda, quindi 0x10 (gain) e 0x18 (sample_count) non si muovono.
//  Previsione: total_samples a 0x20 (+ _ap_vld a 0x24), packet_count a 0x28
//  (+ _ap_vld a 0x2c). E 0x2c non sta in 5 bit di indirizzo (0x00..0x1F):
//  ADDR_BITS deve passare a 6, e con lui il generic C_S_AXI_CTRL_ADDR_WIDTH
//  della entity -- la prima modifica alla entity top dal gradino 1.3, per lo
//  stesso motivo di allora. Nessun pin nuovo.
//
//  MISURATO (docs/00 par. 10): total_samples a 0x28/0x2c, packet_count a
//  0x38/0x3c. La previsione sugli offset era sbagliata: l'allocatore riserva
//  16 BYTE per ogni argomento puntatore (data, control, e 8 byte per il caso
//  in cui il puntatore venga anche letto), e 0x20-0x27 era gia' il blocco di
//  sample_count -- al 1.4 non si vedeva perche' era l'ultimo. ADDR_BITS 6 e
//  la entity: come previsto. Gli offset, come sempre, si leggono da _hw.h.
//
//  I nomi sono in inglese perche' diventano macro del driver
//  (XAXIS_SCALER_CTRL_ADDR_TOTAL_SAMPLES_DATA): sono API verso il software.
//
void axis_scaler(hls::stream<pkt_t> &s_axis,
                 hls::stream<pkt_t> &m_axis,
                 gain_t              gain,
                 int                *sample_count,
                 unsigned int       *total_samples,
                 unsigned int       *packet_count);

#endif // AXIS_SCALER_HPP
