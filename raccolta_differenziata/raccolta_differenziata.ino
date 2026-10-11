/*
 * Promemoria Raccolta Differenziata - ESP8266 (LOLIN / WeMos D1 R2, formato Arduino Uno)
 *
 * Cosa fa:
 *  - Si sveglia, si collega al Wi-Fi e legge l'ora esatta da internet (NTP, ora italiana con ora legale).
 *  - Nelle fasce orarie impostate (default 13:00-14:00 e 19:00-22:00) accende il LED RGB
 *    con il colore del rifiuto da esporre per la raccolta di DOMANI. Se domani ci sono due rifiuti (es. Vetro + Indumenti)
 *    i colori si alternano.
 *  - Fuori dalle fasce orarie (o se domani non c'e' raccolta) va in Deep Sleep.
 *
 * Collegamenti (vedi README.md):
 *  - LED Rosso -> D5 (GPIO14), Verde -> D6 (GPIO12), Blu -> D7 (GPIO13)
 *  - Pin comune del LED -> GND (catodo comune) oppure 3V3 (anodo comune, NON 5V)
 *  - Ponticello per il risveglio: RST <-> GPIO16.
 *    ATTENZIONE: sulla D1 R2 formato Uno GPIO16 e' il pin serigrafato "D2",
 *    NON il pin "RX<-D0" (quello e' GPIO3 = RX seriale e il risveglio non funziona).
 *
 * Scheda da selezionare nell'IDE Arduino: "LOLIN(WEMOS) D1 R2 & mini" (o "WeMos D1 R1").
 * I pin sono indicati con il numero GPIO, quindi funzionano con entrambe le scelte.
 */

#include <ESP8266WiFi.h>
#include <time.h>

// ============================================================================
//  CONFIGURAZIONE - modifica qui
// ============================================================================

const char* WIFI_SSID     = "IL_TUO_WIFI";
const char* WIFI_PASSWORD = "LA_TUA_PASSWORD";

// Fasce orarie in cui il LED e' acceso (ora locale italiana).
// Formato: { ora inizio, minuto inizio, ora fine, minuto fine }. Una riga per fascia:
// per aggiungerne una basta aggiungere una riga, per toglierla basta cancellarla.
struct Fascia { uint8_t oraInizio, minInizio, oraFine, minFine; };
const Fascia FASCE[] = {
  { 13, 0, 14, 0 },   // 13:00 - 14:00
  { 19, 0, 22, 0 },   // 19:00 - 22:00 (metti 23 per arrivare alle 23:00)
};
const uint8_t NUM_FASCE = sizeof(FASCE) / sizeof(FASCE[0]);

// Tipo di LED: true = anodo comune (pin lungo a 3V3), false = catodo comune (pin lungo a GND)
const bool ANODO_COMUNE = false;

// Luminosita' massima (0-255). Valori alti senza resistenze sul LED possono bloccare
// la scheda (soprattutto col bianco, che accende tutti e tre i colori insieme).
const uint8_t LUMINOSITA = 60;

// Pin del LED (numeri GPIO): D5 = 14, D6 = 12, D7 = 13
const uint8_t PIN_ROSSO = 14;
const uint8_t PIN_VERDE = 12;
const uint8_t PIN_BLU   = 13;

// Se manca Wi-Fi o NTP riprova dopo questi secondi
const uint32_t RIPROVA_SECONDI = 10 * 60;

// Il timer del deep sleep e' impreciso (alcuni %): ci si sveglia qualche minuto prima
// e si aspetta da svegli (con Wi-Fi spento) l'inizio della fascia.
const uint32_t ANTICIPO_RISVEGLIO_SECONDI = 5 * 60;

// Fuso orario Italia: CET/CEST con cambio ora automatico
#define FUSO_ORARIO "CET-1CEST,M3.5.0,M10.5.0/3"

// ============================================================================
//  TIPI DI RIFIUTO E COLORI
// ============================================================================

enum Rifiuto : uint8_t {
  NESSUNO         = 0,
  ORGANICO        = 1 << 0,
  PLASTICA        = 1 << 1,   // plastica e metalli
  CARTA           = 1 << 2,
  VETRO           = 1 << 3,
  INDIFFERENZIATO = 1 << 4,   // secco residuo
  INDUMENTI       = 1 << 5,
  VERDE           = 1 << 6,   // sfalci / potature
};

struct Colore { uint8_t r, g, b; const char* nome; };

// Un colore per ogni bit sopra (stesso ordine). Il ROSSO e' riservato agli errori.
const Colore COLORI[] = {
  {255,  40,   0, "Organico"},          // arancione/marrone
  {255, 150,   0, "Plastica"},          // giallo
  {  0,   0, 255, "Carta"},             // blu
  {  0, 255,   0, "Vetro"},             // verde
  {255, 255, 255, "Indifferenziato"},   // bianco
  {160,   0, 255, "Indumenti"},         // viola
  {  0, 255, 120, "Verde/Sfalci"},      // verde acqua
};
const uint8_t NUM_TIPI = sizeof(COLORI) / sizeof(COLORI[0]);

// ============================================================================
//  CALENDARIO
// ============================================================================
//
// 1) SETTIMANA: cosa si RACCOGLIE in ciascun giorno della settimana
//    (il LED si accende la SERA PRIMA). Indice 0 = domenica ... 6 = sabato.
//    Per piu' rifiuti nello stesso giorno usa |  es.  VETRO | INDUMENTI
//
//    Il calendario del Comune non segue uno schema settimanale fisso (il giovedi'
//    alterna Indifferenziato e Vetro, i festivi spostano le raccolte), quindi qui e'
//    tutto NESSUNO e le date sono elencate una per una in ECCEZIONI qui sotto.
const uint8_t SETTIMANA[7] = {
  /* domenica  */ NESSUNO,
  /* lunedi    */ NESSUNO,
  /* martedi   */ NESSUNO,
  /* mercoledi */ NESSUNO,
  /* giovedi   */ NESSUNO,
  /* venerdi   */ NESSUNO,
  /* sabato    */ NESSUNO,
};

// 2) ECCEZIONI: date precise che SOSTITUISCONO il giorno della settimana
//    (festivi spostati, Vetro + Indumenti, raccolte saltate con NESSUNO, ...).
//    Formato: { anno, mese, giorno, rifiuti }  -> data della RACCOLTA.
//    Se preferisci puoi mettere TUTTO il calendario qui (Luglio-Dicembre 2026)
//    e lasciare SETTIMANA tutta a NESSUNO.
//    L'ultima riga {0, 0, 0, 0} deve restare: segna la fine della lista.
struct Eccezione { uint16_t anno; uint8_t mese; uint8_t giorno; uint8_t rifiuti; };

const Eccezione ECCEZIONI[] = {
  // Calendario del Comune: Ottobre - Dicembre 2026
  // --- Ottobre 2026 ---
  { 2026, 10,  1, INDIFFERENZIATO },         // gio
  { 2026, 10,  2, PLASTICA },                // ven
  { 2026, 10,  3, ORGANICO },                // sab
  { 2026, 10,  4, NESSUNO },                 // dom
  { 2026, 10,  5, ORGANICO },                // lun
  { 2026, 10,  6, CARTA },                   // mar
  { 2026, 10,  7, ORGANICO },                // mer
  { 2026, 10,  8, VETRO | INDUMENTI },       // gio
  { 2026, 10,  9, PLASTICA },                // ven
  { 2026, 10, 10, ORGANICO },                // sab
  { 2026, 10, 11, NESSUNO },                 // dom
  { 2026, 10, 12, ORGANICO },                // lun
  { 2026, 10, 13, CARTA },                   // mar
  { 2026, 10, 14, ORGANICO },                // mer
  { 2026, 10, 15, INDIFFERENZIATO },         // gio
  { 2026, 10, 16, PLASTICA },                // ven
  { 2026, 10, 17, ORGANICO },                // sab
  { 2026, 10, 18, NESSUNO },                 // dom
  { 2026, 10, 19, ORGANICO },                // lun
  { 2026, 10, 20, CARTA },                   // mar
  { 2026, 10, 21, ORGANICO },                // mer
  { 2026, 10, 22, VETRO },                   // gio
  { 2026, 10, 23, PLASTICA },                // ven
  { 2026, 10, 24, ORGANICO },                // sab
  { 2026, 10, 25, NESSUNO },                 // dom
  { 2026, 10, 26, ORGANICO },                // lun
  { 2026, 10, 27, CARTA },                   // mar
  { 2026, 10, 28, ORGANICO },                // mer
  { 2026, 10, 29, INDIFFERENZIATO },         // gio
  { 2026, 10, 30, PLASTICA },                // ven
  { 2026, 10, 31, ORGANICO },                // sab

  // --- Novembre 2026 ---
  { 2026, 11,  1, NESSUNO },                 // dom
  { 2026, 11,  2, ORGANICO },                // lun
  { 2026, 11,  3, CARTA },                   // mar
  { 2026, 11,  4, ORGANICO },                // mer
  { 2026, 11,  5, VETRO | INDUMENTI },       // gio
  { 2026, 11,  6, PLASTICA },                // ven
  { 2026, 11,  7, ORGANICO },                // sab
  { 2026, 11,  8, NESSUNO },                 // dom
  { 2026, 11,  9, ORGANICO },                // lun
  { 2026, 11, 10, CARTA },                   // mar
  { 2026, 11, 11, ORGANICO },                // mer
  { 2026, 11, 12, INDIFFERENZIATO },         // gio
  { 2026, 11, 13, PLASTICA },                // ven
  { 2026, 11, 14, ORGANICO },                // sab
  { 2026, 11, 15, NESSUNO },                 // dom
  { 2026, 11, 16, ORGANICO },                // lun
  { 2026, 11, 17, CARTA },                   // mar
  { 2026, 11, 18, ORGANICO },                // mer
  { 2026, 11, 19, VETRO },                   // gio
  { 2026, 11, 20, PLASTICA },                // ven
  { 2026, 11, 21, ORGANICO },                // sab
  { 2026, 11, 22, NESSUNO },                 // dom
  { 2026, 11, 23, ORGANICO },                // lun
  { 2026, 11, 24, CARTA },                   // mar
  { 2026, 11, 25, ORGANICO },                // mer
  { 2026, 11, 26, INDIFFERENZIATO },         // gio
  { 2026, 11, 27, PLASTICA },                // ven
  { 2026, 11, 28, ORGANICO },                // sab
  { 2026, 11, 29, NESSUNO },                 // dom
  { 2026, 11, 30, ORGANICO },                // lun

  // --- Dicembre 2026 ---
  { 2026, 12,  1, CARTA },                   // mar
  { 2026, 12,  2, ORGANICO },                // mer
  { 2026, 12,  3, VETRO | INDUMENTI },       // gio
  { 2026, 12,  4, PLASTICA },                // ven
  { 2026, 12,  5, ORGANICO },                // sab
  { 2026, 12,  6, NESSUNO },                 // dom
  { 2026, 12,  7, ORGANICO },                // lun
  { 2026, 12,  8, NESSUNO },                 // mar
  { 2026, 12,  9, CARTA },                   // mer
  { 2026, 12, 10, INDIFFERENZIATO },         // gio
  { 2026, 12, 11, PLASTICA },                // ven
  { 2026, 12, 12, ORGANICO },                // sab
  { 2026, 12, 13, NESSUNO },                 // dom
  { 2026, 12, 14, ORGANICO },                // lun
  { 2026, 12, 15, CARTA },                   // mar
  { 2026, 12, 16, ORGANICO },                // mer
  { 2026, 12, 17, VETRO },                   // gio
  { 2026, 12, 18, PLASTICA },                // ven
  { 2026, 12, 19, ORGANICO },                // sab
  { 2026, 12, 20, NESSUNO },                 // dom
  { 2026, 12, 21, ORGANICO },                // lun
  { 2026, 12, 22, CARTA },                   // mar
  { 2026, 12, 23, ORGANICO },                // mer
  { 2026, 12, 24, INDIFFERENZIATO },         // gio
  { 2026, 12, 25, NESSUNO },                 // ven
  { 2026, 12, 26, ORGANICO },                // sab
  { 2026, 12, 27, NESSUNO },                 // dom
  { 2026, 12, 28, ORGANICO },                // lun
  { 2026, 12, 29, PLASTICA },                // mar
  { 2026, 12, 30, ORGANICO },                // mer
  { 2026, 12, 31, VETRO },                   // gio

  // Per il 2027 aggiungi qui le nuove date, es. { 2027,  1,  2, ORGANICO },

  { 0, 0, 0, 0 }   // fine lista - non cancellare
};

// ============================================================================
//  LED
// ============================================================================

void scriviLed(uint8_t r, uint8_t g, uint8_t b) {
  r = (uint16_t)r * LUMINOSITA / 255;
  g = (uint16_t)g * LUMINOSITA / 255;
  b = (uint16_t)b * LUMINOSITA / 255;
  if (ANODO_COMUNE) { r = 255 - r; g = 255 - g; b = 255 - b; }
  analogWrite(PIN_ROSSO, r);
  analogWrite(PIN_VERDE, g);
  analogWrite(PIN_BLU, b);
}

void ledSpento() { scriviLed(0, 0, 0); }

void ledInizializza() {
  pinMode(PIN_ROSSO, OUTPUT);
  pinMode(PIN_VERDE, OUTPUT);
  pinMode(PIN_BLU, OUTPUT);
  analogWriteRange(255);
  analogWriteFreq(1000);
  ledSpento();
}

// Mostra i rifiuti di domani: se sono piu' di uno li alterna (1,5 s ciascuno).
// Chiamata in continuazione da loop().
void mostraRifiuti(uint8_t rifiuti) {
  for (uint8_t i = 0; i < NUM_TIPI; i++) {
    if (rifiuti & (1 << i)) {
      scriviLed(COLORI[i].r, COLORI[i].g, COLORI[i].b);
      delay(1500);
    }
  }
}

// Test all'accensione (non al risveglio): mostra tutti i colori in sequenza
void testLed() {
  Serial.println(F("Test LED: R, G, B poi tutti i colori dei rifiuti"));
  scriviLed(255, 0, 0); delay(400);
  scriviLed(0, 255, 0); delay(400);
  scriviLed(0, 0, 255); delay(400);
  for (uint8_t i = 0; i < NUM_TIPI; i++) {
    Serial.printf("  %s\n", COLORI[i].nome);
    scriviLed(COLORI[i].r, COLORI[i].g, COLORI[i].b);
    delay(500);
  }
  ledSpento();
}

// 3 lampeggi rossi = errore Wi-Fi / ora
void segnalaErrore() {
  for (uint8_t i = 0; i < 3; i++) {
    scriviLed(255, 0, 0); delay(200);
    ledSpento();          delay(200);
  }
}

// ============================================================================
//  CALENDARIO: calcolo del rifiuto
// ============================================================================

uint8_t rifiutiDelGiorno(int anno, int mese, int giorno, int giornoSettimana) {
  for (const Eccezione* e = ECCEZIONI; e->anno != 0; e++) {
    if (e->anno == anno && e->mese == mese && e->giorno == giorno) return e->rifiuti;
  }
  return SETTIMANA[giornoSettimana];
}

uint8_t rifiutiDiDomani(time_t adesso) {
  struct tm t;
  localtime_r(&adesso, &t);
  t.tm_mday += 1;          // domani (mktime gestisce fine mese/anno)
  t.tm_hour = 12; t.tm_min = 0; t.tm_sec = 0;
  t.tm_isdst = -1;
  mktime(&t);              // normalizza la data e calcola tm_wday
  return rifiutiDelGiorno(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_wday);
}

int minutiDelGiorno(time_t adesso) {
  struct tm t;
  localtime_r(&adesso, &t);
  return t.tm_hour * 60 + t.tm_min;
}

bool inFascia(time_t adesso) {
  int m = minutiDelGiorno(adesso);
  for (uint8_t i = 0; i < NUM_FASCE; i++) {
    if (m >= FASCE[i].oraInizio * 60 + FASCE[i].minInizio &&
        m <  FASCE[i].oraFine   * 60 + FASCE[i].minFine) return true;
  }
  return false;
}

// Orario di oggi (o di domani) alle ore:minuti indicati
time_t orarioDelGiorno(time_t adesso, int giorniDopo, int ora, int minuto) {
  struct tm t;
  localtime_r(&adesso, &t);
  t.tm_mday += giorniDopo;
  t.tm_hour = ora; t.tm_min = minuto; t.tm_sec = 0;
  t.tm_isdst = -1;
  return mktime(&t);
}

// Il prossimo inizio di una qualsiasi fascia (oggi se non e' ancora iniziata, altrimenti domani)
time_t prossimoInizioFascia(time_t adesso) {
  time_t prossimo = 0;
  for (uint8_t i = 0; i < NUM_FASCE; i++) {
    time_t inizio = orarioDelGiorno(adesso, 0, FASCE[i].oraInizio, FASCE[i].minInizio);
    if (inizio <= adesso) inizio = orarioDelGiorno(adesso, 1, FASCE[i].oraInizio, FASCE[i].minInizio);
    if (prossimo == 0 || inizio < prossimo) prossimo = inizio;
  }
  return prossimo;
}

// ============================================================================
//  WI-FI, ORA, DEEP SLEEP
// ============================================================================

bool sincronizzaOra() {
  Serial.printf("Connessione a %s", WIFI_SSID);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > 20000) { Serial.println(F(" FALLITA")); return false; }
    delay(250);
    Serial.print('.');
  }
  Serial.println(F(" OK"));

  configTime(FUSO_ORARIO, "pool.ntp.org", "time.google.com", "it.pool.ntp.org");
  t0 = millis();
  while (time(nullptr) < 1700000000) {   // finche' l'ora non e' valida
    if (millis() - t0 > 15000) { Serial.println(F("NTP non risponde")); return false; }
    delay(200);
  }
  return true;
}

void spegniWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);
}

void deepSleepSecondi(uint32_t secondi) {
  uint64_t us = (uint64_t)secondi * 1000000ULL;
  uint64_t massimo = ESP.deepSleepMax() * 95 / 100;   // ~3 ore e mezza al massimo
  if (us > massimo) us = massimo;                       // al risveglio si ricalcola tutto
  if (us < 1000000ULL) us = 1000000ULL;

  ledSpento();
  Serial.printf("Deep sleep per %lu minuti\n", (unsigned long)(us / 60000000ULL));
  Serial.flush();
  ESP.deepSleep(us);
}

void stampaOra(time_t adesso) {
  struct tm t;
  localtime_r(&adesso, &t);
  Serial.printf("Ora: %02d/%02d/%04d %02d:%02d:%02d\n",
                t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, t.tm_hour, t.tm_min, t.tm_sec);
}

void stampaRifiuti(uint8_t rifiuti) {
  Serial.print(F("Domani: "));
  if (rifiuti == NESSUNO) Serial.print(F("nessuna raccolta"));
  for (uint8_t i = 0; i < NUM_TIPI; i++)
    if (rifiuti & (1 << i)) Serial.printf("%s ", COLORI[i].nome);
  Serial.println();
}

// ============================================================================
//  PROGRAMMA
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n\n=== Promemoria Raccolta Differenziata ==="));
  ledInizializza();

  bool daDeepSleep = ESP.getResetInfoPtr()->reason == REASON_DEEP_SLEEP_AWAKE;
  if (!daDeepSleep) testLed();

  if (!sincronizzaOra()) {
    segnalaErrore();
    spegniWifi();
    deepSleepSecondi(RIPROVA_SECONDI);
  }
  spegniWifi();   // l'orologio interno continua a funzionare anche senza Wi-Fi

  time_t adesso = time(nullptr);
  stampaOra(adesso);
  stampaRifiuti(rifiutiDiDomani(adesso));
}

void loop() {
  time_t adesso = time(nullptr);

  if (inFascia(adesso)) {
    uint8_t rifiuti = rifiutiDiDomani(adesso);
    if (rifiuti != NESSUNO) {
      mostraRifiuti(rifiuti);   // resta sveglio con il LED acceso fino a fine fascia
      return;
    }
  }

  ledSpento();
  time_t inizio = prossimoInizioFascia(adesso);
  uint32_t mancano = (uint32_t)(inizio - adesso);

  if (mancano <= ANTICIPO_RISVEGLIO_SECONDI) {
    delay(1000);                // quasi ora: aspetta sveglio senza riconnettersi
    return;
  }
  deepSleepSecondi(mancano - ANTICIPO_RISVEGLIO_SECONDI);
}
