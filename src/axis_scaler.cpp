// =============================================================================
//  axis_scaler.cpp  --  Implementazione della top function
// =============================================================================
//
//  GRADINO 1.3  --  IL PRIMO REGISTRO DI CONFIGURAZIONE
//
//  Fino al gradino 1.2 l'IP era un pass-through: copiava i campioni senza
//  toccarli. Il banco registri AXI4-Lite c'era, ma conteneva solo i segnali
//  di controllo del blocco.
//
//  Ora l'IP fa qualcosa di configurabile dal software:
//
//        y = x * gain
//
//  E "gain" arriva da un registro che il processore scrive sul bus.
//
//  Due modifiche, che sono due facce della stessa cosa:
//    1. un argomento nuovo nella funzione        (int gain)
//    2. un pragma che lo mappa nel banco registri
//
//  Obiettivo del gradino: sintetizzare e guardare DOVE finisce quel registro
//  nella mappa degli indirizzi, e scoprire chi decide quel "dove".
//
// =============================================================================
#include "axis_scaler.hpp"


void axis_scaler(hls::stream<pkt_t> &s_axis,
                 hls::stream<pkt_t> &m_axis,
                 int                 gain)
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

    // =========================================================================
    //  LA MODIFICA DEL GRADINO 1.3
    // =========================================================================
    //
    //  --- Il primo registro di configurazione --------------------------------
    //
    //  Questa riga dice: "gain non e' un filo, e' un REGISTRO scrivibile dal
    //  bus AXI4-Lite, nello stesso banco dei registri di controllo".
    //
    //  COSA PRODUCE FISICAMENTE:
    //
    //   1) un registro a 32 bit dentro axis_scaler_ctrl_s_axi.vhd, con la sua
    //      logica di scrittura (decodifica dell'indirizzo + gestione WSTRB).
    //      In VHDL sarebbe stato un altro ramo del tuo "case waddr is".
    //
    //   2) un filo interno dal banco registri alla logica di calcolo, per
    //      portare il valore al moltiplicatore.
    //
    //   3) un OFFSET nella mappa degli indirizzi -- e qui sta il punto
    //      didattico del gradino.
    //
    //  --------------------------------------------------------------------
    //  CHI DECIDE L'OFFSET (la cosa da capire in questo gradino)
    //  --------------------------------------------------------------------
    //  Non lo decidiamo noi. Non c'e' nessun posto in cui scriviamo "gain
    //  sta a 0x10". Lo decide HLS, e lo decide in base a DUE cose:
    //
    //    a) gli offset 0x00-0x0C sono riservati al blocco di controllo
    //       standard AMD (CTRL/GIER/IER/ISR). I registri utente partono
    //       quindi da 0x10;
    //
    //    b) l'ORDINE DEGLI ARGOMENTI della funzione C. Il primo argomento
    //       scalare mappato su s_axilite prende il primo offset libero, il
    //       secondo quello dopo, e cosi' via.
    //
    //  Conseguenza pratica, e seria: **se cambi l'ordine degli argomenti,
    //  cambi gli indirizzi dei registri**, e ogni driver software scritto
    //  per la versione precedente scrive nel posto sbagliato. Non e' un
    //  errore che si vede: il codice compila, l'IP parte, e i valori vanno
    //  nel registro accanto.
    //
    //  Per questo la regola di questo progetto e':
    //    - l'ordine degli argomenti si congela e si documenta;
    //    - gli offset NON si scrivono a mano da nessuna parte: si leggono
    //      dall'header xaxis_scaler_hw.h che il tool genera.
    //  --------------------------------------------------------------------
    //
    //  QUANDO VIENE LETTO gain (conseguenza di ap_ctrl_hs)
    //
    //  Il valore viene campionato all'ap_start e resta congelato per tutta
    //  la durata della transazione. Non puoi cambiare il guadagno a meta'
    //  pacchetto: se scrivi il registro mentre l'IP sta elaborando, il nuovo
    //  valore avra' effetto dalla transazione successiva.
    //
    //  Non e' una limitazione arbitraria, discende dal modello di esecuzione:
    //  una chiamata della funzione C = una transazione. In C il parametro di
    //  una funzione non cambia mentre la funzione gira; in hardware vale la
    //  stessa cosa, ed e' il motivo per cui il driver software fa sempre
    //  "scrivi la config -> ap_start -> aspetta ap_done".
    //
#pragma HLS INTERFACE mode=s_axilite port=gain bundle=ctrl


    // =========================================================================
    //  L'ALGORITMO
    // =========================================================================

    bool ultimo = false;

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

        // --- IL CALCOLO (la novita' del gradino) ------------------------------
        //
        //  Una moltiplicazione. Tre cose da sapere su cosa produce.
        //
        //  1) NASCE UN MOLTIPLICATORE HARDWARE.
        //     Nel report di sintesi comparira' una riga nuova. Su un Versal
        //     una moltiplicazione 32x32 puo' finire in uno o piu' DSP58,
        //     oppure in LUT se il tool decide che conviene. Guarderemo cosa
        //     ha scelto: e' il primo gradino in cui il codice C produce
        //     aritmetica vera e non solo fili e handshake.
        //
        //  2) IL RISULTATO VIENE TRONCATO, e va detto esplicitamente.
        //     campione.data e' ap_int<32>, gain e' int a 32 bit: il prodotto
        //     esatto richiederebbe 64 bit. Assegnandolo a un campo ap_int<32>
        //     teniamo solo i 32 bit bassi, cioe' il risultato "modulo 2^32".
        //
        //     Con gain=1000 e x=5.000.000 il valore esatto (5e9) non ci sta in
        //     32 bit con segno, e quello che esce e' un numero negativo. NON
        //     e' un bug del tool: e' aritmetica a precisione finita, la stessa
        //     che avresti in VHDL scrivendo
        //
        //           y <= resize(x * gain, 32);
        //
        //     Il testbench di questo gradino verifica proprio che il
        //     troncamento avvenga come previsto, invece di far finta che il
        //     problema non esista.
        //
        //     La saturazione (cioe' "fermati a 2^31-1 invece di ribaltarti")
        //     arrivera' al gradino 1.8 con il flag SAT_EN. Qui vogliamo prima
        //     vedere il comportamento grezzo.
        //
        //  3) IL PRODOTTO E' SUL PERCORSO DATI.
        //     A differenza del banco registri del gradino 1.2, che stava a
        //     lato, questo moltiplicatore sta in mezzo allo stream. Se non
        //     entrasse nel budget di 4 ns, il tool dovrebbe spezzarlo su piu'
        //     cicli e l'II peggiorerebbe. Confronteremo i report.
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
    }
}


// =============================================================================
//  COSA GUARDARE DOPO AVER SINTETIZZATO QUESTO FILE
// =============================================================================
//
//  1) L'header generato xaxis_scaler_hw.h: deve essere comparso un offset
//     nuovo per "gain", dopo i quattro registri di controllo.
//
//  2) Il generic C_S_AXI_CTRL_ADDR_WIDTH nella entity. Al gradino 1.2 valeva
//     4 (16 byte = 4 registri). Con un registro in piu' i 16 byte non bastano
//     piu': quel numero deve essere cresciuto. E' la conferma fisica che la
//     mappa degli indirizzi si e' allargata.
//
//  3) Il report di sintesi:
//       - una riga nuova nella tabella delle risorse per il moltiplicatore
//         (DSP oppure LUT: guardiamo cosa ha scelto il tool);
//       - II e timing: sono peggiorati, ora che c'e' aritmetica sul percorso
//         dati?
//
//  4) Nella tabella Interface del report, "gain" compare come s_axi con
//     Source Object "gain": il legame fra l'argomento C e il registro.
//
//  Nel GRADINO 1.4 aggiungeremo il primo registro di STATO -- un valore che
//  l'IP scrive e il software legge -- e scopriremo perche' in C deve essere
//  un puntatore, e cosa e' il bit "_ap_vld" che comparira' accanto.
// =============================================================================
