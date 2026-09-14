// =============================================================================
//  axis_scaler.hpp  --  Tipi e dichiarazione dell'IP
// =============================================================================
//
//  GRADINO 1.6 della costruzione incrementale.
//
//  LA NOVITA': il guadagno diventa un numero in VIRGOLA FISSA, e il prodotto
//  viene ARROTONDATO e SATURATO invece che troncato.
//
//  Fino al 1.5 "gain" era un int: y = x * 3 si poteva fare, y = x * 0.5 no.
//  Un guadagno che non puo' valere 0.5 non e' un guadagno, e' un contatore.
//  In hardware pero' i numeri reali non esistono: esistono bit, e una
//  convenzione su dove sta la virgola. Quella convenzione e' il tipo ap_fixed,
//  ed e' l'equivalente diretto di sfixed di ieee.fixed_pkg.
//
//  Tre tipi nuovi, qui sotto, che raccontano il viaggio di un campione:
//
//        gain_t   Q2.14, 16 bit       il guadagno, come arriva dal registro
//        prod_t   Q34.14, 48 bit      il prodotto ESATTO: 32 + 16 bit, nessuna perdita
//        out_t    32 bit interi       il campione in uscita: e' QUI che si decide
//                 AP_RND, AP_SAT      cosa fare dei bit che non ci stanno
//
//  Il prototipo cambia in un solo punto: il tipo di gain. La entity no -- e
//  questa e' la previsione da verificare: il registro a 0x10 dovrebbe passare
//  da 32 a 16 bit utili, e i DSP da 4 a 2.
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
#include <ap_fixed.h>      // ap_fixed<W,I,Q,O>      : virgola fissa (NUOVO, gradino 1.6)
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
//  3) I TIPI IN VIRGOLA FISSA  (la novita' del gradino 1.6)
// =============================================================================
//
//  ap_fixed<W, I, Q, O> e' un numero con segno su W bit, di cui I sono la
//  parte intera (segno compreso) e W-I la parte frazionaria. La virgola non
//  esiste nei fili: e' una convenzione su come LEGGERE i bit, esattamente come
//  in VHDL con ieee.fixed_pkg:
//
//        ap_fixed<16, 2>      <->     sfixed(1 downto -14)
//
//        bit:   15    14  |  13 ........ 0
//               segno  1  |  1/2 1/4 ... 1/16384
//               \__ I=2 __/ \_____ W-I = 14 _____/
//
//  Q e O sono la novita' vera, e sono due DECISIONI che in VHDL scriveresti a
//  mano con un if:
//
//        Q  (quantizzazione)  cosa fare dei bit frazionari che NON ci stanno
//                             AP_TRN  tronca (default: floor, verso -inf)
//                             AP_RND  arrotonda al piu' vicino, meta' -> +inf
//
//        O  (overflow)        cosa fare se il valore e' piu' grande del massimo
//                             AP_WRAP avvolge (default: come un contatore)
//                             AP_SAT  satura al massimo / al minimo
//
//  REGOLA CHE VALE LA PENA FISSARE: Q e O NON sono proprieta' di un'operazione,
//  sono proprieta' del TIPO DI DESTINAZIONE. Agiscono nel momento in cui un
//  valore viene ASSEGNATO a una variabile di quel tipo, e solo se non ci sta.
//  Il prodotto in se' e' sempre esatto. Per questo qui ci sono tre tipi e non
//  uno: raccontano DOVE, lungo il percorso del campione, i bit si perdono.
// -----------------------------------------------------------------------------

//  --- Il guadagno: Q2.14 ------------------------------------------------------
//
//  16 bit, 2 interi: range [-2, +2), risoluzione 2^-14 = 0.000061.
//  Il massimo NON e' 2.0 ma 2 - 2^-14 = 1.99993896. E' il formato scelto
//  nel piano ("GAIN, Q2.14 signed").
//
//  Nessun modo Q/O qui, e non e' una dimenticanza: gain e' un INGRESSO. In
//  hardware nessun valore viene mai convertito "dentro" gain_t: arrivano 16
//  bit dal registro e vengono letti cosi' come sono. La quantizzazione di un
//  numero reale in Q2.14 e' un lavoro del SOFTWARE -- del driver, e qui del
//  testbench -- che moltiplica per 16384 e arrotonda PRIMA di scrivere il
//  registro. Il registro trasporta bit grezzi.
//
//  ATTENZIONE, verificato con il compilatore: gain_t g = 2.0; NON da' un
//  errore. Con il default AP_WRAP, 2.0 avvolge a -2.0 (bit 0x8000) in
//  silenzio. Un driver che scrive "2.0" nel registro inverte il segno del
//  segnale. Il range check lo deve fare chi quantizza.
//
typedef ap_fixed<16, 2> gain_t;

//  --- Il prodotto esatto: Q34.14 ---------------------------------------------
//
//  La CRESCITA DEI BIT. Un intero a 32 bit per un Q2.14 a 16 bit da' un
//  Q34.14 a 48 bit, e in quel formato il prodotto e' ESATTO, per qualunque
//  coppia di operandi. Le regole sono quelle di fixed_pkg:
//
//        W  = 32 + 16 = 48               sfixed(31 downto 0) * sfixed(1 downto -14)
//        I  = 32 +  2 = 34                     = sfixed(33 downto -14)
//
//  Il tipo lo dichiariamo esplicito per leggibilita', ma il compilatore lo
//  calcola da solo: e' il tipo naturale di "ap_int<32> * ap_fixed<16,2>"
//  (verificato: W=48, I=34). Nessun modo Q/O: qui non si perde niente.
//
typedef ap_fixed<48, 34> prod_t;

//  --- Il campione in uscita: 32 bit interi, con arrotondamento e saturazione --
//
//  ap_fixed<32, 32> e' un intero a 32 bit "visto come virgola fissa senza
//  parte frazionaria": lo stesso contenuto di ap_int<32>, ma con i modi Q e O.
//  E' il tipo a cui viene ASSEGNATO il prodotto, quindi e' qui che i 48 bit
//  diventano 32, e qui che valgono i modi:
//
//        i 14 bit frazionari spariscono   ->  AP_RND: arrotonda, non tronca
//        i 2 bit interi in piu' spariscono ->  AP_SAT: satura, non avvolge
//
//  In VHDL sarebbe   resize(prodotto, 31, 0, fixed_saturate, fixed_round)
//  di fixed_pkg: gli stessi due parametri, con gli stessi nomi.
//
//  COSA PRODUCONO FISICAMENTE, previsione da verificare nell'RTL:
//    AP_RND  ->  un sommatore che aggiunge 2^13 (mezzo LSB) prima di
//                scartare i 14 bit bassi. Non serve guardare i bit 0..12:
//                solo il bit 13 decide il riporto;
//    AP_SAT  ->  un confronto sui bit alti del prodotto (sono tutti uguali
//                al segno? allora ci sta) e un mux a 32 bit che sceglie fra il
//                valore, 0x7FFFFFFF e 0x80000000.
//
typedef ap_fixed<32, 32, AP_RND, AP_SAT> out_t;


// =============================================================================
//  4) IL PROTOTIPO DELLA FUNZIONE TOP
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
//  LA MODIFICA DEL GRADINO 1.6: il tipo di gain
//  --------------------------------------------------------------------------
//
//        int     gain    ->    gain_t  gain
//
//  Stesso argomento, stessa posizione, stesso pragma nel .cpp. Cambia solo il
//  tipo, e con il tipo cambia la LARGHEZZA del registro: 16 bit invece di 32.
//  Il banco registri AXI4-Lite resta a parole da 32 bit -- il bus e' quello --
//  ma nell'header generato ci aspettiamo che 0x10 documenti solo i bit 15..0.
//  Il tool prende la larghezza dal tipo C, come al gradino 1.1 la prendeva da
//  C_DATA_WIDTH per TDATA. Niente e' scritto a mano.
//
//  --------------------------------------------------------------------------
//  Le due regole dei gradini 1.3 e 1.4, che valgono ancora
//  --------------------------------------------------------------------------
//
//  DIREZIONE: la decide il C, non il pragma.
//
//        per VALORE     (gain_t gain)       -> il tool lo puo' solo LEGGERE
//                                              => registro scritto dal software
//        per PUNTATORE  (int *sample_count) -> il tool lo puo' anche SCRIVERE
//                                              => registro letto dal software
//
//  Un argomento per valore e' una copia locale: scriverci dentro non arriva
//  al chiamante, quindi non ha senso generare la logica per riportarlo
//  indietro. Un puntatore e' un indirizzo: scriverci modifica qualcosa fuori.
//  Il pragma INTERFACE dice DOVE finisce la porta, non in che VERSO va.
//  (Perche' non un valore di ritorno: docs/00 par. 7, esperimento B -- il
//  return e' uno solo, non ha bit di valido, e sposta gli offset degli altri.)
//
//  ORDINE: e' l'ordine degli argomenti a decidere gli offset nella mappa
//  registri. Si aggiunge in fondo, non si rimescola ("append-only"): inserire
//  in mezzo trasla tutto quello che segue e ogni driver scritto prima scrive
//  nel registro sbagliato, senza errori visibili.
//
//  I nomi degli argomenti sono in inglese perche' NON sono nomi interni: HLS
//  li trasforma in macro del driver (XAXIS_SCALER_CTRL_ADDR_GAIN_DATA). Sono
//  API pubblica verso il software.
//  --------------------------------------------------------------------------
//
void axis_scaler(hls::stream<pkt_t> &s_axis,
                 hls::stream<pkt_t> &m_axis,
                 gain_t              gain,
                 int                *sample_count);

#endif // AXIS_SCALER_HPP
