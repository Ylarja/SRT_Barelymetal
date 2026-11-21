// Listener_ZCU104.cpp
//
// Nodo LISTENER HALO sulla ZCU104.
// Osserva i canali elencati in CHANNELS e scrive:
//
// 1) UNA riga per ogni evento
//
//      REQ  richiesta su lane 0   (controller -> responder)   tag, nessun payload
//      RSP  risposta  su lane 0   (responder  -> controller)  tag, latenza, esito, payload
//      MSG  messaggio su lane 1   (controller -> responder)   payload
//
//      time[s]  channel  src  dst  lane  type  tag  lat[us]  esito  payload
//
//    time    = istante di arrivo del frame sul bus (ultimo frame per RSP e MSG),
//              in secondi dall'avvio del listener, risoluzione 1 us.
//    src/dst = nodi sorgente e destinazione (dalla configurazione del canale: il Network
//              scarta i frame il cui header non corrisponde alla direzione attesa).
//    lat     = (solo RSP) tempo sul bus tra la richiesta e l'ultimo frame della risposta.
//    esito   = (solo RSP) confronto con il lane0_rx_timeout del canale (T), con D = 2 ms:
//                OK       lat <= T - D
//                GRIGIA   T - D < lat <= T + D   (il controller potrebbe averla scartata)
//                TARDIVA  lat > T + D, oppure arrivata dopo la richiesta successiva
//                         dello stesso canale (in quel caso il controller aveva gia' rinunciato)
//              Con T = 0 il controllo e' disattivato e l'esito e' sempre OK.
//
// 2) FREQUENZA A FINESTRA MOBILE, per canale x REQ/RSP/MSG (ogni flusso separato)
//
//    #FREQ  statistiche sugli ultimi FREQ_SAMPLES intervalli del flusso, ristampate ogni
//           FREQ_HOP nuovi eventi dello stesso flusso (a 10 Hz: finestra di ~2 s,
//           una riga ogni ~0.5 s per flusso)
//      #FREQ  time[s]  channel  type  n  freq[Hz]  T_mean[us]  T_sd[us]  T_min[us]  T_max[us]
//      time = timestamp dell'ultimo evento entrato nella finestra
//      freq = n / (somma degli n intervalli) = 1e6 / T_mean
//    La prima riga di un flusso esce quando la finestra e' piena (FREQ_SAMPLES intervalli).
//    Un intervallo piu' lungo di GAP_FACTOR x periodo nominale (un'interruzione) svuota la
//    finestra: le righe riprendono dopo altri FREQ_SAMPLES intervalli.
//    Un flusso fermo non produce righe #FREQ.
//
// 3) Alla fine di ogni finestra di analisi:
//
//    #STAT  statistiche del PERIODO tra eventi consecutivi, per canale x REQ/RSP/MSG
//      #STAT  window[s]  channel  type  n  freq[Hz]  T_mean[us]  T_sd[us]  T_min[us]  T_max[us]  f_sd[Hz]
//      (riepilogo della finestra fissa; la frequenza "sensibile" e' quella di #FREQ)
//
//    #TOUT  esiti delle richieste chiuse nella finestra, per canale
//      #TOUT  window[s]  channel  timeout[us]  ok  grigia  tardive  mancanti  lat_mean[us]  lat_max[us]
//      mancanti = richieste senza risposta entro T + D (sul bus)
//
//    #ALIEN ID CAN estesi (29 bit) che non appartengono alla rete configurata, con il numero
//           di frame inviati nella finestra
//      #ALIEN  window[s]  ids=N  0xID:frame ...  [+X frame da ID non registrati]
//
//    Le finestre sono consecutive e non sovrapposte.
//
// 4) A fine missione (Ctrl+C), su stderr, insieme ai totali:
//
//    Latenza max di elaborazione [us]  per REQ / RSP / MSG: tempo tra l'arrivo dell'ultimo
//                                      frame sul bus e il momento in cui il listener ha in mano
//                                      l'evento (prima della stampa). "-" = nessun evento.
//    Tempo max di Service(1) [us]      durata massima di una singola chiamata halo.Service(1).
//
//    Il ciclo principale e' in busy-poll (nessuna pausa): il thread occupa un core al 100%.
//
// USO
//      ./listener [durata_finestra_in_secondi]        (default: 7)
//
//      ./listener 5 > log.txt          log su file, errori e riepilogo a video
//      ./listener 7 > /tmp/log.txt 2> /tmp/err.txt &    prove: metriche finali in err.txt
//      grep -v '^#'    log.txt         solo gli eventi
//      grep '^#FREQ'   log.txt         solo la frequenza a finestra mobile
//      grep '^#STAT'   log.txt         solo le statistiche di periodo
//      grep '^#TOUT'   log.txt         solo gli esiti dei timeout
//      grep '^#ALIEN'  log.txt         solo gli ID esterni
//
// Gli eventi NON sono garantiti in ordine di tempo (una risposta viene stampata quando
// e' stata riassemblata): per ordinarli, `grep -v '^#' log.txt | sort -n`.
//
// PREREQUISITI
//   can0 attiva con lo stesso bitrate degli altri nodi (1 Mbit/s):
//       ip link set can0 down
//       ip link set can0 type can bitrate 1000000
//       ip link set can0 up
//
// Ctrl+C per fermare.

#include "Core/Services/Time/Include/Time.h"
#include "Core/DOL/Private/test.h"
#include "HALO/Config/Include/Config.h"
#include "HALO/Protocol/Include/HALO.h"

#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace {

// --- CONFIGURAZIONE -----------------------------------------------------------------------

#define LISTENER_NODE HALO_NODE_LISTENER

// Canali osservati: per aggiungerne uno basta aggiungere una riga.
const halo_channel_t *const CHANNELS[] = {
    &HALO_CHANNEL_CHANNEL1,
    &HALO_CHANNEL_CHANNEL2,
};
constexpr size_t NUM_CHANNELS = sizeof(CHANNELS) / sizeof(CHANNELS[0]);

constexpr double DEFAULT_WINDOW_S = 7.0;       // durata della finestra di analisi

// Timeout: ampiezza della zona grigia attorno a lane0_rx_timeout.
constexpr uint32_t TIMEOUT_DELTA_US   = 2000;
// Tempo che il listener si concede per elaborare una risposta gia' arrivata sul bus,
// prima di dichiarare MANCANTE una richiesta. Non entra nella classificazione degli esiti,
// che usa solo i timestamp del bus.
constexpr uint32_t LISTENER_MARGIN_US = 5000;

constexpr uint8_t LANE0 = 0;
constexpr uint8_t LANE1 = 1;
constexpr uint8_t NUM_TAGS = 8;                // il tag e' a 3 bit

// --- FREQUENZA A FINESTRA MOBILE: PARAMETRI -----------------------------------------------

// Periodi nominali del controller [us]. DEVONO coincidere con portenta.ino:
//   REQUEST_PERIOD_US   (richieste lane 0, e quindi le risposte)
//   LANE1_PERIOD_US     (messaggi lane 1)
// >>> Se cambi i periodi nello sketch del controller, aggiorna questi due valori. <<<
// Servono a riconoscere le interruzioni (GAP_FACTOR) e a dimensionare FREQ_SAMPLES.
constexpr uint32_t REQUEST_PERIOD_US = 100000;   // 100 ms
constexpr uint32_t LANE1_PERIOD_US   = 100000;   // 100 ms

// Intervalli nella finestra mobile di ogni flusso. La finestra copre circa
// FREQ_SAMPLES x periodo nominale: 20 x 100 ms = 2 s.
// >>> Se cambi il periodo e vuoi mantenere ~2 s di storia, adegua anche questo valore
//     (es. periodo 50 ms -> 40 campioni; periodo 200 ms -> 10 campioni). Max 255. <<<
// Piu' campioni = stima piu' stabile ma reazione piu' lenta ai cambi di frequenza.
constexpr uint8_t FREQ_SAMPLES = 20;

// Nuovi eventi dello stesso flusso tra una riga #FREQ e la successiva
// (5 eventi a 10 Hz = una riga ogni 0.5 s; finestre consecutive condividono 15 intervalli).
constexpr uint8_t FREQ_HOP = 5;

// Un intervallo piu' lungo di GAP_FACTOR x periodo nominale e' un'interruzione
// (es. riavvio del controller) e svuota la finestra mobile. Stesso criterio della GUI
// (halo_config.GAP_FACTOR). Una sola risposta mancante (intervallo = 2 x periodo) NON e'
// un'interruzione: resta nella finestra e abbassa la frequenza, come deve.
constexpr uint32_t GAP_FACTOR = 2;

static_assert(FREQ_SAMPLES >= 2, "servono almeno 2 intervalli per la deviazione standard");
static_assert(FREQ_HOP >= 1, "FREQ_HOP deve essere almeno 1");

// --- STATISTICHE --------------------------------------------------------------------------

// Media e varianza calcolate un campione alla volta (algoritmo di Welford):
// non serve tenere in memoria i campioni della finestra. Usato per le finestre fisse
// (#STAT, #TOUT), dove i campioni si aggiungono soltanto.
struct running_stats_t {
    uint32_t n;
    double   mean;
    double   m2;     // somma degli scarti quadratici dalla media
};

void Add_Sample(running_stats_t &s, double x)
{
    ++s.n;
    const double delta = x - s.mean;
    s.mean += delta / s.n;
    s.m2   += delta * (x - s.mean);
}

// Deviazione standard campionaria (n - 1). Valida solo con n >= 2.
double Std_Dev(const running_stats_t &s)
{
    return std::sqrt(s.m2 / (s.n - 1));
}

// Finestra mobile degli ultimi FREQ_SAMPLES intervalli di un flusso:
// buffer circolare che si comporta come uno shift register (entra il nuovo, esce il piu' vecchio).
// La somma e' intera, quindi la media resta esatta per tutta la durata della prova
// (Welford non gestisce bene la rimozione del campione piu' vecchio).
struct sliding_window_t {
    uint32_t interval_us[FREQ_SAMPLES];
    uint8_t  head;          // prossima posizione da scrivere (= la piu' vecchia a buffer pieno)
    uint8_t  n;             // intervalli validi (<= FREQ_SAMPLES)
    uint8_t  new_samples;   // intervalli entrati dall'ultima riga #FREQ
    uint64_t sum_us;        // somma esatta degli intervalli validi
};

void Sliding_Add(sliding_window_t &w, uint32_t interval_us)
{
    if (w.n == FREQ_SAMPLES) {
        w.sum_us -= w.interval_us[w.head];   // esce il campione piu' vecchio
    } else {
        ++w.n;
    }
    w.interval_us[w.head] = interval_us;
    w.sum_us += interval_us;
    w.head = static_cast<uint8_t>((w.head + 1) % FREQ_SAMPLES);
    if (w.new_samples < 255) {
        ++w.new_samples;
    }
}

// Vero quando la finestra e' piena e sono entrati almeno FREQ_HOP intervalli dall'ultima riga.
bool Sliding_Ready(const sliding_window_t &w)
{
    return w.n == FREQ_SAMPLES && w.new_samples >= FREQ_HOP;
}

enum stream_t : uint8_t { STREAM_REQ, STREAM_RSP, STREAM_MSG, NUM_STREAMS };
const char *const STREAM_NAME[NUM_STREAMS] = {"REQ", "RSP", "MSG"};

// Statistiche di un flusso (un tipo di evento su un canale).
struct stream_stats_t {
    uint32_t nominal_us;       // periodo nominale (REQUEST_PERIOD_US o LANE1_PERIOD_US)
    bool     has_previous;     // esiste un evento precedente da cui misurare l'intervallo
    uint32_t previous_us;      // timestamp dell'evento precedente

    // finestra fissa corrente (#STAT)
    running_stats_t period;    // intervalli tra eventi consecutivi [us]
    running_stats_t frequency; // frequenza istantanea 1/intervallo [Hz]
    uint32_t min_us;
    uint32_t max_us;

    // finestra mobile (#FREQ): NON viene azzerata a fine finestra fissa
    sliding_window_t sliding;
};

// Registra un evento del flusso. Restituisce true se e' il momento di stampare una riga #FREQ.
bool Record_Event(stream_stats_t &s, uint32_t timestamp_us)
{
    if (s.has_previous) {
        const uint32_t interval_us = timestamp_us - s.previous_us;   // sicuro rispetto all'overflow
        if (interval_us > 0) {
            // Finestra fissa (#STAT): invariata, tutti gli intervalli.
            if (s.period.n == 0 || interval_us < s.min_us) s.min_us = interval_us;
            if (s.period.n == 0 || interval_us > s.max_us) s.max_us = interval_us;
            Add_Sample(s.period, static_cast<double>(interval_us));
            Add_Sample(s.frequency, 1e6 / static_cast<double>(interval_us));

            // Finestra mobile (#FREQ): un'interruzione la svuota.
            if (interval_us > GAP_FACTOR * s.nominal_us) {
                s.sliding = sliding_window_t{};
            } else {
                Sliding_Add(s.sliding, interval_us);
            }
        }
    }
    s.previous_us  = timestamp_us;
    s.has_previous = true;
    return Sliding_Ready(s.sliding);
}

// Azzera i contatori della finestra fissa ma conserva l'ultimo timestamp:
// l'intervallo a cavallo tra due finestre viene contato nella seconda.
// La finestra mobile non viene toccata: scorre a cavallo delle finestre fisse.
void Start_New_Window(stream_stats_t &s)
{
    s.period    = running_stats_t{};
    s.frequency = running_stats_t{};
    s.min_us    = 0;
    s.max_us    = 0;
}

// --- TIMEOUT ------------------------------------------------------------------------------

enum outcome_t : uint8_t { OUT_OK, OUT_GRAY, OUT_LATE, OUT_MISSING, NUM_OUTCOMES };
const char *const OUTCOME_NAME[NUM_OUTCOMES] = {"OK", "GRIGIA", "TARDIVA", "MANCANTE"};

// Una richiesta lane 0 in attesa di risposta (una posizione per tag).
struct pending_request_t {
    bool     open;          // richiesta in attesa di risposta
    bool     missing;       // chiusa come MANCANTE: una risposta tardiva e' ancora riconoscibile
    uint32_t req_us;        // timestamp della richiesta
    bool     has_next;      // e' gia' arrivata la richiesta successiva dello stesso canale
    uint32_t next_req_us;   // timestamp della richiesta successiva
};

// Esiti delle richieste chiuse nella finestra corrente.
struct timeout_stats_t {
    uint32_t        outcome[NUM_OUTCOMES];
    running_stats_t latency;          // latenze delle risposte ricevute [us]
    uint32_t        max_latency_us;
};

// --- STATO --------------------------------------------------------------------------------

struct channel_state_t {
    const halo_channel_t  *channel;
    HALO::ListenerEndpoint endpoint;
    stream_stats_t stream[NUM_STREAMS];
    uint32_t total[NUM_STREAMS];   // eventi visti dall'avvio
    uint32_t errors;

    uint32_t          timeout_us;           // lane0_rx_timeout del canale (0 = controllo disattivato)
    pending_request_t pending[NUM_TAGS];
    bool              has_last_tag;
    uint8_t           last_tag;
    timeout_stats_t   tout_window;
    uint32_t          tout_total[NUM_OUTCOMES];
};

// Riepilogo degli ID esterni dall'avvio.
struct alien_totals_t {
    uint64_t           frames;               // frame esterni totali
    std::set<uint32_t> ids;                  // ID distinti registrati
    bool               untracked_seen;       // qualche ID non e' entrato in tabella
    uint32_t           windows;              // finestre analizzate
    uint32_t           windows_with_aliens;  // finestre con almeno un frame esterno
};

volatile std::sig_atomic_t g_stop = 0;
uint64_t g_start_us = 0;

// --- METRICHE DI FINE MISSIONE ------------------------------------------------------------

// Metrica 1: latenza massima di elaborazione per tipo di evento.
uint32_t g_max_latency_us[NUM_STREAMS]  = {0, 0, 0};
uint32_t g_latency_samples[NUM_STREAMS] = {0, 0, 0};

// Metrica 3: durata massima di una singola halo.Service(1).
uint32_t g_max_service_us = 0;

void On_Signal(int)
{
    g_stop = 1;
}

// --- TEMPO --------------------------------------------------------------------------------

// I timestamp dei frame sono a 32 bit (us) e vanno in overflow ogni ~71 minuti.
// Li riporto a 64 bit usando l'orologio corrente, poi li esprimo in secondi dall'avvio.
// Presuppone che Get_Micros() e i timestamp dell'HAL usino lo stesso orologio.
double Seconds_Since_Start(uint32_t timestamp_us)
{
    const uint64_t now_us = Get_Micros();
    // Differenza con segno: resta corretta anche se il timestamp e' di poco nel futuro.
    const int32_t age_us = static_cast<int32_t>(static_cast<uint32_t>(now_us) - timestamp_us);
    const int64_t full_us = static_cast<int64_t>(now_us) - age_us;
    return static_cast<double>(full_us - static_cast<int64_t>(g_start_us)) / 1e6;
}

// Metrica 1: tempo tra l'arrivo del frame sul bus (arrival_us) e adesso.
// Va chiamata appena l'evento e' stato letto, prima di qualsiasi stampa.
void Record_Latency(stream_t stream, uint32_t arrival_us)
{
    const int32_t elapsed = static_cast<int32_t>(static_cast<uint32_t>(Get_Micros()) - arrival_us);
    const uint32_t latency_us = (elapsed > 0) ? static_cast<uint32_t>(elapsed) : 0;
    ++g_latency_samples[stream];
    if (latency_us > g_max_latency_us[stream]) {
        g_max_latency_us[stream] = latency_us;
    }
}

// --- LOGICA DEI TIMEOUT -------------------------------------------------------------------

void Count_Outcome(channel_state_t &st, outcome_t outcome)
{
    ++st.tout_window.outcome[outcome];
    ++st.tout_total[outcome];
}

// Nuova richiesta: apre la posizione del suo tag e diventa la "richiesta successiva"
// di quella precedente, se ancora aperta.
void Timeout_On_Request(channel_state_t &st, uint8_t tag, uint32_t req_us)
{
    tag &= (NUM_TAGS - 1);

    if (st.has_last_tag) {
        pending_request_t &prev = st.pending[st.last_tag];
        if (prev.open && !prev.has_next) {
            prev.has_next    = true;
            prev.next_req_us = req_us;
        }
    }

    pending_request_t &slot = st.pending[tag];
    if (slot.open && st.timeout_us != 0) {
        Count_Outcome(st, OUT_MISSING);   // tag riusato prima che arrivasse la risposta
    }
    slot = pending_request_t{true, false, req_us, false, 0};
    st.last_tag     = tag;
    st.has_last_tag = true;
}

// Risposta con un certo tag: calcola latenza ed esito.
// Restituisce false se non esiste una richiesta con quel tag (richiesta non vista).
bool Timeout_On_Response(channel_state_t &st, uint8_t tag, uint32_t rsp_us,
                         uint32_t &latency_us, outcome_t &outcome)
{
    pending_request_t &slot = st.pending[tag & (NUM_TAGS - 1)];

    if (!slot.open && !slot.missing) {
        return false;
    }

    const int32_t signed_latency = static_cast<int32_t>(rsp_us - slot.req_us);
    latency_us = (signed_latency > 0) ? static_cast<uint32_t>(signed_latency) : 0;

    if (!slot.open) {
        // Risposta a una richiesta gia' chiusa come MANCANTE: e' tardiva, ma la richiesta
        // e' gia' stata contata, quindi non la conto di nuovo.
        slot.missing = false;
        outcome = OUT_LATE;
        return true;
    }
    slot.open = false;

    outcome = OUT_OK;
    if (st.timeout_us != 0) {
        const uint32_t t        = st.timeout_us;
        const uint32_t gray_low = (t > TIMEOUT_DELTA_US) ? t - TIMEOUT_DELTA_US : 0;
        const bool after_next   = slot.has_next &&
                                  static_cast<int32_t>(rsp_us - slot.next_req_us) > 0;
        if (after_next || latency_us > t + TIMEOUT_DELTA_US) {
            outcome = OUT_LATE;
        } else if (latency_us > gray_low) {
            outcome = OUT_GRAY;
        }
    }

    Count_Outcome(st, outcome);
    Add_Sample(st.tout_window.latency, static_cast<double>(latency_us));
    if (latency_us > st.tout_window.max_latency_us) {
        st.tout_window.max_latency_us = latency_us;
    }
    return true;
}

// Chiude come MANCANTI le richieste senza risposta entro T + D (piu' il margine del listener).
void Timeout_Check_Missing(channel_state_t &st, uint32_t now_us)
{
    if (st.timeout_us == 0) {
        return;
    }
    const int32_t limit_us = static_cast<int32_t>(st.timeout_us + TIMEOUT_DELTA_US + LISTENER_MARGIN_US);
    for (pending_request_t &slot : st.pending) {
        if (slot.open && static_cast<int32_t>(now_us - slot.req_us) > limit_us) {
            slot.open    = false;
            slot.missing = true;
            Count_Outcome(st, OUT_MISSING);
        }
    }
}

// --- LOG ----------------------------------------------------------------------------------

void Window_Text(char *out, size_t size, double start_s, double end_s)
{
    std::snprintf(out, size, "%.1f-%.1f", start_s, end_s);
}

void Log_Header(const channel_state_t *states, double window_s)
{
    std::printf("# HALO listener ZCU104 - finestra di analisi: %.1f s\n", window_s);
    for (size_t i = 0; i < NUM_CHANNELS; ++i) {
        const halo_channel_t *ch = states[i].channel;
        std::printf("# %s (global id 0x%04X): controller %s (0x%02X) <-> responder %s (0x%02X), "
                    "lane0_rx_timeout %lu us\n",
                    ch->name, static_cast<unsigned>(ch->global_id),
                    ch->controller->name, static_cast<unsigned>(ch->controller->id),
                    ch->responder->name, static_cast<unsigned>(ch->responder->id),
                    static_cast<unsigned long>(states[i].timeout_us));
    }
    std::printf("# REQ = richiesta lane 0 (C->R), RSP = risposta lane 0 (R->C), "
                "MSG = messaggio lane 1 (C->R)\n");
    std::printf("# esito RSP: OK <= T-%lu us < GRIGIA <= T+%lu us < TARDIVA (T = lane0_rx_timeout, "
                "T = 0: sempre OK)\n",
                static_cast<unsigned long>(TIMEOUT_DELTA_US),
                static_cast<unsigned long>(TIMEOUT_DELTA_US));
    std::printf("# frequenza mobile: ultimi %u intervalli per flusso, una riga ogni %u eventi, "
                "periodo nominale REQ/RSP %lu us, MSG %lu us, interruzione > %lu x nominale\n",
                static_cast<unsigned>(FREQ_SAMPLES), static_cast<unsigned>(FREQ_HOP),
                static_cast<unsigned long>(REQUEST_PERIOD_US),
                static_cast<unsigned long>(LANE1_PERIOD_US),
                static_cast<unsigned long>(GAP_FACTOR));
    std::printf("#\n");
    std::printf("#FREQ  %12s  %-10s  %-4s  %3s  %10s  %11s  %9s  %9s  %9s\n",
                "time[s]", "channel", "type", "n", "freq[Hz]",
                "T_mean[us]", "T_sd[us]", "T_min[us]", "T_max[us]");
    std::printf("#STAT  %17s  %-10s  %-4s  %5s  %10s  %11s  %9s  %9s  %9s  %9s\n",
                "window[s]", "channel", "type", "n", "freq[Hz]",
                "T_mean[us]", "T_sd[us]", "T_min[us]", "T_max[us]", "f_sd[Hz]");
    std::printf("#TOUT  %17s  %-10s  %11s  %5s  %6s  %7s  %8s  %12s  %11s\n",
                "window[s]", "channel", "timeout[us]", "ok", "grigia", "tardive", "mancanti",
                "lat_mean[us]", "lat_max[us]");
    std::printf("#ALIEN %17s  %s\n", "window[s]", "ids=N  0xID:frame ...");
    std::printf("#%11s  %-10s  %-4s  %-4s  %-4s  %-4s  %-3s  %9s  %-8s  %s\n",
                "time[s]", "channel", "src", "dst", "lane", "type", "tag",
                "lat[us]", "esito", "payload");
}

// tag < 0 = non significativo; latency_us < 0 = non applicabile;
// outcome / payload nullptr = non applicabile.
void Log_Event(double time_s, const halo_channel_t *channel, uint8_t src, uint8_t dst,
               uint8_t lane, const char *type, int tag, long latency_us,
               const char *outcome, const char *payload)
{
    char tag_text[12] = "-";
    if (tag >= 0) {
        std::snprintf(tag_text, sizeof(tag_text), "%d", tag);
    }
    char lat_text[24] = "-";
    if (latency_us >= 0) {
        std::snprintf(lat_text, sizeof(lat_text), "%ld", latency_us);
    }
    std::printf("%12.6f  %-10s  0x%02X  0x%02X  L%-3u  %-4s  %-3s  %9s  %-8s  %s\n",
                time_s, channel->name, static_cast<unsigned>(src), static_cast<unsigned>(dst),
                static_cast<unsigned>(lane), type, tag_text, lat_text,
                outcome != nullptr ? outcome : "-",
                payload != nullptr ? payload : "-");
}

// Una riga #FREQ per il flusso: statistiche sugli ultimi FREQ_SAMPLES intervalli.
// Chiamata solo quando Record_Event ha restituito true (finestra piena).
void Log_Freq(channel_state_t &state, stream_t stream, uint32_t timestamp_us)
{
    sliding_window_t &w = state.stream[stream].sliding;

    const double mean_us = static_cast<double>(w.sum_us) / w.n;
    uint32_t min_us = w.interval_us[0];
    uint32_t max_us = w.interval_us[0];
    double   acc    = 0.0;
    for (uint8_t i = 0; i < w.n; ++i) {
        const uint32_t x = w.interval_us[i];
        if (x < min_us) min_us = x;
        if (x > max_us) max_us = x;
        const double d = static_cast<double>(x) - mean_us;
        acc += d * d;
    }
    const double sd_us = std::sqrt(acc / (w.n - 1));   // n == FREQ_SAMPLES >= 2

    std::printf("#FREQ  %12.6f  %-10s  %-4s  %3u  %10.4f  %11.1f  %9.1f  %9lu  %9lu\n",
                Seconds_Since_Start(timestamp_us), state.channel->name, STREAM_NAME[stream],
                static_cast<unsigned>(w.n), 1e6 / mean_us, mean_us, sd_us,
                static_cast<unsigned long>(min_us), static_cast<unsigned long>(max_us));
    w.new_samples = 0;
}

// Una riga #STAT per ogni flusso, poi azzera i contatori per la finestra successiva.
void Log_Window_Stats(channel_state_t *states, const char *window_text)
{
    for (size_t i = 0; i < NUM_CHANNELS; ++i) {
        for (uint8_t k = 0; k < NUM_STREAMS; ++k) {
            stream_stats_t &s = states[i].stream[k];
            const uint32_t n = s.period.n;

            std::printf("#STAT  %17s  %-10s  %-4s  %5lu",
                        window_text, states[i].channel->name,
                        STREAM_NAME[k], static_cast<unsigned long>(n));
            if (n == 0) {
                std::printf("  %10s  %11s  %9s  %9s  %9s  %9s\n", "-", "-", "-", "-", "-", "-");
            } else if (n == 1) {
                std::printf("  %10.4f  %11.1f  %9s  %9lu  %9lu  %9s\n",
                            1e6 / s.period.mean, s.period.mean, "-",
                            static_cast<unsigned long>(s.min_us),
                            static_cast<unsigned long>(s.max_us), "-");
            } else {
                std::printf("  %10.4f  %11.1f  %9.1f  %9lu  %9lu  %9.4f\n",
                            1e6 / s.period.mean, s.period.mean, Std_Dev(s.period),
                            static_cast<unsigned long>(s.min_us),
                            static_cast<unsigned long>(s.max_us), Std_Dev(s.frequency));
            }
            Start_New_Window(s);
        }
    }
}

// Una riga #TOUT per canale, poi azzera i contatori della finestra.
void Log_Window_Timeouts(channel_state_t *states, const char *window_text)
{
    for (size_t i = 0; i < NUM_CHANNELS; ++i) {
        timeout_stats_t &t = states[i].tout_window;

        char timeout_text[16] = "off";
        if (states[i].timeout_us != 0) {
            std::snprintf(timeout_text, sizeof(timeout_text), "%lu",
                          static_cast<unsigned long>(states[i].timeout_us));
        }
        std::printf("#TOUT  %17s  %-10s  %11s  %5lu  %6lu  %7lu  %8lu",
                    window_text, states[i].channel->name, timeout_text,
                    static_cast<unsigned long>(t.outcome[OUT_OK]),
                    static_cast<unsigned long>(t.outcome[OUT_GRAY]),
                    static_cast<unsigned long>(t.outcome[OUT_LATE]),
                    static_cast<unsigned long>(t.outcome[OUT_MISSING]));
        if (t.latency.n == 0) {
            std::printf("  %12s  %11s\n", "-", "-");
        } else {
            std::printf("  %12.1f  %11lu\n", t.latency.mean,
                        static_cast<unsigned long>(t.max_latency_us));
        }
        t = timeout_stats_t{};
    }
}

// Una riga #ALIEN con gli ID esterni della finestra (la tabella viene svuotata dalla lettura).
void Log_Window_Aliens(HALO::Interface &halo, const char *window_text, alien_totals_t &totals)
{
    pal_unknown_id_t ids[PAL_UNKNOWN_ID_MAX];
    uint8_t  count     = 0;
    uint32_t untracked = 0;
    const return_code_t rc = halo.TakeUnknownIds(ids, PAL_UNKNOWN_ID_MAX, &count, &untracked);

    ++totals.windows;
    std::printf("#ALIEN %17s", window_text);
    if (rc != RC_OK) {
        std::printf("  n/d (code %d)\n", static_cast<int>(rc));
        return;
    }

    std::printf("  ids=%u", static_cast<unsigned>(count));
    uint64_t window_frames = untracked;
    for (uint8_t i = 0; i < count; ++i) {
        std::printf("  0x%08lX:%lu", static_cast<unsigned long>(ids[i].can_id),
                    static_cast<unsigned long>(ids[i].frames));
        window_frames += ids[i].frames;
        totals.ids.insert(ids[i].can_id);
    }
    if (untracked > 0) {
        std::printf("  +%lu frame da ID non registrati", static_cast<unsigned long>(untracked));
        totals.untracked_seen = true;
    }
    std::printf("\n");

    totals.frames += window_frames;
    if (window_frames > 0) {
        ++totals.windows_with_aliens;
    }
}

void Log_Window(HALO::Interface &halo, channel_state_t *states, alien_totals_t &aliens,
                double window_start_s, double window_end_s)
{
    char window_text[32];
    Window_Text(window_text, sizeof(window_text), window_start_s, window_end_s);
    Log_Window_Stats(states, window_text);
    Log_Window_Timeouts(states, window_text);
    Log_Window_Aliens(halo, window_text, aliens);
}

// Copia il testo del messaggio in una stringa stampabile (i byte non stampabili diventano '.').
void Payload_To_Text(const test_msg_t &message, char *out, size_t out_size)
{
    size_t i = 0;
    while (i < sizeof(message.text) && i + 1 < out_size && message.text[i] != '\0') {
        const unsigned char c = static_cast<unsigned char>(message.text[i]);
        out[i] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.';
        ++i;
    }
    out[i] = '\0';
}

void Log_Error(channel_state_t &state, const char *what, return_code_t rc)
{
    ++state.errors;
    std::fprintf(stderr, "[ERR] %s: %s (code %d)\n",
                 state.channel->name, what, static_cast<int>(rc));
}

// --- LETTURA DEGLI EVENTI -----------------------------------------------------------------

// Richieste su lane 0: solo timestamp e tag (il payload non e' esposto dalla API listener).
void Read_Requests(channel_state_t &state)
{
    const uint8_t ctrl_id = static_cast<uint8_t>(state.channel->controller->id);
    const uint8_t resp_id = static_cast<uint8_t>(state.channel->responder->id);

    for (;;) {
        uint32_t timestamp_us = 0;
        uint8_t  tag = 0;
        const return_code_t rc = state.endpoint.ReceiveRequestWithTag(&timestamp_us, &tag);
        if (rc == RC_OK_NO_DATA) {
            return;
        }
        if (rc != RC_OK) {
            Log_Error(state, "lettura richiesta fallita", rc);
            return;
        }
        Record_Latency(STREAM_REQ, timestamp_us);
        ++state.total[STREAM_REQ];
        const bool freq_ready = Record_Event(state.stream[STREAM_REQ], timestamp_us);
        Timeout_On_Request(state, tag, timestamp_us);
        Log_Event(Seconds_Since_Start(timestamp_us), state.channel, ctrl_id, resp_id,
                  LANE0, "REQ", tag, -1, nullptr, nullptr);
        if (freq_ready) {
            Log_Freq(state, STREAM_REQ, timestamp_us);
        }
    }
}

// Messaggio completo su una lane: risposta (lane 0) oppure messaggio del controller (lane 1).
// Il Transport tiene un solo messaggio per lane, quindi al piu' uno per chiamata.
void Read_Message(channel_state_t &state, uint8_t lane)
{
    const return_code_t available = state.endpoint.IsMessageAvailable(lane);
    if (available == RC_OK_FALSE) {
        return;
    }
    if (available != RC_OK_TRUE) {
        Log_Error(state, "controllo disponibilita' fallito", available);
        return;
    }

    // Payload, timestamp dell'ultimo frame e tag arrivano insieme dal Transport:
    // appartengono sempre allo stesso messaggio.
    test_msg_t message = TEST_MSG_INIT;   // va re-inizializzato: il descriptor e' nel primo campo
    uint32_t   timestamp_us = 0;
    uint8_t    tag = 0;
    const return_code_t rc = state.endpoint.Receive(lane, &message, &timestamp_us, &tag);
    if (rc == RC_OK_NO_DATA) {
        return;
    }
    if (rc != RC_OK) {
        Log_Error(state, "lettura messaggio fallita", rc);
        return;
    }

    const uint8_t ctrl_id = static_cast<uint8_t>(state.channel->controller->id);
    const uint8_t resp_id = static_cast<uint8_t>(state.channel->responder->id);

    const stream_t stream = (lane == LANE0) ? STREAM_RSP : STREAM_MSG;
    Record_Latency(stream, timestamp_us);
    ++state.total[stream];
    const bool freq_ready = Record_Event(state.stream[stream], timestamp_us);

    char payload[sizeof(message.text) + 1];
    Payload_To_Text(message, payload, sizeof(payload));

    if (lane == LANE0) {
        // Risposta: responder -> controller, con latenza ed esito del timeout.
        uint32_t  latency_us = 0;
        outcome_t outcome    = OUT_OK;
        const bool matched   = Timeout_On_Response(state, tag, timestamp_us, latency_us, outcome);
        Log_Event(Seconds_Since_Start(timestamp_us), state.channel, resp_id, ctrl_id,
                  lane, STREAM_NAME[stream], tag,
                  matched ? static_cast<long>(latency_us) : -1,
                  matched ? OUTCOME_NAME[outcome] : "?", payload);
    } else {
        // Messaggio lane 1: controller -> responder. Il tag non e' significativo.
        Log_Event(Seconds_Since_Start(timestamp_us), state.channel, ctrl_id, resp_id,
                  lane, STREAM_NAME[stream], -1, -1, nullptr, payload);
    }

    if (freq_ready) {
        Log_Freq(state, stream, timestamp_us);
    }
}

void Print_Summary(const channel_state_t *states, const alien_totals_t &aliens)
{
    std::fprintf(stderr, "\n%-10s  %8s  %8s  %8s  %8s\n", "channel", "REQ", "RSP", "MSG", "errors");
    for (size_t i = 0; i < NUM_CHANNELS; ++i) {
        std::fprintf(stderr, "%-10s  %8lu  %8lu  %8lu  %8lu\n",
                     states[i].channel->name,
                     static_cast<unsigned long>(states[i].total[STREAM_REQ]),
                     static_cast<unsigned long>(states[i].total[STREAM_RSP]),
                     static_cast<unsigned long>(states[i].total[STREAM_MSG]),
                     static_cast<unsigned long>(states[i].errors));
    }

    std::fprintf(stderr, "\n%-10s  %11s  %8s  %8s  %8s  %8s\n",
                 "channel", "timeout[us]", "OK", "GRIGIA", "TARDIVE", "MANCANTI");
    for (size_t i = 0; i < NUM_CHANNELS; ++i) {
        char timeout_text[16] = "off";
        if (states[i].timeout_us != 0) {
            std::snprintf(timeout_text, sizeof(timeout_text), "%lu",
                          static_cast<unsigned long>(states[i].timeout_us));
        }
        std::fprintf(stderr, "%-10s  %11s  %8lu  %8lu  %8lu  %8lu\n",
                     states[i].channel->name, timeout_text,
                     static_cast<unsigned long>(states[i].tout_total[OUT_OK]),
                     static_cast<unsigned long>(states[i].tout_total[OUT_GRAY]),
                     static_cast<unsigned long>(states[i].tout_total[OUT_LATE]),
                     static_cast<unsigned long>(states[i].tout_total[OUT_MISSING]));
    }

    std::fprintf(stderr, "\nLatenza max di elaborazione [us]: ");
    for (uint8_t k = 0; k < NUM_STREAMS; ++k) {
        if (g_latency_samples[k] == 0) {
            std::fprintf(stderr, " %s -", STREAM_NAME[k]);
        } else {
            std::fprintf(stderr, " %s %lu", STREAM_NAME[k],
                         static_cast<unsigned long>(g_max_latency_us[k]));
        }
        std::fprintf(stderr, k + 1 < NUM_STREAMS ? " " : "\n");
    }
    std::fprintf(stderr, "Tempo max di Service(1) [us]: %lu\n",
                 static_cast<unsigned long>(g_max_service_us));

    std::fprintf(stderr, "\nIntrusioni: %llu frame esterni, %s%lu ID distinti, presenti in %lu finestre su %lu\n",
                 static_cast<unsigned long long>(aliens.frames),
                 aliens.untracked_seen ? "almeno " : "",
                 static_cast<unsigned long>(aliens.ids.size()),
                 static_cast<unsigned long>(aliens.windows_with_aliens),
                 static_cast<unsigned long>(aliens.windows));
}

} // namespace

int main(int argc, char **argv)
{
    double window_s = DEFAULT_WINDOW_S;
    if (argc > 1) {
        window_s = std::strtod(argv[1], nullptr);
        if (!(window_s > 0.0)) {
            std::fprintf(stderr, "Uso: %s [durata_finestra_in_secondi]\n", argv[0]);
            return 1;
        }
    }
    const uint64_t window_us = static_cast<uint64_t>(window_s * 1e6);

    std::setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);   // una scrittura per riga, anche su file
    std::signal(SIGINT, On_Signal);
    std::signal(SIGTERM, On_Signal);

    HALO::Interface &halo = HALO::Interface::Instance();
    const return_code_t init_rc = halo.Initialize(&LISTENER_NODE);
    if (init_rc != RC_OK) {
        std::fprintf(stderr, "Inizializzazione HALO fallita (code %d). can0 e' attiva?\n",
                     static_cast<int>(init_rc));
        return 1;
    }

    channel_state_t states[NUM_CHANNELS] = {};
    for (size_t i = 0; i < NUM_CHANNELS; ++i) {
        states[i].channel    = CHANNELS[i];
        states[i].timeout_us = static_cast<uint32_t>(CHANNELS[i]->lane0_rx_timeout);
        states[i].endpoint   = halo.BindListenerEndpoint(CHANNELS[i]);
        if (!states[i].endpoint.IsBound()) {
            std::fprintf(stderr, "Bind fallito sul canale %s: il nodo listener non deve essere "
                                 "ne' controller ne' responder del canale.\n", CHANNELS[i]->name);
            return 1;
        }
        // Periodi nominali per la finestra mobile (vedi REQUEST_PERIOD_US / LANE1_PERIOD_US).
        states[i].stream[STREAM_REQ].nominal_us = REQUEST_PERIOD_US;
        states[i].stream[STREAM_RSP].nominal_us = REQUEST_PERIOD_US;   // una risposta per richiesta
        states[i].stream[STREAM_MSG].nominal_us = LANE1_PERIOD_US;
    }
    alien_totals_t aliens = {};

    g_start_us = Get_Micros();
    uint64_t window_start_us = g_start_us;
    Log_Header(states, window_s);

    // Busy-poll: nessuna pausa, il thread occupa un core al 100%.
    while (!g_stop) {
        // Metrica 3: durata di una singola Service(1).
        const uint64_t service_start_us = Get_Micros();
        halo.Service(1);
        const uint64_t service_us = Get_Micros() - service_start_us;
        if (service_us > g_max_service_us) {
            g_max_service_us = static_cast<uint32_t>(service_us);
        }

        // Dopo OGNI passo di Service() leggo gli eventi: cosi' un messaggio completato
        // viene prelevato prima che il Transport ne completi un altro sulla stessa lane.
        for (size_t i = 0; i < NUM_CHANNELS; ++i) {
            Read_Requests(states[i]);
            Read_Message(states[i], LANE0);
            Read_Message(states[i], LANE1);
        }

        // Richieste rimaste senza risposta oltre T + D.
        const uint32_t now32 = static_cast<uint32_t>(Get_Micros());
        for (size_t i = 0; i < NUM_CHANNELS; ++i) {
            Timeout_Check_Missing(states[i], now32);
        }

        // Fine finestra: stampo le statistiche e ne apro una nuova.
        if (Get_Micros() - window_start_us >= window_us) {
            const double start_s = static_cast<double>(window_start_us - g_start_us) / 1e6;
            Log_Window(halo, states, aliens, start_s, start_s + window_s);
            window_start_us += window_us;
        }
    }

    // Ultima finestra, incompleta: la stampo con la sua durata reale.
    const uint64_t now_us = Get_Micros();
    for (size_t i = 0; i < NUM_CHANNELS; ++i) {
        Timeout_Check_Missing(states[i], static_cast<uint32_t>(now_us));
    }
    Log_Window(halo, states, aliens,
               static_cast<double>(window_start_us - g_start_us) / 1e6,
               static_cast<double>(now_us - g_start_us) / 1e6);

    std::fflush(stdout);
    Print_Summary(states, aliens);
    return 0;
}
