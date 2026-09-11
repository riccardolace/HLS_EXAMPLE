// =============================================================================
//  axis_scaler.cpp  --  Implementazione della top function
// =============================================================================
//
//  GRADINO 1.1  --  PASS-THROUGH
//
//  L'IP legge campioni da s_axis e li riscrive identici su m_axis, finche' non
//  incontra il campione marcato TLAST. Poi si ferma.
//
//  Obiettivo del gradino: sintetizzare questo e guardare la entity VHDL
//  generata, per scoprire quali porte compaiono anche senza che noi abbiamo
//  chiesto nulla.
//
// =============================================================================
#include "axis_scaler.hpp"


void axis_scaler(hls::stream<pkt_t> &s_axis,
                 hls::stream<pkt_t> &m_axis)
{
    // =========================================================================
    //  I PRAGMA DI INTERFACCIA
    // =========================================================================
    //
    //  Questi tre pragma sono la parte piu' importante del file. Non cambiano
    //  NIENTE di quello che il codice calcola: decidono soltanto con quale
    //  protocollo hardware i dati entrano ed escono dal modulo.
    //
    //  E' la differenza culturale piu' grande rispetto al VHDL. In VHDL
    //  dichiari le porte e poi scrivi a mano la logica che rispetta il
    //  protocollo. In HLS descrivi l'algoritmo, e con i pragma dici al tool
    //  "questo argomento me lo esponi come AXI4-Stream" -- la logica di
    //  handshake la genera lui.
    //
    //  Sintassi: dal 2020.2 la forma corretta e' con  mode=  esplicito.
    //  Nei tutorial vecchi trovi  #pragma HLS INTERFACE axis port=s_axis
    //  (senza "mode="): funziona ancora ma e' deprecata.
    // -------------------------------------------------------------------------

    // --- Lo stream di ingresso diventa una porta AXI4-Stream SLAVE -----------
    //
    //  Genera:  s_axis_TDATA[31:0]  s_axis_TVALID  s_axis_TREADY
    //           s_axis_TKEEP[3:0]   s_axis_TSTRB[3:0]  s_axis_TLAST
    //
    //  Chi decide se e' slave o master? La direzione d'uso nel codice:
    //  su s_axis facciamo .read(), quindi e' un ingresso -> slave.
    //
#pragma HLS INTERFACE mode=axis port=s_axis

    // --- Lo stream di uscita diventa una porta AXI4-Stream MASTER ------------
    //
    //  Stessi segnali, direzioni invertite. Qui facciamo .write() -> master.
    //
#pragma HLS INTERFACE mode=axis port=m_axis

    // --- Il protocollo "a livello di blocco" --------------------------------
    //
    //  Questo pragma non riguarda i dati, riguarda il CONTROLLO del modulo:
    //  come si fa a dirgli "parti", e come fa lui a dire "ho finito".
    //
    //  ap_ctrl_hs = "handshake": il modulo espone quattro pin di controllo
    //  (li vedremo tra un attimo nella entity VHDL). E' il default: anche se
    //  non scrivessi questa riga, HLS farebbe la stessa cosa.
    //
    //  L'ho scritta esplicitamente perche' nel GRADINO 1.2 cambieremo SOLO
    //  questa riga, e vedremo l'hardware cambiare di conseguenza.
    //
    //  Nota: "port=return" e' una convenzione. La funzione restituisce void,
    //  ma HLS usa il valore di ritorno come segnaposto per parlare del blocco
    //  nel suo insieme.
    //
#pragma HLS INTERFACE mode=ap_ctrl_hs port=return


    // =========================================================================
    //  L'ALGORITMO
    // =========================================================================
    //
    //  Tre righe. Tutta la complessita' AXI la genera il tool.

    bool ultimo = false;

    // -------------------------------------------------------------------------
    //  L'etichetta "copia_pacchetto:" non e' decorativa.
    //
    //  HLS usa le etichette dei loop come nomi nei report di sintesi e nello
    //  Schedule Viewer. Un loop senza etichetta compare come "VITIS_LOOP_92_1"
    //  e quando ne hai cinque non capisci piu' niente. Etichettare i loop e'
    //  una abitudine da IP professionale, come dare un nome ai process VHDL.
    // -------------------------------------------------------------------------
    copia_pacchetto:
    while (!ultimo) {

        // --- LETTURA BLOCCANTE ------------------------------------------------
        //
        //  .read() e' l'istruzione chiave da capire. In hardware diventa:
        //
        //        "alza s_axis_TREADY, e aspetta il colpo di clock in cui
        //         anche s_axis_TVALID e' alto; in quel ciclo cattura TDATA"
        //
        //  E' bloccante: se il produttore a monte non ha dati, il modulo si
        //  ferma. Non e' un errore, e' esattamente la backpressure di AXI.
        //
        //  In VHDL avresti scritto qualcosa come:
        //
        //        s_axis_tready <= '1';
        //        if (s_axis_tvalid = '1' and s_axis_tready = '1') then
        //            dato <= s_axis_tdata;
        //        end if;
        //
        //  Qui e' una riga, ma il ferro prodotto e' lo stesso.
        //
        pkt_t campione = s_axis.read();

        // --- Il campo .last e' TLAST -----------------------------------------
        //
        //  E' un ap_uint<1>, quindi lo confrontiamo esplicitamente con 1
        //  invece di usarlo come booleano: piu' chiaro e senza warning.
        //
        ultimo = (campione.last == 1);

        // --- SCRITTURA BLOCCANTE ---------------------------------------------
        //
        //  Speculare alla read:
        //
        //        "metti il dato su m_axis_TDATA, alza m_axis_TVALID e aspetta
        //         il ciclo in cui il consumatore a valle alza m_axis_TREADY"
        //
        //  Anche questa e' bloccante: se il consumatore e' pieno, ci fermiamo.
        //
        //  Scriviamo l'intera struct, quindi TKEEP, TSTRB e TLAST vengono
        //  propagati automaticamente. Per una IP che deve stare in un sistema
        //  vero questo e' importante: se non propagassi TLAST, il DMA a valle
        //  non chiuderebbe mai la trasferta.
        //
        m_axis.write(campione);
    }
}


// =============================================================================
//  COSA GUARDARE DOPO AVER SINTETIZZATO QUESTO FILE
// =============================================================================
//
//  Apri la entity VHDL generata (il percorso te lo diamo nel README) e conta
//  le porte. Ne troverai piu' di quante ne abbiamo dichiarate:
//
//        ap_clk        <- non l'abbiamo mai nominato
//        ap_rst_n      <- nemmeno
//        ap_start      <- nemmeno
//        ap_done       <- nemmeno
//        ap_idle       <- nemmeno
//        ap_ready      <- nemmeno
//        s_axis_*      <- questi si', dal pragma axis
//        m_axis_*      <- questi si', dal pragma axis
//
//  Le prime sei sono il protocollo ap_ctrl_hs materializzato in fili.
//  Sono la ragione per cui una IP HLS NON e' "free-running" come un tuo
//  modulo VHDL: finche' qualcuno non alza ap_start, questo blocco non legge
//  un solo campione, anche se s_axis_TVALID e' alto da un'ora.
//
//  Nel GRADINO 1.2 faremo sparire quei sei pin dentro un banco registri
//  AXI4-Lite, cambiando una riga sola.
// =============================================================================
