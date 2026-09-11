// =============================================================================
//  axis_scaler_tb.cpp  --  Testbench auto-verificante
// =============================================================================
//
//  GRADINO 1.1
//
//  Questo file NON viene sintetizzato. Viene compilato con g++ e girato sul
//  PC, come un normale programma C++. E' l'equivalente del tuo testbench VHDL,
//  con una differenza importante: qui non esiste il tempo. Non ci sono cicli
//  di clock, non c'e' handshake. hls::stream durante la simulazione C si
//  comporta come una coda infinita: scrivi quanto vuoi, leggi quando vuoi.
//
//  Serve a verificare che l'ALGORITMO sia giusto, prima e indipendentemente da
//  come verra' realizzato in hardware.
//
//  --------------------------------------------------------------------------
//  PERCHE' DEVE ESSERE AUTO-VERIFICANTE (e non stampare e basta)
//  --------------------------------------------------------------------------
//  Piu' avanti, nella cosimulation, Vitis HLS prendera' QUESTO STESSO file,
//  lo rieseguira' e usera' gli stessi stimoli per pilotare l'RTL vero, in un
//  simulatore, con clock e handshake reali. Poi confrontera' i risultati.
//
//  Quel confronto si basa sul valore restituito da main():
//
//        return 0        ->  PASS
//        return != 0     ->  FAIL
//
//  Un testbench che stampa i valori e ritorna sempre 0 fa passare la
//  cosimulation anche quando l'hardware e' sbagliato. E' l'errore piu' costoso
//  che si possa fare in questo flusso.
//  --------------------------------------------------------------------------
//
// =============================================================================
#include "axis_scaler.hpp"

#include <cstdio>
#include <vector>


// -----------------------------------------------------------------------------
//  Un caso di prova: costruisce un pacchetto di n campioni, lo fa passare
//  nell'IP e verifica che esca identico.
//
//  Ritorna il numero di errori trovati (0 = tutto bene).
// -----------------------------------------------------------------------------
static int prova_pacchetto(int n_campioni)
{
    printf("\n--- Pacchetto da %d campioni ---\n", n_campioni);

    // I due canali. Sono gli stessi oggetti che la top function ricevera'.
    hls::stream<pkt_t> s_axis("s_axis");   // il nome serve nei messaggi d'errore
    hls::stream<pkt_t> m_axis("m_axis");

    // Qui teniamo da parte quello che ci aspettiamo di rileggere.
    // In un pass-through e' banale (identico all'ingresso), ma la struttura
    // e' gia' quella giusta: nei prossimi gradini questo diventera' il
    // "golden model", cioe' il modello di riferimento calcolato in C puro.
    std::vector<ap_int<C_DATA_WIDTH> > atteso;

    // -------------------------------------------------------------------------
    //  1) STIMOLO: riempiamo lo stream d'ingresso
    // -------------------------------------------------------------------------
    for (int i = 0; i < n_campioni; i++) {

        pkt_t campione;

        // Un pattern riconoscibile a occhio quando guardi le waveform:
        // valori alternati positivi e negativi, crescenti.
        campione.data = (i % 2 == 0) ? (100 + i) : -(100 + i);

        // TKEEP e TSTRB: tutti i byte validi.
        //
        //  Perche' devo scriverli a mano? Perche' sono segnali del protocollo
        //  e il tool non inventa il loro valore. In una IP che elabora dati
        //  "densi" (ogni beat e' un campione intero) valgono sempre tutti 1.
        //
        //  -1 su un ap_uint<4> vale 0b1111. E' un idioma comodo: mette a 1
        //  tutti i bit qualunque sia la larghezza, come (others => '1') in VHDL.
        campione.keep = -1;
        campione.strb = -1;

        // TLAST alto solo sull'ultimo campione: e' cosi' che si delimita un
        // pacchetto in AXI4-Stream.
        campione.last = (i == n_campioni - 1) ? 1 : 0;

        s_axis.write(campione);
        atteso.push_back(campione.data);
    }

    // -------------------------------------------------------------------------
    //  2) ESECUZIONE: chiamiamo la top function
    // -------------------------------------------------------------------------
    //
    //  Una chiamata = una transazione dell'IP = un pacchetto.
    //
    //  Tienilo a mente, perche' e' il modello di esecuzione che ritroveremo
    //  nell'hardware: in RTL questa chiamata corrispondera' a "alza ap_start,
    //  aspetta ap_done".
    //
    axis_scaler(s_axis, m_axis);

    // -------------------------------------------------------------------------
    //  3) VERIFICA
    // -------------------------------------------------------------------------
    int errori = 0;

    // Controllo 1: e' uscito il numero giusto di campioni?
    //
    //  Un pass-through che perde o duplica campioni e' rotto anche se i dati
    //  che escono sono giusti. Va controllato per primo.
    if ((int)m_axis.size() != n_campioni) {
        printf("  ERRORE: attesi %d campioni in uscita, ne sono usciti %d\n",
               n_campioni, (int)m_axis.size());
        errori++;
    }

    // Controllo 2: i dati e i segnali di protocollo.
    for (int i = 0; i < n_campioni && !m_axis.empty(); i++) {

        pkt_t uscita = m_axis.read();

        // 2a) il dato
        if (uscita.data != atteso[i]) {
            printf("  ERRORE campione %d: atteso %d, ottenuto %d\n",
                   i, (int)atteso[i], (int)uscita.data);
            errori++;
        }

        // 2b) TLAST -- si verifica come e piu' del dato.
        //
        //  Un TLAST mancante o nel posto sbagliato non si vede guardando i
        //  numeri, ma manda in stallo il DMA a valle. E' esattamente il tipo
        //  di bug che costa una giornata in laboratorio, quindi lo si prende
        //  qui, in un secondo di simulazione C.
        ap_uint<1> last_atteso = (i == n_campioni - 1) ? 1 : 0;
        if (uscita.last != last_atteso) {
            printf("  ERRORE campione %d: TLAST atteso %d, ottenuto %d\n",
                   i, (int)last_atteso, (int)uscita.last);
            errori++;
        }
    }

    // Controllo 3: lo stream d'ingresso e' stato consumato tutto?
    //
    //  Se avanzano campioni significa che l'IP si e' fermata prima del tempo.
    if (!s_axis.empty()) {
        printf("  ERRORE: sono rimasti %d campioni non letti in ingresso\n",
               (int)s_axis.size());
        errori++;
    }

    if (errori == 0) {
        printf("  OK: %d campioni transitati correttamente, TLAST al posto giusto\n",
               n_campioni);
    }

    return errori;
}


// -----------------------------------------------------------------------------
//  main() -- il punto d'ingresso del testbench
// -----------------------------------------------------------------------------
int main()
{
    printf("=====================================================\n");
    printf(" axis_scaler -- testbench gradino 1.1 (pass-through)\n");
    printf("=====================================================\n");

    int errori = 0;

    // Piu' lunghezze diverse. Non e' pignoleria: i casi limite di un blocco
    // che elabora pacchetti stanno quasi sempre agli estremi.
    errori += prova_pacchetto(1);    // pacchetto minimo: un solo campione,
                                     // che e' contemporaneamente il primo e
                                     // l'ultimo (TLAST sul primo beat)
    errori += prova_pacchetto(8);    // caso normale
    errori += prova_pacchetto(17);   // lunghezza dispari, non potenza di due

    printf("\n=====================================================\n");
    if (errori == 0) {
        printf(" RISULTATO: PASS  (0 errori)\n");
    } else {
        printf(" RISULTATO: FAIL  (%d errori)\n", errori);
    }
    printf("=====================================================\n");

    // Il valore di ritorno E' il risultato del test. Vedi la nota in testa
    // al file: da questo dipende anche l'esito della cosimulation.
    return errori;
}
