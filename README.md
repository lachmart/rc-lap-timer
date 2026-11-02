# RC Lap Timer

Časomíra pro jedno RC auto. ESP32 + laserová závora + webová aplikace v mobilu přes Wi-Fi.

## Součástky

- ESP32 dev board (ESP32-WROOM, „esp32dev“)
- laserový modul 650 nm (5 V nebo 3,3 V)
- fotoodpor LDR (např. GL5528)
- rezistor 10 kΩ
- černá trubička 2 až 3 cm na LDR (stíní okolní světlo)

## Zapojení

```
3V3 ── LDR ──┬── GPIO 32
             └── 10 kΩ ── GND

Laser  VCC → 5V (nebo 3V3), GND → GND
```

Laser může mít i vlastní napájení na druhé straně tratě, pak nevedou dráty přes trať.
Senzor musí být na ADC1 (GPIO 32 až 39), ADC2 s běžící Wi-Fi nefunguje.

Polarita zapojení je jedno. Kód sleduje odchylku od klidové hodnoty, ne absolutní hranici.

## Nahrání

**PlatformIO**

```
pio run -t upload
pio device monitor
```

**Arduino IDE**

1. Board ESP32 Dev Module (balík esp32 od Espressif)
2. Knihovna *WebSockets* od Markus Sattler (Správce knihoven)
3. Otevřít `timer.ino`, nahrát

## Použití

1. Zapnout laser, namířit do trubičky s LDR, pak zapnout ESP32 (při startu se kalibruje klid).
2. V telefonu se připojit k Wi-Fi **RC_Lap_Timer**. Stránka timeru se otevře sama
   (captive portal). Kdyby ne, otevřít `http://192.168.4.1`.
3. První průjezd startuje, každý další zapíše kolo.

V záložce **Senzor** je živá hodnota ADC (pomáhá při míření laseru), kalibrace,
minimální čas kola a citlivost. Nastavení se ukládá do flash.

### Domácí Wi-Fi (volitelné)

V záložce **Wi-Fi** jde zadat domácí síť. ESP32 se po restartu připojí i k ní a je
dostupné na `http://laptimer.local` (nebo IP z výpisu). Telefon pak nemusí přepínat
Wi-Fi a neztrácí internet. Vlastní síť RC_Lap_Timer běží vždy. Když domácí síť
není při startu do 10 s v dosahu (třeba venku na trati), běží jen vlastní síť.

Pozn. `.local` adresy fungují na iPhonu a desktopu, na některých Androidech ne,
tam použij IP adresu.

### Okamžité časy

Stránka dostává data přes WebSocket (port 81). Nové kolo přijde hned po průjezdu,
živá data senzoru 5x za sekundu. Při výpadku spojení se stránka sama přepne do
záložního režimu (HTTP každou sekundu) a WebSocket znovu připojí.
Stav spojení ukazuje tečka vpravo nahoře.

## API

| Metoda | Cesta | Popis |
|---|---|---|
| GET | `/api/state` | kompletní stav (kola, nejlepší, nastavení) |
| POST | `/api/reset` | nový trénink |
| POST | `/api/calibrate` | kalibrace klidu |
| POST | `/api/config?minLap=2000&delta=800` | nastavení (ms, ADC) |
| POST | `/api/wifi?ssid=...&pass=...` | domácí Wi-Fi, prázdné ssid = zapomenout |
| WS | `ws://<ip>:81/` | zprávy `{"t":"state",...}` a `{"t":"live",...}` |
