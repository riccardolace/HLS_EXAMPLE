// =============================================================================
//  axis_scaler.hpp  --  Tipi e dichiarazione dell'IP
// =============================================================================
//
//  GRADINO 1.3 della costruzione incrementale.
//
//  LA NOVITA': il primo registro di CONFIGURAZIONE.
//
//  Fino al gradino 1.2 questo file non era mai cambiato: la firma della
//  funzione era sempre stata la stessa, perche' il banco registri AXI4-Lite
//  conteneva solo i segnali di controllo del blocco (ap_start, ap_done, ...),
//  che in C non sono argomenti.
//
//  Ora cambia, ed e' il punto centrale del gradino:
//
//        UN REGISTRO DI CONFIGURAZIONE = UN ARGOMENTO DELLA FUNZIONE
//
//  Aggiungiamo "gain": un intero che il software scrive in un registro, e che
//  l'IP usa per moltiplicare ogni campione. Da qui in avanti ogni registro
//  nuovo sara' un argomento nuovo in questa riga.
//
//  Obiettivo del gradino: sintetizzare e scoprire DOVE finisce quel registro
//  nella mappa degli indirizzi, e chi decide quel "dove".
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
//  argomenti diventeranno le porte.
//
//  Gli stream si passano SEMPRE per riferimento (&). Non e' uno stile: e' un
//  requisito. Uno stream e' un canale fisico, non un valore da copiare; se lo
//  passassi per valore chiederesti al tool di duplicare una FIFO hardware, e
//  infatti la sintesi si rifiuterebbe.
//
//  --------------------------------------------------------------------------
//  IL NUOVO ARGOMENTO: gain
//  --------------------------------------------------------------------------
//  Al contrario degli stream, "gain" si passa PER VALORE (senza &). Anche
//  questa non e' una scelta stilistica, dice una cosa precisa al tool:
//
//        per valore   -> e' un INGRESSO, letto una volta. Diventa un registro
//                        di sola scrittura dal lato software.
//        per puntatore o riferimento -> potrebbe essere anche un'USCITA, e il
//                        tool deve generare la logica per scriverlo.
//
//  Il gradino 1.4 usera' proprio un puntatore per il primo registro di STATO,
//  e li' si vedra' la differenza nell'RTL. Per ora: per valore = ingresso.
//
//  Perche' "int" e non ap_int<32>? Perche' un registro AXI4-Lite e' comunque
//  largo 32 bit, e "int" e' il tipo che il driver software usera' dall'altro
//  lato del bus. Tenere lo stesso tipo su entrambi i lati evita conversioni
//  implicite di cui poi non ti ricordi. Al gradino 1.6 diventera' un
//  ap_fixed<16,2>, e allora il discorso cambiera' radicalmente.
//
//  ORDINE DEGLI ARGOMENTI: non e' indifferente.
//  L'ordine in cui compaiono qui determina l'ordine con cui HLS assegna gli
//  OFFSET nella mappa registri. Cambiare l'ordine degli argomenti cambia gli
//  indirizzi, e quindi rompe il driver software scritto per la versione
//  precedente. Per questo l'ordine, una volta scelto, si congela.
//  --------------------------------------------------------------------------
//
void axis_scaler(hls::stream<pkt_t> &s_axis,
                 hls::stream<pkt_t> &m_axis,
                 int                 gain);

#endif // AXIS_SCALER_HPP
