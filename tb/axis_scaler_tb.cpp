// =============================================================================
//  axis_scaler_tb.cpp  --  Testbench auto-verificante
// =============================================================================
//
//  GRADINO 1.7
//
//  LA NOVITA': il DUT ha memoria. Due statistiche (total_samples,
//  packet_count) si accumulano da una chiamata all'altra, e il testbench
//  deve verificarle per UNA STRADA INDIPENDENTE DAL DUT:
//
//   1) due contatori GLOBALI del testbench, aggiornati a ogni caso di prova
//      a partire dal PRIMO di main() -- perche' le static del DUT accumulano
//      da sempre, non dal caso "3 poi 5". Il valore atteso viene dal vettore
//      di stimoli (golden_conteggio, che conta i beat fino al primo TLAST),
//      MAI dal sample_count che il DUT stesso riporta: se il DUT sbagliasse
//      sample_count e total_samples nello stesso modo, un atteso costruito
//      su sample_count non se ne accorgerebbe;
//
//   2) il caso "3 poi 5" ora verifica tre cose: sample_count 3 poi 5 (NON 8,
//      il paletto del 1.4), total_samples +3 poi +5, packet_count +2;
//
//   3) le uscite nuove vengono sporcate prima di ogni chiamata, come
//      sample_count al 1.4: un DUT che non le scrive deve essere scoperto.
//
//  Il tb del 1.6 contro questo DUT NON COMPILA (4 argomenti contro 6): il
//  fallimento e' a compile-time, non semantico. L'osservazione speculare: un
//  DRIVER scritto per il 1.6 funzionerebbe ancora, perche' 0x10 e 0x18 non si
//  sono mossi. La firma C si rompe, la mappa registri no (docs/00 par. 10).
//
//  --------------------------------------------------------------------------
//  UNA USCITA NON VERIFICATA E' UNA USCITA CHE NON ESISTE  (regola dal 1.4)
//  --------------------------------------------------------------------------
//
//  Questo file NON viene sintetizzato: gira sul PC come un programma C++.
//  Qui non esiste il tempo (hls::stream e' una coda infinita) e non esistono
//  bus ne' indirizzi: la C simulation verifica l'ALGORITMO. Che i puntatori
//  diventino registri a 0x18/0x20/0x28 con i bit _ap_vld lo verifica la
//  cosimulation, che riusa QUESTO file: l'esito e' il valore di ritorno di
//  main() (0 = PASS).
//
//  Nota per la cosim: le transazioni girano in sequenza su UNA istanza RTL,
//  quindi la persistenza delle static viene verificata anche sull'hardware
//  simulato, non solo in C.
//
// =============================================================================
#include "axis_scaler.hpp"

#include <climits>   // INT_MAX, INT_MIN
#include <cmath>     // std::round
#include <cstdint>   // int16_t
#include <cstdio>
#include <vector>


// =============================================================================
//  IL LATO SOFTWARE: quantizzare il guadagno in Q2.14  (INVARIATO dal 1.6)
// =============================================================================
//
//  raw = round(gain * 2^14), con range check: gain_t g = 2.0 avvolge a -2.0
//  in silenzio, quindi il controllo lo fa chi quantizza (docs/00 par. 9).
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

//  Il gain_t per il DUT, dai bit grezzi: come farebbe la scrittura AXI a 0x10.
static gain_t gain_da_bit(int16_t raw)
{
    gain_t g;
    g.range(15, 0) = (ap_uint<16>)(uint16_t)raw;
    return g;
}


// =============================================================================
//  IL GOLDEN MODEL -- parte 1: il dato  (INVARIATO dal 1.6)
// =============================================================================
//
//  y = sat32( floor( (x * raw + 2^13) / 2^14 ) ) in long long: una strada
//  diversa da quella del DUT ((p >> 14) + bit13, docs/00 par. 9).
//
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
//  IL GOLDEN MODEL -- parte 2: il conteggio per-pacchetto  (INVARIATO dal 1.4)
// =============================================================================
//
//  Conta i beat del vettore di stimoli fino al primo TLAST incluso: guarda il
//  bus come un osservatore esterno, non usa il numero di campioni chiesto.
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


// =============================================================================
//  IL GOLDEN MODEL -- parte 3: gli accumuli  (la novita' del 1.7)
// =============================================================================
//
//  Due contatori che vivono quanto il testbench, cioe' quanto le static del
//  DUT. Si aggiornano DOPO ogni chiamata riuscita, con il conteggio del
//  golden model (parte 2), e sono l'unico "atteso" per total_samples e
//  packet_count.
//
//  unsigned come nel DUT, e per lo stesso motivo: se un giorno il test
//  facesse avvolgere il totale, l'aritmetica modulo 2^32 e' la stessa.
//
//  Sono le uniche variabili globali del testbench. Non sono static di
//  funzione di proposito: cosi' si vede a colpo d'occhio che sono lo
//  specchio software delle due static del DUT.
//
static unsigned int atteso_tot_campioni  = 0;
static unsigned int atteso_tot_pacchetti = 0;


// -----------------------------------------------------------------------------
//  Il valore con cui "sporchiamo" i registri di stato prima della chiamata.
//  Per sample_count (int) un negativo impossibile; per i due unsigned un
//  pattern riconoscibile che nessun accumulo di questo testbench raggiunge.
// -----------------------------------------------------------------------------
static const int          SPORCO   = -999999;
static const unsigned int SPORCO_U = 0xDEADBEEFu;


// -----------------------------------------------------------------------------
//  Lo stimolo di default: valori alternati positivi e negativi, crescenti.
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
//  e verifica dati, protocollo, il registro per-pacchetto E i due registri
//  cumulativi contro il golden model.
//
//  Ritorna il numero di errori trovati (0 = tutto bene).
// -----------------------------------------------------------------------------
static int prova_pacchetto(const std::vector<int> &valori, double gain,
                           const char *descrizione)
{
    const int n_campioni = (int)valori.size();

    // 0) QUANTIZZAZIONE del guadagno: il lavoro del driver.
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

    // 1) STIMOLO
    for (int i = 0; i < n_campioni; i++) {
        pkt_t campione;
        campione.data = valori[i];
        campione.keep = -1;                                   // (others => '1')
        campione.strb = -1;
        campione.last = (i == n_campioni - 1) ? 1 : 0;

        s_axis.write(campione);
        stimoli.push_back(campione);
        atteso.push_back(golden_scala(valori[i], raw));
    }

    // -------------------------------------------------------------------------
    //  2) ESECUZIONE: una chiamata = una transazione dell'IP = un pacchetto.
    //
    //  In hardware:
    //        scrivi raw nel registro gain          (0x10, 16 bit)
    //        scrivi CTRL bit0 = 1                  (ap_start)
    //        aspetta ap_done
    //        leggi sample_count                    (0x18)
    //        leggi total_samples                   (0x20)   <- nuovo
    //        leggi packet_count                    (0x28)   <- nuovo
    //
    //  I tre puntatori vengono sporcati PRIMA: la controparte software del
    //  bit _ap_vld, per distinguere "valore vero" da "mai scritto".
    // -------------------------------------------------------------------------
    int          sample_count  = SPORCO;
    unsigned int total_samples = SPORCO_U;
    unsigned int packet_count  = SPORCO_U;

    axis_scaler(s_axis, m_axis, gain_da_bit(raw),
                &sample_count, &total_samples, &packet_count);

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

        int ottenuto = (int)uscita.data;
        if (ottenuto != atteso[i]) {
            printf("  ERRORE campione %d: x = %d, atteso %d, ottenuto %d\n",
                   i, valori[i], atteso[i], ottenuto);
            errori++;
        }

        ap_uint<1> last_atteso = (i == n_campioni - 1) ? 1 : 0;
        if (uscita.last != last_atteso) {
            printf("  ERRORE campione %d: TLAST atteso %d, ottenuto %d\n",
                   i, (int)last_atteso, (int)uscita.last);
            errori++;
        }

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

    // Controllo 4: il registro di stato per-pacchetto (gradino 1.4).
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

    // -------------------------------------------------------------------------
    //  Controllo 5: i due registri cumulativi (gradino 1.7).
    //
    //  L'atteso si aggiorna PRIMA del confronto, con il conteggio del golden
    //  model -- non con sample_count del DUT. Poi si confronta.
    //
    //  Si aggiorna anche se il caso ha altri errori: il DUT ha comunque
    //  consumato un pacchetto, e gli accumuli dei casi SUCCESSIVI devono
    //  restare confrontabili invece di trascinarsi un errore solo.
    // -------------------------------------------------------------------------
    atteso_tot_campioni  += (unsigned int)conteggio_atteso;
    atteso_tot_pacchetti += 1;

    if (total_samples == SPORCO_U) {
        printf("  ERRORE: total_samples non e' stato scritto dalla IP\n");
        errori++;
    } else if (total_samples != atteso_tot_campioni) {
        printf("  ERRORE: total_samples atteso %u, ottenuto %u\n",
               atteso_tot_campioni, total_samples);
        errori++;
    }

    if (packet_count == SPORCO_U) {
        printf("  ERRORE: packet_count non e' stato scritto dalla IP\n");
        errori++;
    } else if (packet_count != atteso_tot_pacchetti) {
        printf("  ERRORE: packet_count atteso %u, ottenuto %u\n",
               atteso_tot_pacchetti, packet_count);
        errori++;
    }

    if (errori == 0) {
        printf("  OK: %d campioni scalati, TLAST e TKEEP intatti, "
               "sample_count = %d, total_samples = %u, packet_count = %u\n",
               n_campioni, sample_count, total_samples, packet_count);
    }

    return errori;
}


// -----------------------------------------------------------------------------
//  Auto-test del quantizzatore (INVARIATO dal 1.6): verifica il lato
//  software, non il DUT.
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
//
//  L'ORDINE DEI CASI CONTA, da questo gradino: ogni caso e' una transazione
//  che sposta le static del DUT, e i contatori attesi seguono lo stesso
//  ordine. Spostare un caso non rompe niente (gli attesi si muovono con
//  lui), ma i valori stampati cambiano.
// -----------------------------------------------------------------------------
int main()
{
    printf("=====================================================\n");
    printf(" axis_scaler -- testbench gradino 1.7 (static)\n");
    printf("=====================================================\n");

    int errori = 0;

    errori += prova_quantizzatore();

    // -------------------------------------------------------------------------
    //  IL PRIMO PACCHETTO: i totali partono da zero.
    //
    //  E' l'unico caso in cui il valore assoluto dei totali e' noto a priori
    //  (1 campione, 1 pacchetto): in C perche' le static sono inizializzate
    //  a 0, in cosim perche' il := iniziale dei registri e' 0. Quello che
    //  succede DOPO un reset a meta' vita, la cosim non lo mostra: si legge
    //  nel VHDL (docs/00 par. 10) e si vedra' in waveform in Fase 3.
    // -------------------------------------------------------------------------
    printf("\n=== protocollo, gain = 1.0 ===\n");
    errori += prova_pacchetto(pattern(1),  1.0, "pacchetto minimo: TLAST sul primo beat; totali 1 e 1");
    errori += prova_pacchetto(pattern(8),  1.0, "identita': deve comportarsi come il pass-through");
    errori += prova_pacchetto(pattern(17), 1.0, "lunghezza dispari, non potenza di due");

    printf("\n=== aritmetica: i casi dei gradini precedenti ===\n");
    errori += prova_pacchetto(pattern(8),  0.0, "gain nullo: uscita a zero, conteggio no");
    errori += prova_pacchetto(pattern(8), -1.0, "gain negativo: aritmetica con segno");
    errori += prova_pacchetto(pattern(8),  1.5, "gain frazionario: la ragione di ap_fixed");

    printf("\n=== AP_RND: prodotti a meta' fra due interi ===\n");
    errori += prova_pacchetto({101, -101, 103, -103, 1, -1, 3, -3}, 0.5,
                              "x dispari * 0.5: la meta' esatta va verso +inf");
    errori += prova_pacchetto({101, 102, 103, -101, -102, -103}, 0.25,
                              "quarti: 0.25 -> giu', 0.5 -> su, 0.75 -> su");

    printf("\n=== AP_SAT: prodotti fuori dai 32 bit ===\n");
    errori += prova_pacchetto({INT_MAX, INT_MIN, 1 << 30, -(1 << 30), 0}, 1.5,
                              "ai bordi: satura sopra e sotto, esatto in mezzo");
    errori += prova_pacchetto({INT_MIN, INT_MAX, -1, 1}, -1.0,
                              "-(-2^31): con il wrap era INT_MIN, ora INT_MAX");

    printf("\n=== gli estremi di Q2.14 ===\n");
    errori += prova_pacchetto(pattern(8), -2.0,       "il minimo: -2.0");
    errori += prova_pacchetto(pattern(8),  1.99993896, "il massimo: 2 - 2^-14 (2.0 non esiste)");

    // -------------------------------------------------------------------------
    //  IL CASO DEL GRADINO: locale contro static, fianco a fianco.
    //
    //  Stessi due pacchetti del 1.4. sample_count deve dare 3 poi 5 (NON 8:
    //  e' locale, riparte); total_samples deve crescere di 3 poi di 5;
    //  packet_count di 1 poi di 1. Il messaggio OK stampa tutti e tre, cosi'
    //  la differenza si legge nel log.
    //
    //  Prima del caso, i totali valgono gia' quello che i dodici casi sopra
    //  hanno accumulato (1+8+17+8+8+8+8+6+5+4+8+8 = 89 campioni, 12
    //  pacchetti): il DUT li ricorda, e il testbench pure.
    // -------------------------------------------------------------------------
    printf("\n=== locale contro static: 3 poi 5 ===\n");
    printf("  (totali attesi prima: %u campioni, %u pacchetti)\n",
           atteso_tot_campioni, atteso_tot_pacchetti);
    errori += prova_pacchetto(pattern(3), 1.0, "sample_count 3; total_samples +3; packet_count +1");
    errori += prova_pacchetto(pattern(5), 1.0, "sample_count 5, non 8; total_samples +5; packet_count +1");

    printf("\n=====================================================\n");
    if (errori == 0) {
        printf(" RISULTATO: PASS  (0 errori)  --  totali finali: %u campioni, %u pacchetti\n",
               atteso_tot_campioni, atteso_tot_pacchetti);
    } else {
        printf(" RISULTATO: FAIL  (%d errori)\n", errori);
    }
    printf("=====================================================\n");

    // Il valore di ritorno E' il risultato del test (anche per la cosim).
    return errori;
}
