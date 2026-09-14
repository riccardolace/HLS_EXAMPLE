// =============================================================================
//  axis_scaler.cpp  --  Implementazione della top function
// =============================================================================
//
//  GRADINO 1.5  --  IL CONTROLLO ESPLICITO DEL PIPELINE
//
//  Fino al gradino 1.4 ogni riga aggiunta faceva comparire qualcosa nell'RTL:
//  porte, registri, un moltiplicatore, un bit di validita'. Questo gradino e'
//  diverso, e lo e' di proposito: aggiungiamo UNA riga
//
//        #pragma HLS PIPELINE II=1
//
//  e l'RTL generato NON CAMBIA. Non e' un esperimento fallito: e' la conferma
//  di una cosa scoperta al gradino 1.1, cioe' che il tool mette in pipeline i
//  loop da solo. Il pragma dichiara per iscritto il ritmo che vogliamo -- un
//  campione per colpo di clock -- invece di lasciarlo a un default.
//
//  Obiettivo del gradino: capire cosa sono davvero "II" e "iteration latency"
//  guardando cosa succede all'hardware quando l'II lo FORZIAMO a 2 e a 4, e
//  quando il pipeline lo spegniamo. Quegli esperimenti stanno in scratchpad,
//  i risultati in docs/00 par. 8. Qui in src/ resta solo la riga permanente.
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

    // --- Il registro di stato (gradino 1.4) ----------------------------------
    //
    //  Stessa riga di gain, ma nella firma sample_count e' un PUNTATORE: e' da
    //  li' -- non dal pragma -- che il tool capisce che e' un'uscita.
    //
    //        stesso pragma + argomento per valore     -> registro scrivibile
    //        stesso pragma + argomento per puntatore  -> registro leggibile
    //
    //  Produce, dentro axis_scaler_ctrl_s_axi.vhd, un registro a 32 bit
    //  caricato dal DATAPATH (a 0x18, Read) e un bit di validita'
    //  sample_count_ap_vld (a 0x1c, Read/COR) nello slot che per gain era
    //  "reserved": un latch acceso da un impulso dell'hardware e spento
    //  dalla lettura del software, lo stesso idioma di ap_done. E' il TVALID
    //  di AXI4-Stream applicato a un registro. Dettagli in docs/00 par. 7.
    //
    //  INVARIATO dal gradino 1.4.
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

        // =====================================================================
        //  LA MODIFICA DEL GRADINO 1.5: il pipeline dichiarato per iscritto
        // =====================================================================
        //
        //  Il pragma sta DENTRO il corpo del loop, non prima dell'etichetta:
        //  e' la sintassi di HLS, e si applica al loop che lo contiene. E' la
        //  prima cosa che sorprende venendo dal VHDL.
        //
        //  COSA PRODUCE FISICAMENTE: NIENTE DI NUOVO. L'RTL e' identico a
        //  quello del gradino 1.4, byte per byte (verificato con il diff del
        //  VHDL, docs/00 par. 8): dalla versione 2020.2 il tool pipelina da
        //  solo i loop, e questo lo aveva gia' fatto. Lo confessa lui stesso
        //  in syn/inferred_directives.ini del build 1.4:
        //
        //        # Inferred from syn.compile.pipeline_loops=64
        //        syn.directive.pipeline=axis_scaler/copia_pacchetto
        //
        //  I DUE NUMERI DEL REPORT, che si confondono facilmente:
        //
        //    Iteration Latency = 4   quanti cicli impiega UN campione ad
        //                            attraversare il corpo del loop: il
        //                            moltiplicatore 32x32 non sta in 4 ns e
        //                            il tool lo ha spezzato in 4 stadi.
        //
        //    II = 1                  ogni quanti cicli PUO' ENTRARE un campione
        //    (Initiation Interval)   NUOVO. Non aspetta che il precedente sia
        //                            uscito: fino a 4 campioni "in volo"
        //                            insieme, uno per stadio. Una catena di
        //                            montaggio, non uno sportello.
        //
        //  In termini di fili: II=1 vuol dire che s_axis_TREADY puo' stare
        //  alto a OGNI colpo di clock. Con II=2 il modulo lo alzerebbe un
        //  ciclo si' e uno no -- backpressure che si impone da solo, meta' del
        //  throughput. In VHDL sarebbe il contatore di fase di un datapath
        //  condiviso,  ready <= '1' when fase = 0 else '0';  con i mux sugli
        //  operandi scritti a mano. Qui li genera lo scheduler.
        //
        //  A cosa serve, allora, un II piu' alto? A CONDIVIDERE hardware: con
        //  II=2 due operazioni dello stesso tipo nella stessa iterazione
        //  possono usare a turno lo stesso operatore fisico (docs/05 par. 3,
        //  il FIR con 8 moltiplicazioni su 4 DSP). Ma qui di moltiplicazione
        //  ce n'e' UNA per iterazione: non c'e' niente da mettere a turno.
        //  Misurato (docs/00 par. 8): con II=2 i DSP restano 4 e il
        //  moltiplicatore e' lo stesso modulo; spariscono solo 56 FF di
        //  registri di pipeline, in cambio di meta' del throughput.
        //
        //  PERCHE' LA RIGA RESTA, se non cambia niente: perche' "un campione
        //  per clock" e' la SPECIFICA di questa IP, e va scritta dove si legge
        //  il codice, non lasciata a un default (pipeline_loops=64) che
        //  chiunque puo' cambiare in hls_config.cfg senza toccare il sorgente.
        //  Verificato: con  syn.compile.pipeline_loops=0  nel cfg, il sorgente
        //  del 1.4 sintetizza un loop NON pipelinato -- 3 cicli per campione,
        //  4 stati di FSM -- senza un solo warning. Con questa riga resta
        //  II=1. Un vincolo dichiarato si vede; un default si eredita.
        //
        //  Nella GUI: pannello HLS DIRECTIVES, cursore sul loop, "+", PIPELINE,
        //  II=1. Destinazione "Source file" scrive questa riga; "Config file"
        //  scrive invece in hls_config.cfg la forma equivalente
        //        syn.directive.pipeline=axis_scaler/copia_pacchetto II=1
        //  che e' quella usata per lo sweep in scratchpad.
        // ---------------------------------------------------------------------
#pragma HLS PIPELINE II=1

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
    //  Scriverla DENTRO il loop sarebbe C valido e darebbe lo stesso numero,
    //  ma _ap_vld pulserebbe a ogni beat e smetterebbe di significare "il
    //  risultato e' pronto". Verificato in scratchpad al gradino 1.4 (docs/00
    //  par. 7, esperimento A): la ragione e' semantica, non di risorse.
    // -------------------------------------------------------------------------
    *sample_count = conteggio;
}


// =============================================================================
//  COSA GUARDARE DOPO AVER SINTETIZZATO QUESTO FILE
// =============================================================================
//
//  Stavolta la lista e' di cose che NON devono cambiare, piu' una.
//
//  1) diff del VHDL contro il build 1.4 congelato: zero righe. Se compare
//     una differenza, il pragma ha fatto qualcosa che non avevamo previsto.
//
//  2) Il report: stessi 4 DSP / 298 FF / 343 LUT, stesso Estimated 2,238 ns,
//     e nella tabella dei loop  II achieved = 1, target = 1, Pipelined = yes.
//     La colonna "target" c'era gia' al 1.4 con lo stesso valore: il default
//     e il pragma chiedono la stessa cosa.
//
//  3) syn/inferred_directives.ini: al 1.4 elencava il pipeline come dedotto
//     ("Inferred from syn.compile.pipeline_loops=64"). Ora che lo dichiariamo
//     noi, quella riga deve sparire. E' l'unica traccia della modifica.
//
//  4) La cosim (C/RTL COSIMULATION > Run): la tabella delle transazioni in
//     sim/report/verilog/result.transaction.rpt deve coincidere al ciclo con
//     quella del 1.4. E' la conferma fatta dove conta, sull'RTL in esecuzione.
//
//  Nel GRADINO 1.6 gain diventa ap_fixed<16,2>: la previsione, da docs/05
//  par. 6, e' che i DSP scendano da 4 a 1. Da scrivere PRIMA di sintetizzare.
// =============================================================================
