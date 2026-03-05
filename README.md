# DeskBuddy 🤖

Compañero de escritorio basado en **ESP32** con pantalla OLED 128x64. Muestra ojos animados con física de muelle, reloj, temperatura/humedad local y del tiempo online, y un temporizador Pomodoro.

---

## Hardware

| Componente | Detalle |
|---|---|
| MCU | ESP32 (cualquier variante con WiFi) |
| Display | SSD1306 128x64 OLED (I2C) |
| Sensor | DHT22 (temperatura + humedad) |
| Entrada | Pin táctil capacitivo |

### Pines

| Pin | Función |
|---|---|
| GPIO 8 | SDA (I2C display) |
| GPIO 9 | SCL (I2C display) |
| GPIO 4 | Touch / botón |
| GPIO 5 | DHT22 datos |

---

## Funcionalidades

### Páginas (navegación con toque corto)
- **Página 0** — Ojos animados (motor de física con muelle/amortiguador)
- **Página 1** — Reloj y fecha (NTP sincronizado)
- **Página 2** — Temperatura y humedad local (DHT22)
- **Página 3** — Temperatura y descripción del tiempo online (OpenWeatherMap)
- **Página 4** — Pomodoro (toque largo para entrar/salir)

### Modos de ánimo
Los ojos cambian de forma según el tiempo y la hora:

| Mood | Condición |
|---|---|
| HAPPY | Cielo despejado |
| SAD | Lluvia / llovizna |
| LOVE | Nieve |
| SURPRISED | Tormenta |
| SUSPICIOUS | Niebla / bruma |
| EXCITED | Temperatura > 30°C |
| SLEEPY | Temperatura < 5°C o modo noche |
| ANGRY | — |
| NORMAL | Resto de condiciones |

### Deep sleep nocturno
- Duerme automáticamente de **21:00 a 06:00**
- Se despierta con un toque (modo nocturno de 30s)

### Timeout de pantalla
- La pantalla OLED se apaga tras **1 minuto** sin actividad
- El primer toque solo enciende la pantalla (sin cambiar de página)

---

## Configuración

Si el WiFi falla en el arranque, el dispositivo crea un punto de acceso:

- **SSID:** `DeskBuddy-Setup`
- **Password:** `12345678`
- **URL:** `http://192.168.4.1`

Parámetros configurables: SSID, contraseña WiFi, API key OpenWeatherMap, ciudad, zona horaria (TZ POSIX), latitud y longitud.

---

## Dependencias (Arduino IDE)

- `Adafruit SSD1306`
- `Adafruit GFX Library`
- `DHT sensor library` (Adafruit)
- `Arduino_JSON`
- `ESP32` board package

---

## Versiones

| Versión | Cambios |
|---|---|
| v1.2.0 | Timeout OLED (1 min), todas las optimizaciones de batería |
| v1.1.0 | CPU a 80MHz, WiFi apagado entre actualizaciones, clima cada 4h, deep sleep mejorado |
| v1.0.0 | Versión inicial |
