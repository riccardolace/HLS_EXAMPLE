// =============================================================================
//  axis_scaler.cpp  --  Implementazione della top function
// =============================================================================
//
//  GRADINO 1.2  --  IL CONTROLLO PASSA SU AXI4-LITE
//
//  L'algoritmo e' IDENTICO al gradino 1.1: leggi da s_axis, riscrivi su m_axis,
//  fermati dopo TLAST. Non e' cambiata una virgola del calcolo.
//
//  E' cambiata UNA RIGA SOLA, il pragma di controllo del blocco:
//
//        1.1:  #pragma HLS INTERFACE mode=ap_ctrl_hs port=return
//        1.2:  #pragma HLS INTERFACE mode=s_axilite  port=return bundle=ctrl
//
//  Obiettivo del gradino: sintetizzare e confrontare la entity VHDL con quella
//  di prima. I quattro pin ap_start/ap_done/ap_idle/ap_ready spariscono dal
//  bordo del modulo e al loro posto compare un bus AXI4-Lite piu' un pin
//  "interrupt".
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
    //  INVARIATO dal gradino 1.1.
    //
#pragma HLS INTERFACE mode=axis port=s_axis

    // --- Lo stream di uscita diventa una porta AXI4-Stream MASTER ------------
    //
    //  Stessi segnali, direzioni invertite. Qui facciamo .write() -> master.
    //
    //  INVARIATO dal gradino 1.1.
    //
#pragma HLS INTERFACE mode=axis port=m_axis

    // =========================================================================
    //  LA MODIFICA DEL GRADINO 1.2
    // =========================================================================
    //
    //  --- Il protocollo "a livello di blocco", ora raggiungibile da software --
    //
    //  Al gradino 1.1 qui c'era:
    //
    //        #pragma HLS INTERFACE mode=ap_ctrl_hs port=return
    //
    //  che esponeva i quattro segnali di controllo come PIN FISICI del modulo:
    //  ap_start, ap_done, ap_idle, ap_ready. Per far partire l'IP dovevi
    //  pilotare a mano quei fili, in RTL, da un altro modulo.
    //
    //  Ora chiediamo al tool di mettere quegli stessi segnali dentro un BANCO
    //  REGISTRI accessibile da un bus AXI4-Lite. Cosi' l'IP diventa pilotabile
    //  da un processore, cioe' da software, che e' il modo in cui si usano le
    //  IP nei sistemi veri.
    //
    //  -----------------------------------------------------------------------
    //  ATTENZIONE AL PUNTO CHE INGANNA (importante, da fissare bene)
    //  -----------------------------------------------------------------------
    //  s_axilite NON sostituisce ap_ctrl_hs: il protocollo a livello di blocco
    //  rimane esattamente quello.
    //
    //        "una chiamata della funzione = una transazione dell'IP"
    //
    //  vale ancora, identico. E ap_start/ap_done/ap_idle/ap_ready continuano a
    //  esistere: solo che diventano fili INTERNI, generati e letti dal banco
    //  registri, invece di affacciarsi sul bordo del modulo.
    //
    //  Cambia il MODO DI ACCESSO, non il modello di esecuzione. Guardando la
    //  entity dirai "sono spariti quattro pin"; guardando il file .vhd per
    //  intero li ritroverai come segnali interni.
    //  -----------------------------------------------------------------------
    //
    //  COSA PRODUCE FISICAMENTE questo pragma:
    //
    //   1) un'interfaccia AXI4-Lite slave completa, con i cinque canali:
    //
    //        s_axi_ctrl_AWADDR/AWVALID/AWREADY     scrittura: indirizzo
    //        s_axi_ctrl_WDATA/WSTRB/WVALID/WREADY  scrittura: dato
    //        s_axi_ctrl_BRESP/BVALID/BREADY        scrittura: risposta
    //        s_axi_ctrl_ARADDR/ARVALID/ARREADY     lettura:   indirizzo
    //        s_axi_ctrl_RDATA/RRESP/RVALID/RREADY  lettura:   dato
    //
    //      In VHDL questi 15 segnali li avresti dichiarati a mano nella entity,
    //      e poi avresti scritto la macchina a stati dello slave AXI4-Lite:
    //      decodifica dell'indirizzo, gestione di WSTRB per le scritture
    //      parziali, generazione di BRESP/RRESP. Tipicamente 300-400 righe che
    //      si copiano da un template e si sbagliano una volta su tre. Qui e'
    //      una riga di pragma, e il tool genera un modulo dedicato
    //      (axis_scaler_ctrl_s_axi.vhd) che vedremo comparire.
    //
    //   2) i quattro registri del blocco di controllo standard AMD, agli
    //      offset che sono una convenzione fissa di tutte le IP HLS:
    //
    //        0x00  CTRL  ap_start(bit0) ap_done(bit1) ap_idle(bit2)
    //                    ap_ready(bit3) auto_restart(bit7) interrupt(bit9)
    //        0x04  GIER  global interrupt enable
    //        0x08  IER   interrupt enable  (bit0 = su ap_done, bit1 = su ap_ready)
    //        0x0C  ISR   interrupt status  (si pulisce scrivendoci 1)
    //
    //      Non li dichiariamo noi e non dobbiamo saperne gli offset a memoria:
    //      il tool genera l'header axis_scaler_hw.h con le #define. Quello e'
    //      la sorgente di verita', e non va MAI ricopiato a mano.
    //
    //   3) un pin di uscita "interrupt", level-high.
    //
    //      Molti testi (e la bozza del nostro piano) riassumono la legge come
    //      "interrupt = GIER and (ISR and IER)". Il VHDL generato dice una
    //      cosa leggermente diversa, ed e' meglio saperlo:
    //
    //        interrupt <= GIER and (ISR(0) or ISR(1));          -- l'uscita
    //        if IER(0)='1' and ap_done='1' then ISR(0) <= '1';  -- il latch
    //        if IER(1)='1' and ap_ready='1' then ISR(1) <= '1';
    //
    //      Cioe' IER non filtra l'uscita: filtra il LATCH dentro ISR. Due
    //      conseguenze pratiche che in laboratorio costano tempo:
    //
    //        - se abiliti IER DOPO che ap_done si e' alzato, l'evento e'
    //          perso per sempre: ISR non si e' mai armato;
    //        - azzerare IER NON spegne un interrupt gia' pendente. ISR resta
    //          a 1 e il pin resta alto. Per abbassarlo devi pulire ISR
    //          scrivendoci 1 (TOW, toggle on write) -- oppure azzerare GIER,
    //          che invece maschera davvero l'uscita.
    //
    //      Il pin esiste sempre, anche se non lo colleghi: se non vuoi
    //      interrupt lasci GIER a 0 e fai polling su CTRL bit1 (ap_done).
    //
    //  PERCHE' bundle=ctrl
    //
    //      "bundle" e' il nome del gruppo AXI4-Lite, e diventa il nome della
    //      porta: bundle=ctrl -> s_axi_ctrl. Se lo omettessimo il tool
    //      sceglierebbe un nome di default derivato dal contesto, che cambia
    //      da versione a versione: meglio deciderlo noi e congelarlo, perche'
    //      questo nome finira' nel block design di Vivado e nel driver
    //      software. Nei gradini successivi i registri di configurazione e di
    //      stato andranno nello STESSO bundle, cioe' nello stesso banco.
    //
#pragma HLS INTERFACE mode=s_axilite port=return bundle=ctrl


    // =========================================================================
    //  L'ALGORITMO  --  invariato dal gradino 1.1
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
//  1) La entity VHDL, confrontata con quella del gradino 1.1:
//
//        SPARITI dal bordo del modulo        COMPARSI
//        ----------------------------        --------------------------------
//        ap_start                            s_axi_ctrl_* (15 segnali AXI4-Lite)
//        ap_done                             interrupt
//        ap_idle
//        ap_ready
//
//     ap_clk, ap_rst_n e tutti i segnali s_axis_*/m_axis_* sono invariati:
//     abbiamo toccato solo il controllo, non i dati.
//
//  2) Un file VHDL NUOVO nella cartella syn/vhdl/:
//
//        axis_scaler_ctrl_s_axi.vhd
//
//     E' il banco registri: lo slave AXI4-Lite con la decodifica degli
//     indirizzi e i registri CTRL/GIER/IER/ISR. E' il codice che in VHDL
//     avresti scritto a mano.
//
//  3) Nel file axis_scaler.vhd, i quattro segnali "spariti" ritrovati come
//     segnali interni, tra il banco registri e la logica di calcolo. La prova
//     che il modello di esecuzione non e' cambiato.
//
//  4) L'header axis_scaler_hw.h (generato dal packaging, "make ip"): la mappa
//     registri ufficiale, con gli offset e il significato di ogni bit.
//
//  Nel GRADINO 1.3 aggiungeremo il primo registro di CONFIGURAZIONE, e
//  scopriremo che l'ordine degli argomenti della funzione C decide gli offset
//  dei registri.
// =============================================================================
