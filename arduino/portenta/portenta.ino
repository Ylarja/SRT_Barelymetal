
#include "/Users/domenicomennillo/Desktop/Esami Da Dare/Real Time/Progetto/portenta/src/halo_listener/HALO/Protocol/Include/HALO.h"

// --- Parametri dell'esperimento ------------------------------------------------
// Periodi in microsecondi (micros()): servono per le prove sotto i 10 ms.
#define REQUEST_PERIOD_US    100000UL      // richiesta lane 0 su entrambi i canali
#define LANE1_PERIOD_US      100000UL      // messaggio lane 1 su entrambi i canali
#define MISSION_DURATION_MS  180000UL      // durata della missione: 3 minuti
#define DRAIN_MS             500UL         // attesa dopo la fine, per chiudere il round in corso

HALO::Interface &Halo = HALO::Interface::Instance();

HALO::ControllerEndpoint ctr1;
HALO::ControllerEndpoint ctr2;

test_msg_t test_msg_1 = TEST_MSG_INIT;   // risposta canale 1
test_msg_t test_msg_2 = TEST_MSG_INIT;   // risposta canale 2
test_msg_t test_msg_3 = TEST_MSG_INIT;   // messaggio lane 1 (stesso contenuto sui due canali)

unsigned long mission_start_ms = 0;
unsigned long next_req_us  = 0;
unsigned long next_send_us = 0;
bool mission_running = true;
bool totals_printed  = false;

uint32_t lane1_sent[2] = {0, 0};          // riferimento per la loss rate delle MSG, per canale

void generate_msg(test_msg_t* msg){
  static char inv = '0';
  for(int i = 0; i<20; i++){
    msg->text[i] = '%';
  }
  inv = (inv == '9') ? '0' : inv + 1;
  msg->text[20] = inv;
}

// true se e' arrivato il momento `deadline`; poi la sposta di un periodo.
// Se il loop e' rimasto indietro di piu' di un periodo, riparte da adesso
// invece di recuperare con una raffica di invii.
bool Is_Due(unsigned long &deadline, unsigned long period, unsigned long now){
  if ((long)(now - deadline) < 0) {
    return false;
  }
  deadline += period;
  if ((long)(now - deadline) >= 0) {
    deadline = now + period;
  }
  return true;
}

void setup() {

  Serial.begin(9600);

  Halo.Initialize(&HALO_NODE_CONTROLLER);
  ctr1 = Halo.BindControllerEndpoint(&HALO_CHANNEL_CHANNEL1);
  ctr2 = Halo.BindControllerEndpoint(&HALO_CHANNEL_CHANNEL2);

  // Lane 1 sfasata di mezzo periodo rispetto alle richieste: quando parte il messaggio
  // lane 1 il round di richieste e' gia' concluso, e le due cose non si bloccano a vicenda.
  unsigned long now = micros();
  next_req_us  = now;
  next_send_us = now + LANE1_PERIOD_US / 2;
  mission_start_ms = millis();

  Serial.println("Missione avviata");
}

void loop() {

  // Service() gestisce anche la sincronizzazione del tempo: nessuna Sync() manuale.
  Halo.Service(1);

  if (mission_running) {
    unsigned long now = micros();

    // Richieste lane 0 su entrambi i canali (non contate: il riferimento e' il responder).
    if (Is_Due(next_req_us, REQUEST_PERIOD_US, now)) {
      ctr1.Request();
      ctr2.Request();
    }

    // Messaggio lane 1 su entrambi i canali. Conta solo i Send accettati:
    // con RC_BUSY l'invio precedente e' ancora in corso e questo messaggio non parte.
    if (Is_Due(next_send_us, LANE1_PERIOD_US, now)) {
      generate_msg(&test_msg_3);
      if (ctr1.Send(&test_msg_3) == RC_OK) lane1_sent[0]++;
      if (ctr2.Send(&test_msg_3) == RC_OK) lane1_sent[1]++;
    }

    if (millis() - mission_start_ms >= MISSION_DURATION_MS) {
      mission_running = false;
      Serial.println("Missione terminata");
    }
  }

  // Receive() va chiamata a ogni giro anche senza stampare: e' lei che chiude la risposta
  // e permette alla richiesta successiva di partire.
  ctr1.Receive(&test_msg_1);
  ctr2.Receive(&test_msg_2);

  // Totali, una sola volta, poco dopo la fine della missione.
  if (!mission_running && !totals_printed &&
      millis() - mission_start_ms >= MISSION_DURATION_MS + DRAIN_MS) {
    totals_printed = true;
    Serial.print("L1 inviati  CH1: ");
    Serial.print(lane1_sent[0]);
    Serial.print("  CH2: ");
    Serial.println(lane1_sent[1]);
  }
}
