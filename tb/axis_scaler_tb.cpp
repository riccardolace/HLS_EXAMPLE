// =============================================================================
//  axis_scaler_tb.cpp  --  Testbench auto-verificante
// =============================================================================
//
//  GRADINO 1.5
//
//  IL TESTBENCH NON CAMBIA. Il gradino aggiunge #pragma HLS PIPELINE II=1 al
//  loop del DUT, e un pragma di pipeline non tocca ne' la firma della
//  funzione ne' il valore che produce: cambia (o, qui, conferma) il RITMO con
//  cui l'hardware accetta i campioni. E il ritmo e' esattamente la cosa che
//  la C simulation non vede -- non ha clock. Qui hls::stream e' una coda
//  infinita, e un loop con II=1 o II=4 produce gli stessi numeri nello
//  stesso ordine.
//
//  Chi lo vede, invece, e' la cosimulation: e' li' che l'II diventa cicli di
//  clock misurati per transazione. Stesso file, stesso main(), stesso return
//  -- ma riapplicato all'RTL nel simulatore. E' il motivo per cui il file
//  deve restare auto-verificante (vedi sotto).
//
//  Dal gradino 1.4: il testbench verifica anche quello che la IP dice di se'
//  stessa, non solo i dati che produce. Fino al 1.3 controllavamo tre cose --
//  i valori in uscita, TLAST, e il numero di beat -- e tutte e tre
//  viaggiavano sullo stream. Dal 1.4 la IP scrive un REGISTRO DI STATO, e
//  quel registro e' un'uscita a tutti gli effetti: se contiene un numero
//  sbagliato, la IP e' rotta anche se lo stream e' perfetto.
//
//  --------------------------------------------------------------------------
//  UNA USCITA NON VERIFICATA E' UNA USCITA CHE NON ESISTE
//  --------------------------------------------------------------------------
//  Questa e' la regola che vale la pena portarsi dietro. In un testbench VHDL
//  ti verrebbe naturale: se una entity ha una porta di uscita, la guardi. In
//  HLS la tentazione e' di considerare "veri" solo i dati dello stream e di
//  fidarsi del resto, perche' i registri sembrano contorno. Non lo sono.
//
//  Il gradino 1.9 aggiungera' un caso in cui il conteggio e' l'UNICO modo che
//  il software ha di accorgersi che un pacchetto e' stato troncato. Se il
//  registro non lo verifichiamo da adesso, quel bug ce lo troveremo in
//  laboratorio.
//  --------------------------------------------------------------------------
//
//  Questo file NON viene sintetizzato. Viene compilato con clang e girato sul
//  PC, come un normale programma C++. E' l'equivalente del tuo testbench VHDL,
//  con una differenza importante: qui non esiste il tempo. Non ci sono cicli
//  di clock, non c'e' handshake. hls::stream durante la simulazione C si
//  comporta come una coda infinita: scrivi quanto vuoi, leggi quando vuoi.
//
//  Ricorda inoltre (gradino 1.2): la C simulation verifica l'ALGORITMO, non
//  le INTERFACCE. Qui non esistono bus ne' indirizzi: "gain" e' un parametro
//  di funzione e "sample_count" e' un puntatore a una variabile locale del
//  main. Che nell'hardware vero quei due siano due indirizzi su un bus
//  AXI4-Lite, la C simulation non lo sa e non lo verifica.
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
//  IL GOLDEN MODEL -- parte 1: il dato
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
//  LA REGOLA DEL GOLDEN MODEL, dal gradino 1.3: non deve assomigliare al DUT.
//  Scrivere qui la stessa riga del sorgente sintetizzato non verificherebbe
//  niente -- se e' sbagliata, e' sbagliata identica nei due posti e il test
//  passa comunque. Qui la strada e' un'altra: long long in C puro, senza mai
//  passare dai tipi ap_int.
//
static int golden_scala(int x, int gain)
{
    long long prodotto_esatto = (long long)x * (long long)gain;
    unsigned int bassi_32     = (unsigned int)(prodotto_esatto & 0xFFFFFFFFLL);
    return (int)bassi_32;
}


// =============================================================================
//  IL GOLDEN MODEL -- parte 2: il conteggio
// =============================================================================
//
//  Il DUT conta i beat mentre li elabora, e si ferma quando ne vede uno con
//  TLAST alto. Il golden model deve arrivare allo stesso numero per un'altra
//  strada, altrimenti non verifica niente (e' la stessa regola di sopra).
//
//  La strada indipendente e' questa: il testbench guarda gli STIMOLI che ha
//  costruito e conta quanti beat ci sono fino al primo TLAST incluso. Non
//  usa n_campioni, non sa come e' fatto il loop del DUT: legge il vettore di
//  stimoli come lo leggerebbe un osservatore esterno attaccato al bus.
//
//  La differenza rispetto a "tanto so gia' che sono n_campioni" e' sottile ma
//  reale: se un giorno lo stimolo mettesse TLAST a meta' pacchetto -- per
//  errore o di proposito -- il DUT si fermerebbe li', e il valore atteso
//  calcolato cosi' si fermerebbe li' anche lui. Con n_campioni no: il test
//  fallirebbe dando la colpa alla IP invece che allo stimolo.
//
static int golden_conteggio(const std::vector<pkt_t> &stimoli)
{
    int n = 0;
    for (size_t i = 0; i < stimoli.size(); i++) {
        n++;                                   // questo beat e' stato elaborato
        if (stimoli[i].last == 1) break;       // ... ed era l'ultimo
    }
    return n;
}


// -----------------------------------------------------------------------------
//  Il valore con cui "sporchiamo" il registro di stato prima della chiamata.
//
//  Perche' non lasciarlo a zero? Perche' zero e' un valore plausibile, e un
//  test che passa perche' il valore atteso coincide con quello iniziale non
//  ha provato niente. Con un valore impossibile (negativo: un conteggio di
//  campioni non lo e' mai) siamo sicuri che se il DUT non scrive il registro,
//  il test se ne accorge.
//
//  E' la controparte software del bit _ap_vld che l'hardware genera per lo
//  stesso identico motivo: distinguere "valore mai scritto" da "valore letto
//  correttamente".
// -----------------------------------------------------------------------------
static const int SPORCO = -999999;


// -----------------------------------------------------------------------------
//  Un caso di prova: costruisce un pacchetto di n campioni, lo fa passare
//  nell'IP con un certo gain, e verifica contro il golden model sia i dati
//  in uscita sia il registro di stato.
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

    // E qui gli stimoli, per poterli rileggere dal golden model del conteggio.
    std::vector<pkt_t> stimoli;

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
        stimoli.push_back(campione);

        // Il valore atteso lo calcola il GOLDEN MODEL, non il DUT.
        atteso.push_back(golden_scala(valore, gain));
    }

    // -------------------------------------------------------------------------
    //  2) ESECUZIONE: chiamiamo la top function
    // -------------------------------------------------------------------------
    //
    //  Una chiamata = una transazione dell'IP = un pacchetto.
    //
    //  Nota come i due argomenti scalari raccontino due direzioni diverse:
    //
    //        gain           passato per valore    -> lo diamo noi alla IP
    //        &sample_count  passato per indirizzo -> lo compila la IP per noi
    //
    //  In hardware questa chiamata corrisponde a:
    //
    //        scrivi gain nel suo registro AXI4-Lite   (offset 0x10)
    //        scrivi CTRL bit0 = 1                     (ap_start)
    //        aspetta ap_done
    //        leggi il registro sample_count           (offset 0x18)
    //
    //  L'ordine di quelle quattro righe non e' negoziabile, ed e' il motivo
    //  per cui sporchiamo la variabile prima: vogliamo che il test fallisca se
    //  la IP non scrive davvero il registro.
    //
    int sample_count = SPORCO;

    axis_scaler(s_axis, m_axis, gain, &sample_count);

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

    // -------------------------------------------------------------------------
    //  Controllo 4: IL REGISTRO DI STATO (la novita' del gradino 1.4)
    // -------------------------------------------------------------------------
    //
    //  Due errori distinti, e vale la pena distinguerli anche nei messaggi,
    //  perche' indicano guasti diversi:
    //
    //    - il registro e' rimasto SPORCO -> la IP non ha mai scritto l'uscita.
    //      In hardware sarebbe il caso in cui _ap_vld non si alza mai: il bus
    //      risponde, ma con un valore che non significa niente;
    //
    //    - il registro contiene un numero diverso da quello atteso -> la IP
    //      ha contato, ma ha contato male.
    //
    int conteggio_atteso = golden_conteggio(stimoli);

    if (sample_count == SPORCO) {
        printf("  ERRORE: sample_count non e' stato scritto dalla IP "
               "(vale ancora %d)\n", SPORCO);
        errori++;
    } else if (sample_count != conteggio_atteso) {
        printf("  ERRORE: sample_count atteso %d, ottenuto %d\n",
               conteggio_atteso, sample_count);
        errori++;
    }

    if (errori == 0) {
        printf("  OK: %d campioni scalati, TLAST e TKEEP intatti, "
               "sample_count = %d\n", n_campioni, sample_count);
    }

    return errori;
}


// -----------------------------------------------------------------------------
//  main() -- il punto d'ingresso del testbench
// -----------------------------------------------------------------------------
int main()
{
    printf("=====================================================\n");
    printf(" axis_scaler -- testbench gradino 1.5 (pipeline esplicito)\n");
    printf("=====================================================\n");

    int errori = 0;

    // -------------------------------------------------------------------------
    //  I casi di prova.
    //
    //  Tre dimensioni da coprire, ortogonali fra loro:
    //    - la LUNGHEZZA del pacchetto (casi limite del protocollo, e ora anche
    //      il valore che ci aspettiamo nel registro di stato)
    //    - il VALORE del gain         (casi limite dell'aritmetica)
    //    - la SEQUENZA delle chiamate (il registro riporta l'ultimo pacchetto?)
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
    //
    //  Da questo gradino ha un secondo scopo: i dati in uscita sono tutti zero,
    //  ma sample_count deve valere 8. Se il conteggio dipendesse per sbaglio
    //  dai dati invece che dai beat, qui si vedrebbe.
    errori += prova_pacchetto(8,  0, "gain nullo: uscita a zero, conteggio no");

    //  gain negativo: verifica che l'aritmetica con segno funzioni. Con
    //  ap_int (e non ap_uint) il segno c'e', ma va provato.
    errori += prova_pacchetto(8, -3, "gain negativo: aritmetica con segno");

    // -------------------------------------------------------------------------
    //  Il caso aritmetico che ci interessa di piu': IL TRONCAMENTO.
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

    // -------------------------------------------------------------------------
    //  Il caso nuovo del gradino 1.4: DUE PACCHETTI DI LUNGHEZZA DIVERSA,
    //  uno dopo l'altro.
    //
    //  Serve a verificare una proprieta' precisa: il contatore e' una variabile
    //  LOCALE, quindi deve ripartire da zero a ogni chiamata. Il registro deve
    //  riportare la lunghezza dell'ULTIMO pacchetto, non la somma.
    //
    //  Se qualcuno un giorno trasformasse "conteggio" in una variabile static
    //  -- che e' proprio quello che faremo al gradino 1.7, ma per le
    //  statistiche cumulative, non per questo registro -- questi due casi
    //  darebbero 3 e 5+3=8 invece di 3 e 5, e il test lo direbbe subito.
    //
    //  Nota di metodo: ogni chiamata a prova_pacchetto e' gia' una transazione
    //  separata, quindi in un certo senso lo stavamo gia' verificando. Renderlo
    //  esplicito con due lunghezze vicine e diverse lo rende leggibile a chi
    //  legge l'output: 3 poi 5, non c'e' modo di confonderle.
    // -------------------------------------------------------------------------
    printf("\n=== due transazioni consecutive: il conteggio non si accumula ===\n");
    errori += prova_pacchetto(3, 2, "prima transazione: sample_count deve dare 3");
    errori += prova_pacchetto(5, 2, "seconda transazione: deve dare 5, non 8");

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
