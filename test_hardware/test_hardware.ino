/*
 * TEST HARDWARE - Promemoria Raccolta Differenziata
 *
 * Carica questo sketch, apri il Monitor seriale a 115200 baud e segui le istruzioni.
 * Controlla in pochi minuti, senza aspettare la sera:
 *   1. LED spento        -> capisci se ANODO_COMUNE e' giusto
 *   2. Rosso, Verde, Blu -> capisci se i pin D5/D6/D7 sono collegati bene
 *   3. Colori dei rifiuti
 *   4. Wi-Fi + ora da internet
 *   5. Deep Sleep di 15 secondi -> capisci se il ponticello RST <-> D2 (GPIO16) funziona
 *
 * Usa le STESSE impostazioni dello sketch principale (copiale da li').
 */

#include <ESP8266WiFi.h>
#include <time.h>

// ---- Copia qui le stesse impostazioni dello sketch principale ----
const char* WIFI_SSID     = "IL_TUO_WIFI";
const char* WIFI_PASSWORD = "LA_TUA_PASSWORD";
const bool  ANODO_COMUNE  = false;
const uint8_t PIN_ROSSO = 14;   // D5
const uint8_t PIN_VERDE = 12;   // D6
const uint8_t PIN_BLU   = 13;   // D7
#define FUSO_ORARIO "CET-1CEST,M3.5.0,M10.5.0/3"
// ------------------------------------------------------------------

const uint32_t SECONDI_DEEP_SLEEP = 15;

void scriviLed(uint8_t r, uint8_t g, uint8_t b) {
  if (ANODO_COMUNE) { r = 255 - r; g = 255 - g; b = 255 - b; }
  analogWrite(PIN_ROSSO, r);
  analogWrite(PIN_VERDE, g);
  analogWrite(PIN_BLU, b);
}

void passo(const char* titolo, const char* cosaVedi, uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
  Serial.printf("  %-16s -> dovresti vedere: %s\n", titolo, cosaVedi);
  scriviLed(r, g, b);
  delay(ms);
}

void testLed() {
  Serial.println(F("\n[1] LED SPENTO"));
  passo("Spento", "NIENTE (se il LED e' acceso, cambia ANODO_COMUNE)", 0, 0, 0, 3000);

  Serial.println(F("\n[2] CANALI SINGOLI (se il colore e' diverso, scambia i fili)"));
  passo("D5 Rosso", "ROSSO", 255, 0, 0, 2000);
  passo("D6 Verde", "VERDE", 0, 255, 0, 2000);
  passo("D7 Blu",   "BLU",   0, 0, 255, 2000);

  Serial.println(F("\n[3] COLORI DEI RIFIUTI"));
  passo("Organico",        "ARANCIONE",   255,  40,   0, 2000);
  passo("Plastica",        "GIALLO",      255, 150,   0, 2000);
  passo("Carta",           "BLU",           0,   0, 255, 2000);
  passo("Vetro",           "VERDE",         0, 255,   0, 2000);
  passo("Indifferenziato", "BIANCO",      255, 255, 255, 2000);
  passo("Indumenti",       "VIOLA",       160,   0, 255, 2000);
  scriviLed(0, 0, 0);
}

bool testWifiOra() {
  Serial.printf("\n[4] WI-FI \"%s\"", WIFI_SSID);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > 20000) {
      Serial.println(F("\n  ERRORE: connessione non riuscita."));
      Serial.println(F("  Controlla nome/password e che la rete sia a 2,4 GHz (non 5 GHz)."));
      return false;
    }
    delay(250);
    Serial.print('.');
  }
  Serial.printf("\n  OK, IP %s, segnale %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());

  configTime(FUSO_ORARIO, "pool.ntp.org", "time.google.com", "it.pool.ntp.org");
  t0 = millis();
  while (time(nullptr) < 1700000000) {
    if (millis() - t0 > 15000) { Serial.println(F("  ERRORE: ora da internet non ricevuta")); return false; }
    delay(200);
  }
  time_t adesso = time(nullptr);
  struct tm t;
  localtime_r(&adesso, &t);
  Serial.printf("  Ora italiana: %02d/%02d/%04d %02d:%02d:%02d  (controlla che sia giusta)\n",
                t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, t.tm_hour, t.tm_min, t.tm_sec);
  return true;
}

void lampeggia(uint8_t r, uint8_t g, uint8_t b, uint8_t volte) {
  for (uint8_t i = 0; i < volte; i++) {
    scriviLed(r, g, b); delay(250);
    scriviLed(0, 0, 0); delay(250);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(PIN_ROSSO, OUTPUT);
  pinMode(PIN_VERDE, OUTPUT);
  pinMode(PIN_BLU, OUTPUT);
  analogWriteRange(255);
  analogWriteFreq(1000);
  scriviLed(0, 0, 0);

  if (ESP.getResetInfoPtr()->reason == REASON_DEEP_SLEEP_AWAKE) {
    Serial.println(F("\n\n[5] RISVEGLIO DAL DEEP SLEEP: OK!"));
    Serial.println(F("  Il ponticello RST <-> D2 (GPIO16) funziona."));
    Serial.println(F("  Il LED lampeggia VERDE 5 volte. Test completato: carica lo sketch principale."));
    lampeggia(0, 255, 0, 5);
    return;
  }

  Serial.println(F("\n\n=== TEST HARDWARE RACCOLTA DIFFERENZIATA ==="));
  testLed();
  bool wifiOk = testWifiOra();
  lampeggia(wifiOk ? 0 : 255, wifiOk ? 255 : 0, 0, 3);   // 3 verdi = ok, 3 rossi = errore

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  Serial.printf("\n[5] DEEP SLEEP per %lu secondi...\n", (unsigned long)SECONDI_DEEP_SLEEP);
  Serial.println(F("  Se tra 15-20 secondi qui compare \"RISVEGLIO DAL DEEP SLEEP: OK!\" e il LED"));
  Serial.println(F("  lampeggia verde, e' tutto a posto. Se non succede NIENTE, il ponticello e'"));
  Serial.println(F("  sbagliato: deve andare da RST al pin D2 (GPIO16), NON a \"RX<-D0\"."));
  Serial.println(F("  (Premi il tasto RESET per ripetere tutto il test.)"));
  Serial.flush();
  ESP.deepSleep((uint64_t)SECONDI_DEEP_SLEEP * 1000000ULL);
}

void loop() {}
