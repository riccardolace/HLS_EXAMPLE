// =============================================================================
//  axis_scaler.cpp  --  Implementazione della top function
// =============================================================================
//
//  GRADINO 1.4  --  IL PRIMO REGISTRO DI STATO
//
//  Fino al gradino 1.3 l'informazione viaggiava in una direzione sola:
//
//        software  --(gain)-->  hardware  --(campioni)-->  stream
//
//  Il software poteva dire alla IP cosa fare, ma la IP non poteva dire niente
//  al software se non attraverso lo stream dei dati. L'unica cosa che il
//  processore sapeva era "ho finito" (il bit ap_done).
//
//  Ora apriamo il canale di ritorno: un registro che l'hardware SCRIVE e il
//  software LEGGE. Contiamo quanti campioni aveva il pacchetto appena
//  elaborato -- cioe' quanti beat sono passati prima del TLAST.
//
//  Tre modifiche, che sono tre facce della stessa cosa:
//    1. un argomento nuovo, e stavolta e' un PUNTATORE   (int *sample_count)
//    2. il solito pragma che lo mappa nel banco registri
//    3. un contatore nel loop, e UNA scrittura dopo il loop
//
//  Obiettivo del gradino: guardare cosa compare nella mappa registri accanto
//  al valore, e capire quando quel valore viene aggiornato.
//
// =============================================================================
#include "axis_scaler.hpp"


void axis_scaler(hls::stream<pkt_t> &s_axis,
                 hls::stream<pkt_t> &m_axis,
                 int                 gain,
                 int                *sample_count)
{
    // =========================================================================
    //  I PRAGMA DI INTERFACCIA
    // =========================================================================
    //
    //  Non cambiano NIENTE di quello che il codice calcola: decidono soltanto
    //  con quale protocollo hardware i dati entrano ed escono dal modulo.
    //
    //  E' la differenza culturale piu' grande rispetto al VHDL. In VHDL
    //  dichiari le porte e poi scrivi a mano la logica che rispetta il
    //  protocollo. In HLS descrivi l'algoritmo, e con i pragma dici al tool
    //  "questo argomento me lo esponi come AXI4-Stream" -- la logica di
    //  handshake la genera lui.
    // -------------------------------------------------------------------------

    // --- Lo stream di ingresso: porta AXI4-Stream SLAVE ----------------------
    //
    //  Genera:  s_axis_TDATA[31:0]  s_axis_TVALID  s_axis_TREADY
    //           s_axis_TKEEP[3:0]   s_axis_TSTRB[3:0]  s_axis_TLAST
    //
    //  INVARIATO dai gradini precedenti.
    //
#pragma HLS INTERFACE mode=axis port=s_axis

    // --- Lo stream di uscita: porta AXI4-Stream MASTER -----------------------
    //
    //  Stessi segnali, direzioni invertite.
    //
    //  INVARIATO dai gradini precedenti.
    //
#pragma HLS INTERFACE mode=axis port=m_axis

    // --- Il controllo del blocco su AXI4-Lite --------------------------------
    //
    //  Genera s_axi_ctrl (i cinque canali AXI4-Lite), il pin interrupt e i
    //  quattro registri CTRL/GIER/IER/ISR agli offset 0x00..0x0C.
    //
    //  Ricorda il punto del gradino 1.2: s_axilite NON sostituisce ap_ctrl_hs.
    //  Il protocollo a livello di blocco resta quello -- "una chiamata della
    //  funzione = una transazione dell'IP" -- e ap_start/ap_done/ap_idle/
    //  ap_ready esistono ancora come segnali interni. Cambia solo il modo di
    //  raggiungerli: una scrittura sul bus invece di un pin.
    //
    //  INVARIATO dal gradino 1.2.
    //
#pragma HLS INTERFACE mode=s_axilite port=return bundle=ctrl

    // --- Il registro di configurazione (gradino 1.3) -------------------------
    //
    //  gain vive a 0x10. Offset scelto da HLS, non da noi: 0x00..0x0C sono
    //  riservati al blocco di controllo standard AMD, e i registri utente
    //  partono da li' nell'ordine in cui compaiono gli argomenti.
    //
    //  Viene campionato all'ap_start e resta congelato per tutta la
    //  transazione: in C un parametro non cambia mentre la funzione gira, e in
    //  hardware vale la stessa cosa.
    //
    //  INVARIATO dal gradino 1.3.
    //
#pragma HLS INTERFACE mode=s_axilite port=gain bundle=ctrl

    // =========================================================================
    //  LA MODIFICA DEL GRADINO 1.4
    // =========================================================================
    //
    //  --- Il primo registro di stato -----------------------------------------
    //
    //  La riga e' identica a quella di gain. Cambia una cosa sola, e non sta
    //  nel pragma: sta nella firma della funzione, dove sample_count e' un
    //  PUNTATORE. E' da li' che il tool capisce che questa e' un'uscita.
    //
    //        stesso pragma + argomento per valore     -> registro scrivibile
    //        stesso pragma + argomento per puntatore  -> registro leggibile
    //
    //  COSA PRODUCE FISICAMENTE, rispetto a gain:
    //
    //   1) un registro a 32 bit dentro axis_scaler_ctrl_s_axi.vhd, ma con la
    //      logica ROVESCIATA: gain veniva caricato dal canale di scrittura
    //      AXI (WDATA) e letto dal datapath; sample_count viene caricato dal
    //      DATAPATH e letto dal canale di lettura AXI (RDATA);
    //
    //   2) un filo interno che va dalla logica di calcolo al banco registri --
    //      direzione opposta a quella di gain;
    //
    //   3) qualcosa in piu', che con gain non c'era: un segnale di VALIDITA'
    //      che accompagna il dato. E' la cosa da cercare nell'RTL generato.
    //
    //  --------------------------------------------------------------------
    //  IL PEZZO DA CERCARE DOPO LA SINTESI: lo slot "_CTRL"
    //  --------------------------------------------------------------------
    //  Al gradino 1.3, nell'header generato, avevamo notato una riga strana:
    //
    //        0x10 : Data signal of gain
    //        0x14 : reserved            <-- inutilizzato
    //
    //  HLS riserva a OGNI registro utente una coppia di slot: uno per il dato
    //  e uno di servizio (nel VHDL si chiamano ADDR_<nome>_DATA_0 e
    //  ADDR_<nome>_CTRL). Per un ingresso come gain il secondo non serve a
    //  niente e resta "reserved".
    //
    //  Per un'uscita invece serve, e la ragione e' un problema che in VHDL
    //  avresti dovuto risolvere a mano:
    //
    //        il software legge quel registro quando vuole, anche PRIMA che
    //        l'hardware ci abbia mai scritto dentro. Cosa trova? E come fa a
    //        sapere se quello che ha letto e' un valore vero o spazzatura
    //        rimasta dall'accensione?
    //
    //  La risposta di AMD e' un bit di accompagnamento, "<nome>_ap_vld": lo
    //  alza l'hardware quando deposita un valore nuovo, e dice al software
    //  "quello che c'e' nel registro dato e' roba fresca". Lo vedremo comparire
    //  proprio nello slot che per gain era "reserved".
    //
    //  E' lo stesso concetto del TVALID di AXI4-Stream, applicato a un
    //  registro invece che a un bus: un dato senza un valido che lo accompagni
    //  e' un dato di cui non sai niente.
    //  --------------------------------------------------------------------
    //
#pragma HLS INTERFACE mode=s_axilite port=sample_count bundle=ctrl


    // =========================================================================
    //  L'ALGORITMO
    // =========================================================================

    bool ultimo = false;

    // -------------------------------------------------------------------------
    //  IL CONTATORE
    //
    //  Una variabile locale, non static. E' importante notarlo adesso perche'
    //  determina il comportamento fra una transazione e l'altra:
    //
    //        locale  -> riparte da 0 a ogni chiamata della funzione, cioe' a
    //                   ogni ap_start. Il registro riporta la lunghezza
    //                   dell'ULTIMO pacchetto, non un totale.
    //
    //  E' quello che vogliamo qui: "quanti campioni aveva questo pacchetto".
    //  Le statistiche che si ACCUMULANO fra i pacchetti (totale campioni,
    //  conteggio pacchetti, min, max) richiedono variabili static, e sono il
    //  gradino 1.7: li' vedremo che HLS genera registri che sopravvivono alla
    //  fine della funzione, e che il reset li tratta in modo diverso.
    //
    //  In hardware questa variabile diventa un registro a 32 bit piu' un
    //  sommatore: l'equivalente esatto di un
    //
    //        signal conteggio : unsigned(31 downto 0);
    //        ...
    //        conteggio <= conteggio + 1;
    //
    //  dentro un process, con l'azzeramento fatto all'inizio della transazione
    //  invece che dal reset globale.
    // -------------------------------------------------------------------------
    int conteggio = 0;

    // -------------------------------------------------------------------------
    //  L'etichetta "copia_pacchetto:" non e' decorativa.
    //
    //  HLS usa le etichette dei loop come nomi nei report di sintesi e nello
    //  Schedule Viewer. Un loop senza etichetta compare come "VITIS_LOOP_92_1"
    //  e quando ne hai cinque non capisci piu' niente. Etichettare i loop e'
    //  una abitudine da IP professionale, come dare un nome ai process VHDL.
    //
    //  (Il nome resta "copia_pacchetto" anche se ora non copia soltanto: lo
    //  cambieremo quando l'elaborazione sara' completa, per non falsare i
    //  confronti dei report fra un gradino e l'altro.)
    // -------------------------------------------------------------------------
    copia_pacchetto:
    while (!ultimo) {

        // --- LETTURA BLOCCANTE ------------------------------------------------
        //
        //  .read() in hardware diventa:
        //
        //        "alza s_axis_TREADY, e aspetta il colpo di clock in cui
        //         anche s_axis_TVALID e' alto; in quel ciclo cattura TDATA"
        //
        //  E' bloccante: se il produttore a monte non ha dati, il modulo si
        //  ferma. Non e' un errore, e' esattamente la backpressure di AXI.
        //
        pkt_t campione = s_axis.read();

        // --- Il campo .last e' TLAST -----------------------------------------
        //
        //  E' un ap_uint<1>, quindi lo confrontiamo esplicitamente con 1
        //  invece di usarlo come booleano: piu' chiaro e senza warning.
        //
        ultimo = (campione.last == 1);

        // --- IL CALCOLO (gradino 1.3) -----------------------------------------
        //
        //  Il prodotto viene TRONCATO a 32 bit: campione.data e' ap_int<32> e
        //  il risultato esatto ne vorrebbe 64. Teniamo i 32 bit bassi, come
        //  farebbe  y <= resize(x * gain, 32);  in VHDL. La saturazione
        //  arrivera' al gradino 1.8.
        //
        //  Costa 4 DSP e allunga la iteration latency a 4 cicli, ma l'II resta
        //  1: entra un campione ogni colpo di clock.
        //
        campione.data = campione.data * gain;

        // --- SCRITTURA BLOCCANTE ---------------------------------------------
        //
        //  Scriviamo l'intera struct, quindi TKEEP, TSTRB e TLAST vengono
        //  propagati automaticamente insieme al dato modificato. Per una IP
        //  che deve stare in un sistema vero questo e' importante: se non
        //  propagassi TLAST, il DMA a valle non chiuderebbe mai la trasferta.
        //
        m_axis.write(campione);

        // --- IL CONTEGGIO (la novita' del gradino) ----------------------------
        //
        //  Una riga, e in hardware e' un sommatore a 32 bit con il suo
        //  registro. Nota che incrementiamo DOPO aver scritto l'uscita, cosi'
        //  il contatore conta i campioni effettivamente EMESSI e non quelli
        //  letti: quando piu' avanti (gradino 1.9) aggiungeremo un caso in cui
        //  un campione viene letto ma non emesso, questa distinzione smettera'
        //  di essere accademica.
        //
        //  Il sommatore ha una dipendenza portata dal loop (conteggio dipende
        //  da se stesso all'iterazione precedente). Vale la pena guardare nel
        //  report se questo ha peggiorato l'II: un'addizione a 32 bit sta
        //  comodamente in un ciclo, quindi non dovrebbe -- ma "non dovrebbe"
        //  non e' una verifica, e il report ce lo dice.
        //
        conteggio++;
    }

    // -------------------------------------------------------------------------
    //  LA SCRITTURA DEL REGISTRO DI STATO
    //
    //  UNA riga, UNA volta, FUORI dal loop. La posizione non e' una questione
    //  di eleganza: e' la specifica temporale del registro.
    //
    //  In C sembra ovvio -- si assegna il risultato quando e' pronto. In
    //  hardware quella singola assegnazione diventa:
    //
    //        il datapath presenta il valore al banco registri e alza per UN
    //        ciclo il segnale di validita'; il banco lo cattura.
    //
    //  E lo fa nell'istante in cui la funzione finisce, cioe' lo stesso in cui
    //  si alza ap_done. Da qui la regola d'uso, che e' anche il motivo per cui
    //  un registro di stato si legge SOLO dopo aver visto ap_done:
    //
    //        scrivi gain -> ap_start -> aspetta ap_done -> leggi sample_count
    //
    //  Leggerlo prima non e' illegale (il bus risponde sempre): restituisce
    //  semplicemente il valore della transazione PRECEDENTE, o zero se non ce
    //  n'e' mai stata una. Ed e' esattamente il caso in cui serve il bit
    //  _ap_vld di cui sopra.
    //
    //  --------------------------------------------------------------------
    //  E SE L'AVESSIMO MESSA DENTRO IL LOOP?
    //  --------------------------------------------------------------------
    //  Scrivere  *sample_count = conteggio;  a ogni iterazione sarebbe C
    //  perfettamente valido, e il risultato finale sarebbe lo stesso. Ma
    //  chiederebbe al tool di aggiornare il registro a ogni beat, cioe' di
    //  tenere un percorso verso il banco registri attivo per tutta la durata
    //  del pacchetto invece che per un ciclo solo.
    //
    //  Non ci limitiamo a immaginarlo: e' una delle cose che guardiamo in
    //  questo gradino, sintetizzando la variante in scratchpad e confrontando
    //  il VHDL. Il risultato sta in docs/00.
    //  --------------------------------------------------------------------
    // -------------------------------------------------------------------------
    *sample_count = conteggio;
}


// =============================================================================
//  COSA GUARDARE DOPO AVER SINTETIZZATO QUESTO FILE
// =============================================================================
//
//  1) L'header generato xaxis_scaler_hw.h. Ci aspettiamo due righe nuove: il
//     dato a 0x18, e -- questa e' la novita' -- lo slot successivo NON piu'
//     "reserved", ma occupato da un bit di validita'. Con quale sigla di
//     accesso e' documentato? (COR? Il vocabolario del gradino 1.2 serve qui.)
//
//  2) axis_scaler_ctrl_s_axi.vhd: la logica di gain era un ramo del processo
//     di SCRITTURA (w_hs, wmask). Per sample_count deve esserci qualcosa nel
//     processo di LETTURA, piu' un ramo che cattura il valore dal datapath.
//
//  3) La entity axis_scaler. Al gradino 1.3 C_S_AXI_CTRL_ADDR_WIDTH era
//     passato da 4 a 5 bit (32 byte). Ora occupiamo 0x18 e 0x1C: siamo ancora
//     dentro i 32 byte, oppure serve un altro bit? Conto prima, verifica dopo.
//
//  4) Il report: un sommatore a 32 bit in piu' e un registro in piu' nel banco.
//     L'II e' rimasto 1?
//
//  Nel GRADINO 1.5 metteremo mano al PIPELINE in modo esplicito e guarderemo
//  cosa cambia davvero in II, latenza e throughput.
// =============================================================================
