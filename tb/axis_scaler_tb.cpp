// =============================================================================
//  axis_scaler_tb.cpp  --  Testbench auto-verificante
// =============================================================================
//
//  GRADINO 1.3
//
//  LA NOVITA': per la prima volta serve un GOLDEN MODEL.
//
//  Fino al gradino 1.2 l'IP era un pass-through, e "il risultato atteso" era
//  banalmente l'ingresso. Da ora l'IP calcola qualcosa, quindi il testbench
//  deve sapere calcolare la stessa cosa PER CONTO SUO, in modo indipendente,
//  per poter dire se l'hardware ha ragione.
//
//  E' il punto in cui un testbench smette di essere una formalita'.
//
//  --------------------------------------------------------------------------
//  LA REGOLA DEL GOLDEN MODEL: non deve assomigliare al DUT
//  --------------------------------------------------------------------------
//  La tentazione e' scrivere nel testbench la stessa riga che sta nel
//  sorgente sintetizzato:
//
//        atteso = campione.data * gain;      // <-- INUTILE
//
//  Cosi' non si verifica niente. Se quella riga e' sbagliata, e' sbagliata
//  identica nei due posti e il test passa comunque. Il golden model va
//  calcolato per un'altra strada: qui usiamo aritmetica a 64 bit in C puro e
//  poi tronchiamo esplicitamente a 32 bit, senza passare dai tipi ap_int.
//
//  Se i due percorsi -- ap_int nel DUT, long long nel modello -- danno lo
//  stesso risultato, allora il comportamento e' quello che crediamo.
//  --------------------------------------------------------------------------
//
//  Questo file NON viene sintetizzato. Viene compilato con clang e girato sul
//  PC, come un normale programma C++. E' l'equivalente del tuo testbench VHDL,
//  con una differenza importante: qui non esiste il tempo. Non ci sono cicli
//  di clock, non c'e' handshake. hls::stream durante la simulazione C si
//  comporta come una coda infinita: scrivi quanto vuoi, leggi quando vuoi.
//
//  Ricorda inoltre (gradino 1.2): la C simulation verifica l'ALGORITMO, non
//  le INTERFACCE. Qui non esistono bus ne' indirizzi: il registro "gain" e'
//  semplicemente un parametro di funzione.
//
//  --------------------------------------------------------------------------
//  PERCHE' DEVE ESSERE AUTO-VERIFICANTE (e non stampare e basta)
//  --------------------------------------------------------------------------
//  Nella cosimulation, Vitis HLS prende QUESTO STESSO file, lo riesegue e usa
//  gli stessi stimoli per pilotare l'RTL vero, in un simulatore, con clock e
//  handshake reali. Poi confronta i risultati.
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


// =============================================================================
//  IL GOLDEN MODEL
// =============================================================================
//
//  Calcola  y = x * gain  troncato a 32 bit con segno, in C puro.
//
//  Perche' passare da "long long" (64 bit)? Perche' il prodotto di due numeri
//  a 32 bit puo' richiedere fino a 64 bit, e vogliamo prima calcolare il
//  valore ESATTO, e solo dopo troncarlo deliberatamente. Cosi' il troncamento
//  e' un passo visibile e verificabile, non un effetto collaterale nascosto.
//
//  Il cast a "unsigned int" prima di tornare a "int" e' il modo portabile di
//  dire "prendi i 32 bit bassi e reinterpretali con segno": la conversione
//  diretta da long long a int fuori range, in C++, non e' definita in modo
//  garantito, mentre il passaggio per unsigned si', perche' e' definito come
//  modulo 2^32.
//
//  E' esattamente cio' che fa l'hardware quando assegni un prodotto a un
//  registro a 32 bit, e l'equivalente VHDL di:
//
//        y <= resize(x * gain, 32);
//
static int golden_scala(int x, int gain)
{
    long long prodotto_esatto = (long long)x * (long long)gain;
    unsigned int bassi_32     = (unsigned int)(prodotto_esatto & 0xFFFFFFFFLL);
    return (int)bassi_32;
}


// -----------------------------------------------------------------------------
//  Un caso di prova: costruisce un pacchetto di n campioni, lo fa passare
//  nell'IP con un certo gain, e verifica il risultato contro il golden model.
//
//  Ritorna il numero di errori trovati (0 = tutto bene).
// -----------------------------------------------------------------------------
static int prova_pacchetto(int n_campioni, int gain, const char *descrizione)
{
    printf("\n--- %d campioni, gain = %d  (%s) ---\n",
           n_campioni, gain, descrizione);

    // I due canali. Sono gli stessi oggetti che la top function ricevera'.
    hls::stream<pkt_t> s_axis("s_axis");   // il nome serve nei messaggi d'errore
    hls::stream<pkt_t> m_axis("m_axis");

    // Qui teniamo da parte quello che ci aspettiamo di rileggere, calcolato
    // dal golden model.
    std::vector<int> atteso;

    // -------------------------------------------------------------------------
    //  1) STIMOLO: riempiamo lo stream d'ingresso
    // -------------------------------------------------------------------------
    for (int i = 0; i < n_campioni; i++) {

        pkt_t campione;

        // Un pattern riconoscibile a occhio quando guardi le waveform:
        // valori alternati positivi e negativi, crescenti.
        int valore = (i % 2 == 0) ? (100 + i) : -(100 + i);
        campione.data = valore;

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

        // Il valore atteso lo calcola il GOLDEN MODEL, non il DUT.
        atteso.push_back(golden_scala(valore, gain));
    }

    // -------------------------------------------------------------------------
    //  2) ESECUZIONE: chiamiamo la top function
    // -------------------------------------------------------------------------
    //
    //  Una chiamata = una transazione dell'IP = un pacchetto.
    //
    //  Nota che "gain" e' un normale parametro di funzione. In hardware questa
    //  chiamata corrisponde a:
    //
    //        scrivi gain nel suo registro AXI4-Lite
    //        scrivi CTRL bit0 = 1   (ap_start)
    //        aspetta ap_done
    //
    //  Il fatto che il gain sia un argomento e non una variabile modificabile
    //  a meta' pacchetto riflette esattamente l'hardware: il registro viene
    //  campionato all'ap_start e resta congelato per tutta la transazione.
    //
    axis_scaler(s_axis, m_axis, gain);

    // -------------------------------------------------------------------------
    //  3) VERIFICA
    // -------------------------------------------------------------------------
    int errori = 0;

    // Controllo 1: e' uscito il numero giusto di campioni?
    //
    //  Una IP che perde o duplica campioni e' rotta anche se i dati che escono
    //  sono giusti. Va controllato per primo.
    if ((int)m_axis.size() != n_campioni) {
        printf("  ERRORE: attesi %d campioni in uscita, ne sono usciti %d\n",
               n_campioni, (int)m_axis.size());
        errori++;
    }

    // Controllo 2: i dati e i segnali di protocollo.
    for (int i = 0; i < n_campioni && !m_axis.empty(); i++) {

        pkt_t uscita = m_axis.read();

        // 2a) il dato, confrontato col golden model
        int ottenuto = (int)uscita.data;
        if (ottenuto != atteso[i]) {
            printf("  ERRORE campione %d: atteso %d, ottenuto %d\n",
                   i, atteso[i], ottenuto);
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

        // 2c) TKEEP e TSTRB devono essere propagati intatti.
        //
        //  Ora che l'IP modifica il dato, vale la pena controllare anche che
        //  NON abbia modificato i segnali di contorno: un bug che azzera
        //  TKEEP produce uno stream formalmente valido ma che il DMA a valle
        //  interpreta come "nessun byte utile".
        //
        //  C_DATA_WIDTH/8 = 4, cioe' la larghezza di TKEEP e TSTRB: la
        //  ricaviamo dalla costante invece di scrivere 4, cosi' il controllo
        //  resta valido se un giorno cambieremo la larghezza del dato.
        const ap_uint<C_DATA_WIDTH/8> tutti_uni = -1;
        if (uscita.keep != tutti_uni || uscita.strb != tutti_uni) {
            printf("  ERRORE campione %d: TKEEP/TSTRB alterati (keep=%d strb=%d)\n",
                   i, (int)uscita.keep, (int)uscita.strb);
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
        printf("  OK: %d campioni scalati correttamente, TLAST e TKEEP intatti\n",
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
    printf(" axis_scaler -- testbench gradino 1.3 (gain intero)\n");
    printf("=====================================================\n");

    int errori = 0;

    // -------------------------------------------------------------------------
    //  I casi di prova.
    //
    //  Due dimensioni da coprire, ortogonali fra loro:
    //    - la LUNGHEZZA del pacchetto (casi limite del protocollo)
    //    - il VALORE del gain         (casi limite dell'aritmetica)
    // -------------------------------------------------------------------------

    // --- Casi limite del protocollo, a gain fisso ---
    errori += prova_pacchetto(1,  2, "pacchetto minimo: TLAST sul primo beat");
    errori += prova_pacchetto(8,  2, "lunghezza normale");
    errori += prova_pacchetto(17, 2, "lunghezza dispari, non potenza di due");

    // --- Casi limite dell'aritmetica, a lunghezza fissa ---
    //
    //  gain = 1 e' il caso "identita'": verifica che l'IP con gain unitario
    //  si comporti come il pass-through del gradino 1.2. E' un buon controllo
    //  di non-regressione fra un gradino e l'altro.
    errori += prova_pacchetto(8,  1, "identita': deve comportarsi come il 1.2");

    //  gain = 0 azzera tutto. Caso degenere ma legittimo, e utile: verifica
    //  che il registro venga davvero letto e non ignorato.
    errori += prova_pacchetto(8,  0, "gain nullo: uscita tutta a zero");

    //  gain negativo: verifica che l'aritmetica con segno funzioni. Con
    //  ap_int (e non ap_uint) il segno c'e', ma va provato.
    errori += prova_pacchetto(8, -3, "gain negativo: aritmetica con segno");

    // -------------------------------------------------------------------------
    //  Il caso che ci interessa di piu': IL TRONCAMENTO.
    //
    //  Un gain grande manda il prodotto fuori dai 32 bit. Non ci aspettiamo
    //  un valore "giusto" in senso matematico: ci aspettiamo esattamente i 32
    //  bit bassi del prodotto, che e' quello che l'hardware produce.
    //
    //  Questo test e' importante per una ragione di metodo: documenta il
    //  comportamento reale invece di evitarlo. Se un giorno aggiungeremo la
    //  saturazione (gradino 1.8), questo test dovra' essere aggiornato -- e il
    //  fatto che FALLISCA sara' la prova che la saturazione funziona.
    // -------------------------------------------------------------------------
    errori += prova_pacchetto(4, 100000000,
                              "gain enorme: verifica il TRONCAMENTO a 32 bit");

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
