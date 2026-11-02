# RC Lap Timer

Časomíra kol pro RC auto. ESP32 + laserová závora + webová aplikace v mobilu přes Wi-Fi.
Hobby projekt. Komunikace, komentáře v kódu, UI a commity česky.

## Stav

- Kód napsán, ale **zatím nikdy nezkompilován ani nenahrán na desku**. První `pio run` může odhalit drobné chyby (API ESP32 Arduino core 2.x vs 3.x, WebSockets knihovna). Opravuj podle výpisu.
- Hardware zatím nesestaven a neodzkoušen. Hodnoty `DEF_DELTA` a `DEF_MIN_LAP` jsou odhad, ladí se přes web UI.
- Vývoj na Macu, PlatformIO ve VS Code (`pio run -t upload`, `pio device monitor`).

## Struktura

- `timer.ino` – celý firmware v jednom souboru včetně HTML/JS stránky (raw string `PAGE`)
- `platformio.ini` – `src_dir = .`, `build_src_filter = +<*.ino>`, lib `links2004/WebSockets`
- `README.md` – zapojení, nahrání, použití, API

## Hardware

- ESP32 dev board (esp32dev)
- Laser 650 nm svítí přes trať do LDR v černé trubičce
- Dělič `3V3 ── LDR ──┬── GPIO 32`, `└── 10 kΩ ── GND`
- Senzor musí být na ADC1 (GPIO 32–39), ADC2 nefunguje se zapnutou Wi-Fi

## Architektura firmware

- **Měření** v samostatném FreeRTOS tasku (`sensorTask`, core 1, ~1 kHz přes `vTaskDelay(1)`), časy z `esp_timer_get_time()` (µs).
- Detekce = odchylka od klidové hodnoty (`|raw - base| > delta`), takže nezáleží na polaritě zapojení. Hystereze `delta/2`. Klid se kalibruje při startu a na požádání, mimo průjezd pomalu sleduje okolní světlo (EMA ~2 s).
- První průjezd jen startuje, každý další zapíše kolo. Průjezdy kratší než `minLapMs` se zahodí (počítadlo `ignored`).
- Sdílený stav chráněn `portMUX` (`mux`). Sensor task jen nastaví `stateDirty`, broadcast dělá `loop()`.
- **Wi-Fi**: AP `RC_Lap_Timer` běží vždy (192.168.4.1) + captive portal (DNSServer na vše, `onNotFound` přesměruje cizí hosty). Volitelně STA na domácí síť (SSID/heslo v Preferences, nastavení z UI). Když se STA do 10 s nepřipojí, vypne se, aby skenování nerušilo AP. mDNS `laptimer.local`.
- **Komunikace**: WebSocket port 81. Zprávy `{"t":"state",...}` (kola, best, nastavení; při připojení, novém kole, resetu, změně configu) a `{"t":"live",...}` (raw ADC, base, broken, cur; každých 200 ms). Stránka při výpadku WS přepne na HTTP polling `/api/state` 1 s a WS znovu připojuje.
- HTTP API: `GET /api/state`, `POST /api/reset`, `/api/calibrate`, `/api/config?minLap=&delta=`, `/api/wifi?ssid=&pass=` (pak restart).
- Nastavení v `Preferences` namespace `laptimer` (`minLap`, `delta`, `ssid`, `pass`).
- Stránka: aktuální kolo počítá klient lokálně (`cur` + `performance.now()`), zvuk přes WebAudio (zapíná se tlačítkem), vibrace, flash při novém/nejlepším kole.

## Rozhodnutí a zvažované varianty

- Zatím **jen jedno auto**. Pro více aut bez úpravy aut byly zvažovány oddělené pruhy s vlastní závorou nebo závory v různých výškách. S úpravou aut IR LED + NE555 na různých frekvencích a TSOP přijímače. RFID (UHF jako na maratonech) je drahé a nemá rád karbon/kov.
- **Doppler**: akustický (zvuk motoru) zamítnut jako časomíra kvůli přesnosti (~±10–30 ms, plyn mění frekvenci). Mikrovlnný radar (HB100 10,5 GHz, ~19,5 Hz na km/h) je možné budoucí rozšíření jako **měřič rychlosti** na rovince, ne jako časomíra. Martin zjišťuje, zda modul má.
- **Bluetooth** záměrně ne. Zůstává Wi-Fi kvůli kompatibilitě s iPhonem bez instalace aplikace.

## Možné další kroky

- První kompilace, nahrání, oživení HW, doladění `delta`/`minLap`
- HB100 radar pro rychlost
- Historie tréninků uložená na ESP32 + export CSV
- Hlasové hlášení časů (speechSynthesis)
