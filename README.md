# Promemoria Raccolta Differenziata (ESP8266)

Un LED RGB sul frigorifero si accende ogni giorno nelle fasce orarie impostate (default
13:00–14:00 e 19:00–22:00) con il colore del rifiuto da esporre per la raccolta del giorno dopo. L'ora arriva da internet (NTP, con
cambio ora legale automatico). Fuori da queste fasce la scheda va in Deep Sleep.

Sketch: [`raccolta_differenziata/raccolta_differenziata.ino`](raccolta_differenziata/raccolta_differenziata.ino)

## Collegamenti

| LED RGB        | Scheda D1 R2                                      |
|----------------|---------------------------------------------------|
| R              | D5 (GPIO14) — con resistenza 220 Ω in serie*      |
| G              | D6 (GPIO12) — con resistenza 220 Ω in serie*      |
| B              | D7 (GPIO13) — con resistenza 220 Ω in serie*      |
| Pin comune (−) | GND  → catodo comune: `ANODO_COMUNE = false`      |
| Pin comune (+) | 3V3 (NON 5V) → anodo comune: `ANODO_COMUNE = true`|

\* I moduli LED già montati su basetta (es. KY-016) hanno spesso le resistenze a bordo.

### Ponticello per il Deep Sleep (importante)

Per svegliarsi da solo, il chip deve avere **GPIO16 collegato a RST**.

- Sulla **D1 R2 formato Arduino Uno**, GPIO16 è il pin serigrafato **D2**.
- Il pin serigrafato **"RX ← D0"** è invece GPIO3 (RX della seriale): collegarlo a RST
  **non sveglia la scheda** e può impedire il caricamento dello sketch e il monitor seriale.
- Se il caricamento dello sketch fallisce, stacca il ponticello durante l'upload e
  ricollegalo dopo.

## Configurazione

Apri lo sketch e modifica la sezione `CONFIGURAZIONE`:

1. `WIFI_SSID` / `WIFI_PASSWORD` (solo reti a 2,4 GHz: l'ESP8266 non vede il 5 GHz).
2. Fasce orarie in `FASCE[]`: una riga per fascia, es. `{ 13, 0, 14, 0 }` = 13:00–14:00.
3. `ANODO_COMUNE` in base al tuo LED. Se i colori risultano "invertiti" (LED acceso
   quando dovrebbe essere spento) cambia questo valore.
4. **Calendario**: è già inserito quello del Comune da **Ottobre a Dicembre 2026**
   (in `ECCEZIONI[]`, un giorno per riga). Dopo il 31/12/2026 il LED non si accende
   più finché non aggiungi le date del 2027.
   - `SETTIMANA[]`: cosa si raccoglie in ciascun giorno della settimana (ora tutto
     `NESSUNO`, perché il calendario non segue uno schema settimanale fisso).
   - `ECCEZIONI[]`: date precise che sostituiscono il giorno della settimana
     (festivi, `VETRO | INDUMENTI`, giorni senza raccolta con `NESSUNO`).
   - Le date sono quelle della **raccolta**: il LED si accende la sera prima.
   - Per gli anni successivi basta aggiungere nuove righe ad `ECCEZIONI[]` e ricaricare.

## Test rapido dell'hardware

Prima dello sketch principale puoi caricare
[`test_hardware/test_hardware.ino`](test_hardware/test_hardware.ino) (stesse impostazioni
Wi-Fi, LED e pin). In circa un minuto, seguendo il Monitor seriale, verifica: LED spento
(tipo di LED corretto), canali rosso/verde/blu, colori dei rifiuti, Wi-Fi e ora da
internet, e infine il risveglio dal Deep Sleep dopo 15 secondi (ponticello RST ↔ D2).

## Caricamento (Arduino IDE)

1. Preferenze → URL aggiuntive per il Gestore schede:
   `https://arduino.esp8266.com/stable/package_esp8266com_index.json`
2. Gestore schede → installa **esp8266** (by ESP8266 Community).
3. Scheda: **LOLIN(WEMOS) D1 R2 & mini** (va bene anche "WeMos D1 R1").
4. Carica lo sketch e apri il Monitor seriale a **115200 baud**.

## Cosa vedi

- All'accensione: test del LED (rosso, verde, blu, poi tutti i colori dei rifiuti).
- Sul Monitor seriale: ora letta da internet e rifiuti di domani.
- **3 lampeggi rossi** = Wi-Fi o ora non disponibili; riprova dopo 10 minuti.

| Rifiuto         | Colore         |
|-----------------|----------------|
| Organico        | arancione      |
| Plastica        | giallo         |
| Carta           | blu            |
| Vetro           | verde          |
| Indifferenziato | bianco         |
| Indumenti       | viola          |
| Verde/Sfalci    | verde acqua    |

I colori si cambiano nella tabella `COLORI[]`.

## Note sull'alimentazione

- Durante le fasce orarie la scheda **resta sveglia** (il Deep Sleep spegne i pin e
  quindi anche il LED), ma con il Wi-Fi spento.
- Il Deep Sleep dura al massimo ~3,5 ore per volta: la scheda si risveglia, rilegge
  l'ora e torna a dormire fino alla sera.
- **Power bank**: molti si spengono da soli quando l'assorbimento è troppo basso
  (proprio durante il Deep Sleep) e poi la scheda non si riaccende più. Usa un power
  bank con modalità "sempre acceso / bassa corrente", oppure un alimentatore USB 5V.
- La D1 R2 formato Uno ha a bordo il chip USB e il regolatore, che consumano anche in
  Deep Sleep: con un power bank la durata sarà di giorni/settimane, non di mesi.
