// =============================================================================
//  axis_scaler.hpp  --  Tipi e dichiarazione dell'IP
// =============================================================================
//
//  GRADINO 1.4 della costruzione incrementale.
//
//  LA NOVITA': il primo registro di STATO.
//
//  Al gradino 1.3 abbiamo aggiunto "gain": un registro che il SOFTWARE scrive e
//  l'HARDWARE legge. Ora facciamo il viaggio opposto:
//
//        un registro che l'HARDWARE scrive e il SOFTWARE legge
//
//  Contiamo quanti campioni aveva il pacchetto appena elaborato, e lo rendiamo
//  leggibile dal bus. E' la prima informazione che esce dalla IP senza passare
//  dallo stream.
//
//  La riga che cambia e' una sola, e sta nel prototipo qui sotto:
//
//        int  gain          ->  ingresso,  si passa PER VALORE
//        int *sample_count  ->  uscita,    si passa PER PUNTATORE
//
//  Obiettivo del gradino: capire perche' quell'asterisco e' obbligatorio, e
//  scoprire cosa compare nella mappa registri accanto al valore.
//
// =============================================================================
#ifndef AXIS_SCALER_HPP
#define AXIS_SCALER_HPP

// -----------------------------------------------------------------------------
//  Gli header della libreria HLS.
//
//  Non sono header C++ standard: li fornisce Vitis HLS e vivono in
//  /tools/Xilinx/2025.2/Vitis/include. Il compilatore li trova da solo, sia
//  quando compila per la simulazione C sia quando sintetizza.
// -----------------------------------------------------------------------------
#include <ap_int.h>        // ap_int<N> / ap_uint<N> : interi di larghezza arbitraria
#include <hls_stream.h>    // hls::stream<T>         : la coda con handshake
#include <ap_axi_sdata.h>  // ap_axis / hls::axis    : il "pacchetto" AXI4-Stream


// =============================================================================
//  1) LARGHEZZA DEL DATO
// =============================================================================
//
//  Un solo posto in cui e' scritta la larghezza del dato. In VHDL avresti
//  scritto:
//
//        constant C_DATA_WIDTH : natural := 32;
//
//  Qui e' una costante di compilazione C++. Cambiarla qui cambia
//  automaticamente TDATA, TKEEP e TSTRB nell'RTL generato: la larghezza dei
//  segnali non e' scritta a mano da nessuna parte, la deduce il tool.
//
static const int C_DATA_WIDTH = 32;


// =============================================================================
//  2) IL TIPO DEL PACCHETTO AXI4-STREAM
// =============================================================================
//
//  ap_axis<WData, WUser, WId, WDest> rappresenta UNA "beat" di AXI4-Stream,
//  cioe' un colpo di clock in cui TVALID e TREADY sono entrambi alti.
//
//  Precisazione utile (verificata leggendo l'header del tool, vedi docs/01):
//  ap_axis NON e' una struct, e' un ALIAS. Il tipo vero e' hls::axis, e la
//  definizione in /tools/Xilinx/2025.2/Vitis/include/ap_axi_sdata.h e':
//
//        using ap_axis = hls::axis<ap_int<WData>, WUser, WId, WDest, ...>
//
//  Quindi il nostro ap_axis<32,0,0,0> e', per il compilatore,
//  hls::axis<ap_int<32>, 0, 0, 0>. Nota che il primo parametro di hls::axis
//  e' il TIPO del dato, non la sua larghezza in bit.
//
//  I campi diventano i segnali fisici del bus:
//
//        campo C++          segnale RTL         cosa e'
//        ---------          -----------         -------------------------------
//        .data              TDATA[31:0]         il dato (qui: ap_int<32>, con segno)
//        .keep              TKEEP[3:0]          quali byte sono "veri" byte
//        .strb              TSTRB[3:0]          quali byte sono "posizione" valida
//        .last              TLAST               1 sull'ultimo campione del pacchetto
//
//  I quattro parametri del template:
//
//        WData = 32 -> TDATA a 32 bit
//        WUser = 0  -> niente TUSER : la porta NON viene generata
//        WId   = 0  -> niente TID   : la porta NON viene generata
//        WDest = 0  -> niente TDEST : la porta NON viene generata
//
//  Mettere 0 non significa "larghezza zero", significa "questo segnale non
//  esiste". E' un modo elegante di dire al tool quali fili del protocollo
//  vuoi davvero, senza doverli dichiarare a mano come faresti in VHDL.
//
//  --------------------------------------------------------------------------
//  ATTENZIONE, ERRORE CLASSICO DA PRINCIPIANTE
//  --------------------------------------------------------------------------
//  Molti tutorial usano  hls::stream< ap_uint<32> >  invece di ap_axis.
//  Quello genera un bus con SOLO TDATA / TVALID / TREADY: niente TLAST.
//  Senza TLAST non hai il concetto di "pacchetto", e un AXI DMA non sa mai
//  quando la trasferta finisce. Per una IP che deve stare in un sistema vero
//  ap_axis e' la scelta obbligata.
//  --------------------------------------------------------------------------
//
//  Nota su ap_axis vs ap_axiu:
//        ap_axis<...>  -> .data e' ap_int<W>   (con segno)
//        ap_axiu<...>  -> .data e' ap_uint<W>  (senza segno)
//  I fili sono identici, cambia solo come il C++ interpreta i bit. Noi
//  lavoreremo su campioni con segno, quindi ap_axis.
//
typedef ap_axis<C_DATA_WIDTH, 0, 0, 0> pkt_t;


// =============================================================================
//  3) IL PROTOTIPO DELLA FUNZIONE TOP
// =============================================================================
//
//  Questa e' la "top function": e' l'equivalente della tua entity VHDL.
//  Il nome della funzione diventera' il nome del modulo RTL, e i suoi
//  argomenti diventeranno le porte (o, per quelli su s_axilite, i registri).
//
//  Gli stream si passano SEMPRE per riferimento (&). Non e' uno stile: e' un
//  requisito. Uno stream e' un canale fisico, non un valore da copiare; se lo
//  passassi per valore chiederesti al tool di duplicare una FIFO hardware, e
//  infatti la sintesi si rifiuterebbe.
//
//  --------------------------------------------------------------------------
//  IL NUOVO ARGOMENTO: sample_count -- e perche' ha un asterisco
//  --------------------------------------------------------------------------
//
//  Al gradino 1.3 avevamo anticipato la regola, ora la usiamo:
//
//        per VALORE     (int gain)          -> il tool lo puo' solo LEGGERE
//                                              => registro di sola scrittura
//                                                 dal lato software (config)
//
//        per PUNTATORE  (int *sample_count) -> il tool lo puo' anche SCRIVERE
//                                              => registro di sola lettura
//                                                 dal lato software (stato)
//
//  E qui sta il punto didattico del gradino, che merita di essere detto
//  esplicitamente perche' e' controintuitivo per chi arriva dal VHDL.
//
//  IL TOOL NON DECIDE LA DIREZIONE DELLE PORTE GUARDANDO I PRAGMA.
//  La deduce dal C, esattamente come farebbe un compilatore software:
//
//    - un argomento passato per valore e' una COPIA locale. Qualunque cosa tu
//      ci scriva dentro muore quando la funzione ritorna: il chiamante non la
//      vedra' mai. Quindi non ha senso generare la logica per riportarla
//      indietro -> porta di ingresso, punto.
//
//    - un argomento passato per puntatore e' un INDIRIZZO. Scriverci dentro
//      modifica qualcosa che vive fuori dalla funzione, e che il chiamante
//      rileggera'. Quindi il tool deve generare la logica di scrittura
//      -> porta (o registro) di uscita.
//
//  Non e' una convenzione di HLS: e' la semantica del C presa alla lettera e
//  tradotta in fili. Il pragma INTERFACE dice soltanto DOVE finisce quella
//  porta (dentro il bus AXI4-Lite), non in che DIREZIONE va.
//
//  --------------------------------------------------------------------------
//  "MA NON POTEVA ESSERE IL VALORE DI RITORNO?"
//  --------------------------------------------------------------------------
//  E' la domanda giusta, perche' in C il modo naturale di restituire UN valore
//  e' proprio "return". E la risposta non e' "non si puo'": HLS un valore di
//  ritorno lo sa gestire eccome, lo chiama "ap_return". Le ragioni sono altre,
//  e sono tre, in ordine di importanza.
//
//   1) IL RETURN E' UNO SOLO. La nostra IP finira' per avere sette registri di
//      stato (campioni, pacchetti, min, max, saturazioni, sopra-soglia, flag).
//      Con "return" ne restituiresti uno; gli altri sei sarebbero comunque
//      puntatori. Un meccanismo che non scala non e' il meccanismo giusto
//      nemmeno per il primo caso.
//
//   2) IL "port=return" E' GIA' OCCUPATO, e non dal valore di ritorno.
//      Nel pragma
//
//            #pragma HLS INTERFACE mode=s_axilite port=return bundle=ctrl
//
//      la parola "return" non indica il valore restituito: indica LA FUNZIONE
//      NEL SUO INSIEME, cioe' il protocollo a livello di blocco
//      (ap_start/ap_done/ap_idle/ap_ready, il gradino 1.2). E' un omonimo
//      sfortunato nella sintassi di HLS, e vale la pena saperlo perche' nei
//      forum genera confusione a ripetizione.
//
//   3) UN REGISTRO DI STATO NON E' UN "RISULTATO". Un valore di ritorno, in C,
//      e' il risultato dell'elaborazione. Un registro di stato e' un
//      sottoprodotto osservabile: puoi leggerlo o ignorarlo, e la IP funziona
//      lo stesso. Il puntatore descrive meglio la cosa anche a chi legge il
//      codice.
//
//  --------------------------------------------------------------------------
//  ORDINE DEGLI ARGOMENTI: la regola del gradino 1.3 vale ancora
//  --------------------------------------------------------------------------
//  sample_count va IN FONDO, dopo gain. Non e' indifferente: e' l'ordine degli
//  argomenti a decidere gli offset nella mappa registri, quindi
//
//        aggiungere in fondo  -> gli offset gia' esistenti NON si spostano
//        inserire in mezzo    -> tutto quello che segue trasla, e ogni driver
//                                scritto prima scrive nel registro sbagliato
//
//  E' la stessa regola "append-only" con cui si estendono i protocolli e i
//  formati di file: si aggiunge in coda, non si rimescola. Da qui in avanti
//  ogni registro nuovo andra' in fondo alla lista.
//
//  Perche' il nome e' in inglese, in mezzo a commenti italiani? Perche' NON e'
//  un nome interno: HLS lo trasforma nel nome di una macro del driver
//  (XAXIS_SCALER_CTRL_ADDR_SAMPLE_COUNT_DATA). E' API pubblica verso il
//  software, e sta insieme al resto dei nomi generati dal tool.
//  --------------------------------------------------------------------------
//
void axis_scaler(hls::stream<pkt_t> &s_axis,
                 hls::stream<pkt_t> &m_axis,
                 int                 gain,
                 int                *sample_count);

#endif // AXIS_SCALER_HPP
