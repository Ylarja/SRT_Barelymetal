
#include "/Users/domenicomennillo/Desktop/Esami Da Dare/Real Time/Progetto/adafruit/src/halo_listener/HALO/Protocol/Include/HALO.h"

// --- Configurazione della board ------------------------------------------------
// Stesso sketch per i due responder: cambiare solo queste tre righe.
//   responder1: HALO_NODE_RESPONDER1, HALO_CHANNEL_CHANNEL1, '#'
//   responder2: HALO_NODE_RESPONDER2, HALO_CHANNEL_CHANNEL2, '*'
#define RESPONDER_NODE     HALO_NODE_RESPONDER1
#define RESPONDER_CHANNEL  HALO_CHANNEL_CHANNEL1
#define PAYLOAD_CHAR       '#'

// Ogni quanto cambia il valore della risposta (simula un sensore che si aggiorna).
#define UPDATE_PERIOD_MS   100

// Ogni quanto stampare i totali. Nessuna stampa per singolo messaggio: rallenterebbe
// il loop e il responder rischierebbe di perdere frame, falsando il riferimento.
#define PRINT_PERIOD_MS    2000

HALO::Interface &Halo = HALO::Interface::Instance();
HALO::ResponderEndpoint rrs;

test_msg_t test_msg_1 = TEST_MSG_INIT;   // risposta lane 0
test_msg_t test_msg_2 = TEST_MSG_INIT;   // messaggio ricevuto su lane 1

unsigned long last_update = 0;
unsigned long last_print  = 0;

uint32_t responses_sent = 0;             // riferimento per la loss rate delle RSP
uint32_t lane1_received = 0;             // controllo incrociato con i Send del controller

void generate_msg(test_msg_t* msg){
  static char inv = '0';
  for(int i = 0; i<20; i++){
    msg->text[i] = PAYLOAD_CHAR;
  }
  inv = (inv == '9') ? '0' : inv + 1;
  msg->text[20] = inv;
}

void setup() {

  Serial.begin(9600);

  Halo.Initialize(&RESPONDER_NODE);
  rrs = Halo.BindResponderEndpoint(&RESPONDER_CHANNEL);

  generate_msg(&test_msg_1);
  last_update = millis();
  last_print  = millis();
}

void loop() {

  // 1) Service: e' qui che una risposta finisce di essere trasmessa.
  Halo.Service(1);

  // 2) Subito dopo Service e PRIMA di Respond: Respond() azzera il flag
  //    "risposta inviata" quando il responder e' fermo in attesa.
  if (rrs.IsResponseDone() == RC_OK_TRUE) {
    responses_sent++;
  }
  // Da capire se vale la pena tenerlo con periodo fisso o meno
  // 3) Aggiorna il valore della risposta a periodo fisso.
  if (millis() - last_update >= UPDATE_PERIOD_MS) {
    generate_msg(&test_msg_1);
    last_update = millis();
  }

  // 4) Respond() va chiamata a ogni giro: dopo una risposta il responder resta fermo
  //    finche' non viene chiamata di nuovo.
  rrs.Respond(&test_msg_1);

  // 5) Messaggi lane 1 del controller: solo conteggio.
  if (rrs.Receive(&test_msg_2) == RC_OK) {
    lane1_received++;
  }

  // 6) Totali a intervalli: l'ultimo valore stabile e' quello finale della missione.
  if (millis() - last_print >= PRINT_PERIOD_MS) {
    last_print = millis();
    Serial.print("RSP inviate: ");
    Serial.print(responses_sent);
    Serial.print("  L1 ricevuti: ");
    Serial.println(lane1_received);
  }
}
