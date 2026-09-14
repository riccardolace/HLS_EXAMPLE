// =============================================================================
//  axis_scaler_tb.cpp  --  Testbench auto-verificante
// =============================================================================
//
//  GRADINO 1.6
//
//  LA NOVITA': il DUT lavora in virgola fissa, arrotonda e satura. Il
//  testbench deve fare tre cose nuove, e nessuna delle tre usa ap_fixed:
//
//   1) QUANTIZZARE il guadagno. Un test dice "gain = 1.5", ma il registro a
//      0x10 riceve 16 bit. Qualcuno deve trasformare 1.5 in 24576 (= 1.5 x
//      2^14), e quel qualcuno e' il SOFTWARE: nel sistema vero il driver, qui
//      il testbench. La funzione q2_14() qui sotto e' esattamente quello che
//      il driver dovra' fare, range check compreso.
//
//   2) CALCOLARE il valore atteso con arrotondamento e saturazione, per una
//      strada che non assomigli al DUT. Il DUT usa ap_fixed e i modi AP_RND /
//      AP_SAT; il golden model usa long long, una divisione con floor, e due
//      confronti. Se i due arrivano allo stesso numero, e' perche' il numero
//      e' giusto -- non perche' hanno fatto lo stesso errore.
//
//   3) SCEGLIERE STIMOLI che colpiscano i casi in cui le due decisioni
//      contano: prodotti che cadono esattamente a meta' (per AP_RND) e
//      prodotti che escono dai 32 bit (per AP_SAT). Con i valori "comodi" dei
//      gradini precedenti arrotondamento e saturazione non scattano mai, e
//      un test che non le fa scattare non le verifica.
//
//  Il caso "gain enorme: verifica il TRONCAMENTO" del gradino 1.3 sparisce:
//  documentava un comportamento che ora non esiste piu'. Fatto girare il
//  vecchio testbench contro questo DUT, FALLISCE -- ed e' giusto cosi'
//  (docs/00 par. 9).
//
//  --------------------------------------------------------------------------
//  UNA USCITA NON VERIFICATA E' UNA USCITA CHE NON ESISTE
//  --------------------------------------------------------------------------
//  Regola dal gradino 1.4: il testbench verifica anche quello che la IP dice
//  di se' stessa (il registro sample_count), non solo i dati che produce. In
//  un testbench VHDL verrebbe naturale -- se una entity ha una porta di
//  uscita, la guardi. In HLS la tentazione e' di considerare "veri" solo i
//  dati dello stream. Non lo sono.
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
//  AXI4-Lite, la C simulation non lo sa e non lo verifica. Lo verifica la
//  cosimulation, che riusa QUESTO file: l'esito della cosim e' il valore di
//  ritorno di main() (0 = PASS), quindi un testbench che stampa e ritorna
//  sempre 0 farebbe passare anche un hardware sbagliato.
//
// =============================================================================
#include "axis_scaler.hpp"

#include <climits>   // INT_MAX, INT_MIN
#include <cmath>     // std::round
#include <cstdint>   // int16_t
#include <cstdio>
#include <vector>


// =============================================================================
//  IL LATO SOFTWARE: quantizzare il guadagno in Q2.14
// =============================================================================
//
//  Il formato e' deciso in axis_scaler.hpp (gain_t = ap_fixed<16,2>): 14 bit
//  frazionari, quindi il valore intero da scrivere nel registro e'
//
//        raw = round(gain * 2^14)      con raw in [-32768, 32767]
//
//  cioe' gain in [-2, 2 - 2^-14]. Il range check NON e' opzionale: e' stato
//  verificato con il compilatore che  gain_t g = 2.0;  non da' errore e
//  avvolge a -2.0 (bit 0x8000). Un driver che non controlla il range inverte
//  il segno del segnale senza accorgersene.
//
//  Ritorna false se il valore non e' rappresentabile.
//
static const double Q2_14_SCALA = 16384.0;   // 2^14

static bool q2_14(double gain, int16_t &raw)
{
    double scalato = std::round(gain * Q2_14_SCALA);
    if (scalato < -32768.0 || scalato > 32767.0) {
        return false;
    }
    raw = (int16_t)scalato;
    return true;
}

//  Costruisce il gain_t da passare al DUT a partire dai bit grezzi.
//
//  E' l'unico punto del testbench che tocca ap_fixed, e lo fa nel modo piu'
//  "hardware" possibile: scrive i 16 bit cosi' come sono, come farebbe una
//  scrittura AXI4-Lite nel registro a 0x10. Non passa per una conversione da
//  double, di proposito: cosi' la quantizzazione resta un lavoro di q2_14(),
//  che e' codice nostro e verificabile, e non del costruttore di ap_fixed.
//
static gain_t gain_da_bit(int16_t raw)
{
    gain_t g;
    g.range(15, 0) = (ap_uint<16>)(uint16_t)raw;
    return g;
}


// =============================================================================
//  IL GOLDEN MODEL -- parte 1: il dato
// =============================================================================
//
//  Calcola  y = sat32( round( x * gain ) )  in aritmetica intera pura.
//
//  Il trucco e' lavorare sui bit grezzi del guadagno: x * raw e' esattamente
//  x * gain * 2^14, un intero che sta in 47 bit (2^31 * 2^15), quindi in un
//  long long senza perdere niente. Da li':
//
//        AP_RND  =  floor( (p + 2^13) / 2^14 )    aggiungi mezzo LSB, poi floor
//        AP_SAT  =  min(max(r, INT_MIN), INT_MAX)
//
//  L'arrotondamento "meta' verso +inf" di AP_RND (dall'header del tool: il bit
//  scartato piu' alto viene sommato, sempre) e' proprio floor(v + 0.5):
//  50.5 -> 51, e -50.5 -> -50, non -51. Il golden model lo riproduce con una
//  formula diversa da quella del DUT, e i casi di prova qui sotto colpiscono
//  esattamente le mezze unita' per controllare che i due siano d'accordo.
//
//  LA REGOLA DEL GOLDEN MODEL, dal gradino 1.3: non deve assomigliare al DUT.
//  Niente ap_fixed, niente AP_RND: long long, una divisione e due if.
//

//  Divisione intera per 2^14 con FLOOR (verso -inf), che e' quello che fa
//  l'hardware quando scarta i bit bassi di un numero in complemento a due.
//  In C la divisione tronca verso ZERO (-3/2 = -1, non -2), quindi per i
//  negativi con resto va corretta di uno.
static long long floor_div_2p14(long long v)
{
    long long q = v / 16384;
    if ((v % 16384 != 0) && (v < 0)) q--;
    return q;
}

static int golden_scala(int x, int16_t raw_gain)
{
    long long prodotto = (long long)x * (long long)raw_gain;      // esatto
    long long r        = floor_div_2p14(prodotto + (1LL << 13));  // AP_RND
    if (r > INT_MAX) r = INT_MAX;                                 // AP_SAT
    if (r < INT_MIN) r = INT_MIN;
    return (int)r;
}


// =============================================================================
//  IL GOLDEN MODEL -- parte 2: il conteggio (gradino 1.4, invariato)
// =============================================================================
//
//  Conta i beat del vettore di stimoli fino al primo TLAST incluso, cioe'
//  guarda il bus come lo guarderebbe un osservatore esterno. Non usa il numero
//  di campioni chiesto dal test: se uno stimolo mettesse TLAST a meta'
//  pacchetto, il DUT si fermerebbe li' e il modello anche.
//
static int golden_conteggio(const std::vector<pkt_t> &stimoli)
{
    int n = 0;
    for (size_t i = 0; i < stimoli.size(); i++) {
        n++;
        if (stimoli[i].last == 1) break;
    }
    return n;
}


// -----------------------------------------------------------------------------
//  Il valore con cui "sporchiamo" il registro di stato prima della chiamata:
//  impossibile (negativo), cosi' se il DUT non scrive il registro il test se
//  ne accorge. E' la controparte software del bit _ap_vld.
// -----------------------------------------------------------------------------
static const int SPORCO = -999999;


// -----------------------------------------------------------------------------
//  Lo stimolo di default dei gradini precedenti: valori alternati positivi e
//  negativi, crescenti, riconoscibili a occhio nelle waveform.
//
//  Da questo gradino i casi di prova possono anche passare un vettore di
//  valori scelti a mano: per arrotondamento e saturazione servono numeri
//  precisi, non un pattern.
// -----------------------------------------------------------------------------
static std::vector<int> pattern(int n_campioni)
{
    std::vector<int> v;
    for (int i = 0; i < n_campioni; i++) {
        v.push_back((i % 2 == 0) ? (100 + i) : -(100 + i));
    }
    return v;
}


// -----------------------------------------------------------------------------
//  Un caso di prova: fa passare i campioni "valori" nell'IP con un certo gain
//  (reale, quantizzato qui in Q2.14) e verifica dati, protocollo e registro
//  di stato contro il golden model.
//
//  Ritorna il numero di errori trovati (0 = tutto bene).
// -----------------------------------------------------------------------------
static int prova_pacchetto(const std::vector<int> &valori, double gain,
                           const char *descrizione)
{
    const int n_campioni = (int)valori.size();

    // -------------------------------------------------------------------------
    //  0) QUANTIZZAZIONE del guadagno: il lavoro del driver.
    //
    //  Un gain fuori range e' un errore del TEST, non del DUT, e lo trattiamo
    //  come tale: il caso conta come fallito e lo dice chiaramente.
    // -------------------------------------------------------------------------
    int16_t raw = 0;
    if (!q2_14(gain, raw)) {
        printf("\n--- gain = %g  (%s) ---\n", gain, descrizione);
        printf("  ERRORE DI TEST: gain %g non rappresentabile in Q2.14 "
               "[-2, 1.99993896]\n", gain);
        return 1;
    }

    printf("\n--- %d campioni, gain = %g  (Q2.14: %d = 0x%04x)  (%s) ---\n",
           n_campioni, gain, (int)raw, (unsigned)(uint16_t)raw, descrizione);

    hls::stream<pkt_t> s_axis("s_axis");
    hls::stream<pkt_t> m_axis("m_axis");

    std::vector<int>   atteso;    // dal golden model
    std::vector<pkt_t> stimoli;   // per il golden model del conteggio

    // -------------------------------------------------------------------------
    //  1) STIMOLO: riempiamo lo stream d'ingresso
    // -------------------------------------------------------------------------
    for (int i = 0; i < n_campioni; i++) {

        pkt_t campione;
        campione.data = valori[i];

        // TKEEP e TSTRB: tutti i byte validi. -1 su un ap_uint<4> vale 0b1111,
        // come (others => '1') in VHDL.
        campione.keep = -1;
        campione.strb = -1;

        // TLAST alto solo sull'ultimo campione.
        campione.last = (i == n_campioni - 1) ? 1 : 0;

        s_axis.write(campione);
        stimoli.push_back(campione);

        // Il valore atteso lo calcola il GOLDEN MODEL, sui bit grezzi.
        atteso.push_back(golden_scala(valori[i], raw));
    }

    // -------------------------------------------------------------------------
    //  2) ESECUZIONE: una chiamata = una transazione dell'IP = un pacchetto.
    //
    //  In hardware:
    //        scrivi raw nel registro gain          (offset 0x10, 16 bit)
    //        scrivi CTRL bit0 = 1                  (ap_start)
    //        aspetta ap_done
    //        leggi il registro sample_count        (offset 0x18)
    // -------------------------------------------------------------------------
    int sample_count = SPORCO;

    axis_scaler(s_axis, m_axis, gain_da_bit(raw), &sample_count);

    // -------------------------------------------------------------------------
    //  3) VERIFICA
    // -------------------------------------------------------------------------
    int errori = 0;

    // Controllo 1: e' uscito il numero giusto di campioni?
    if ((int)m_axis.size() != n_campioni) {
        printf("  ERRORE: attesi %d campioni in uscita, ne sono usciti %d\n",
               n_campioni, (int)m_axis.size());
        errori++;
    }

    // Controllo 2: i dati e i segnali di protocollo.
    for (int i = 0; i < n_campioni && !m_axis.empty(); i++) {

        pkt_t uscita = m_axis.read();

        // 2a) il dato, confrontato col golden model. Il messaggio stampa
        //     anche l'ingresso: per arrotondamento e saturazione e' quello
        //     che serve per capire al volo cosa e' andato storto.
        int ottenuto = (int)uscita.data;
        if (ottenuto != atteso[i]) {
            printf("  ERRORE campione %d: x = %d, atteso %d, ottenuto %d\n",
                   i, valori[i], atteso[i], ottenuto);
            errori++;
        }

        // 2b) TLAST: un TLAST mancante manda in stallo il DMA a valle.
        ap_uint<1> last_atteso = (i == n_campioni - 1) ? 1 : 0;
        if (uscita.last != last_atteso) {
            printf("  ERRORE campione %d: TLAST atteso %d, ottenuto %d\n",
                   i, (int)last_atteso, (int)uscita.last);
            errori++;
        }

        // 2c) TKEEP e TSTRB propagati intatti.
        const ap_uint<C_DATA_WIDTH/8> tutti_uni = -1;
        if (uscita.keep != tutti_uni || uscita.strb != tutti_uni) {
            printf("  ERRORE campione %d: TKEEP/TSTRB alterati (keep=%d strb=%d)\n",
                   i, (int)uscita.keep, (int)uscita.strb);
            errori++;
        }
    }

    // Controllo 3: lo stream d'ingresso e' stato consumato tutto?
    if (!s_axis.empty()) {
        printf("  ERRORE: sono rimasti %d campioni non letti in ingresso\n",
               (int)s_axis.size());
        errori++;
    }

    // Controllo 4: il registro di stato (gradino 1.4).
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
//  Auto-test del quantizzatore.
//
//  Non verifica il DUT: verifica che il LATO SOFTWARE sappia cosa sta
//  scrivendo nel registro. E' il tipo di controllo che in un driver vero
//  finisce in uno unit test, e qui serve a fissare tre fatti del formato:
//  gli estremi, la risoluzione, e che 2.0 NON esiste.
// -----------------------------------------------------------------------------
static int prova_quantizzatore()
{
    printf("\n--- il quantizzatore Q2.14 (lato software) ---\n");
    int errori = 0;
    int16_t raw = 0;

    struct caso_t { double gain; bool ok; int16_t atteso; const char *nota; };
    const caso_t casi[] = {
        {  1.0,           true,   16384, "uno"                          },
        {  0.5,           true,    8192, "un mezzo"                     },
        { -2.0,           true,  -32768, "il minimo del formato"        },
        {  1.99993896,    true,   32767, "il massimo: 2 - 2^-14"        },
        {  0.00006103515, true,       1, "la risoluzione: 2^-14"        },
        {  2.0,           false,      0, "NON rappresentabile: rifiutato"},
        { -2.00006,       false,      0, "sotto il minimo: rifiutato"   },
    };

    for (size_t i = 0; i < sizeof(casi)/sizeof(casi[0]); i++) {
        bool ok = q2_14(casi[i].gain, raw);
        bool giusto = (ok == casi[i].ok) && (!ok || raw == casi[i].atteso);
        if (ok) printf("  %-14g -> raw %6d  (%s)\n", casi[i].gain, (int)raw, casi[i].nota);
        else    printf("  %-14g -> rifiutato   (%s)\n", casi[i].gain, casi[i].nota);
        if (!giusto) {
            printf("  ERRORE: atteso %s %d\n",
                   casi[i].ok ? "raw" : "rifiuto", (int)casi[i].atteso);
            errori++;
        }
    }
    return errori;
}


// -----------------------------------------------------------------------------
//  main() -- il punto d'ingresso del testbench
// -----------------------------------------------------------------------------
int main()
{
    printf("=====================================================\n");
    printf(" axis_scaler -- testbench gradino 1.6 (virgola fissa)\n");
    printf("=====================================================\n");

    int errori = 0;

    errori += prova_quantizzatore();

    // -------------------------------------------------------------------------
    //  Casi limite del protocollo, a gain unitario (non regressione dal 1.5:
    //  con gain = 1.0 il prodotto e' esatto e l'IP deve comportarsi come un
    //  pass-through).
    // -------------------------------------------------------------------------
    printf("\n=== protocollo, gain = 1.0 ===\n");
    errori += prova_pacchetto(pattern(1),  1.0, "pacchetto minimo: TLAST sul primo beat");
    errori += prova_pacchetto(pattern(8),  1.0, "identita': deve comportarsi come il pass-through");
    errori += prova_pacchetto(pattern(17), 1.0, "lunghezza dispari, non potenza di due");

    // -------------------------------------------------------------------------
    //  Casi dell'aritmetica che gia' conoscevamo, ora in virgola fissa.
    // -------------------------------------------------------------------------
    printf("\n=== aritmetica: i casi dei gradini precedenti ===\n");
    errori += prova_pacchetto(pattern(8),  0.0, "gain nullo: uscita a zero, conteggio no");
    errori += prova_pacchetto(pattern(8), -1.0, "gain negativo: aritmetica con segno");
    errori += prova_pacchetto(pattern(8),  1.5, "gain frazionario: la ragione di ap_fixed");

    // -------------------------------------------------------------------------
    //  IL CASO NUOVO 1: L'ARROTONDAMENTO (AP_RND).
    //
    //  Con gain = 0.5, un x dispari da' un prodotto che cade ESATTAMENTE a
    //  meta' fra due interi: 101 * 0.5 = 50.5. Qui la politica conta:
    //
    //        AP_TRN (il default)     50.5 -> 50     -50.5 -> -51   (floor)
    //        AP_RND (il nostro)      50.5 -> 51     -50.5 -> -50   (+0.5, floor)
    //
    //  Nota l'asimmetria di AP_RND sui negativi: -50.5 va a -50, cioe' verso
    //  +inf, non "via dallo zero". E' la definizione del modo (dall'header
    //  del tool: "rounding to plus infinity"), e il golden model la riproduce
    //  con floor(v + 0.5). Se un giorno servisse un arrotondamento senza
    //  bias, il modo e' AP_RND_CONV (pari piu' vicino).
    //
    //  Con gain = 0.25 si coprono anche i quarti: 25.25 -> 25, 25.75 -> 26.
    // -------------------------------------------------------------------------
    printf("\n=== AP_RND: prodotti a meta' fra due interi ===\n");
    errori += prova_pacchetto({101, -101, 103, -103, 1, -1, 3, -3}, 0.5,
                              "x dispari * 0.5: la meta' esatta va verso +inf");
    errori += prova_pacchetto({101, 102, 103, -101, -102, -103}, 0.25,
                              "quarti: 0.25 -> giu', 0.5 -> su, 0.75 -> su");

    // -------------------------------------------------------------------------
    //  IL CASO NUOVO 2: LA SATURAZIONE (AP_SAT).
    //
    //  Con |gain| < 2 il prodotto esce dai 32 bit solo se |x| e' vicino a
    //  2^31: servono ingressi scelti apposta.
    //
    //        INT_MAX * 1.5    = +3.2e9   -> non ci sta  -> INT_MAX
    //        INT_MIN * 1.5    = -3.2e9   -> non ci sta  -> INT_MIN
    //        2^30    * 1.5    = 1.61e9   -> ci sta      -> esatto
    //
    //  E il caso che da solo giustifica AP_SAT, perche' con il troncamento
    //  del 1.3 dava il SEGNO SBAGLIATO:
    //
    //        INT_MIN * -1.0   = +2^31    -> con AP_WRAP: -2^31 (INT_MIN!)
    //                                    -> con AP_SAT:  +2^31 - 1
    // -------------------------------------------------------------------------
    printf("\n=== AP_SAT: prodotti fuori dai 32 bit ===\n");
    errori += prova_pacchetto({INT_MAX, INT_MIN, 1 << 30, -(1 << 30), 0}, 1.5,
                              "ai bordi: satura sopra e sotto, esatto in mezzo");
    errori += prova_pacchetto({INT_MIN, INT_MAX, -1, 1}, -1.0,
                              "-(-2^31): con il wrap era INT_MIN, ora INT_MAX");

    // -------------------------------------------------------------------------
    //  Gli estremi del formato del guadagno.
    // -------------------------------------------------------------------------
    printf("\n=== gli estremi di Q2.14 ===\n");
    errori += prova_pacchetto(pattern(8), -2.0,       "il minimo: -2.0");
    errori += prova_pacchetto(pattern(8),  1.99993896, "il massimo: 2 - 2^-14 (2.0 non esiste)");

    // -------------------------------------------------------------------------
    //  Due transazioni consecutive (gradino 1.4): il conteggio non si accumula.
    // -------------------------------------------------------------------------
    printf("\n=== due transazioni consecutive: il conteggio non si accumula ===\n");
    errori += prova_pacchetto(pattern(3), 1.0, "prima transazione: sample_count deve dare 3");
    errori += prova_pacchetto(pattern(5), 1.0, "seconda transazione: deve dare 5, non 8");

    printf("\n=====================================================\n");
    if (errori == 0) {
        printf(" RISULTATO: PASS  (0 errori)\n");
    } else {
        printf(" RISULTATO: FAIL  (%d errori)\n", errori);
    }
    printf("=====================================================\n");

    // Il valore di ritorno E' il risultato del test (anche per la cosim).
    return errori;
}
