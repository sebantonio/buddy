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

### v1.3.0 — `buddy3.ino`
Mejoras avanzadas de rendimiento y robustez:
- **Light sleep** en lugar de `delay()` cuando la pantalla está apagada (~15mA de ahorro adicional)
- **Clima en RTC memory**: al despertar de noche no conecta WiFi, usa los últimos datos guardados
- **Fade out suave** del brillo OLED antes de apagar (tanto en timeout como en deep sleep)
- **Brillo restaurado** a 127 al encender la pantalla o arrancar
- **Resync NTP cada 24h** para evitar deriva del reloj (~1-2s/hora sin resync)
- **Retry con backoff x3** en `getWeather()` si falla WiFi o la API (0s, 2s, 4s)
- **Watchdog de 30s**: reinicio automático si el código se cuelga en algún `while`
- **DHT solo con pantalla encendida**: no tiene sentido leer el sensor si no se muestra
- **Hora de última actualización** del clima visible en la página de tiempo online
- **Animación "Actualizando..."** en pantalla mientras conecta WiFi cada 4h

### v1.2.0 — `buddy2.ino`
Timeout de pantalla OLED:
- La pantalla OLED se apaga automáticamente tras **1 minuto** sin actividad
- El primer toque solo enciende la pantalla, sin cambiar de página
- El loop salta el dibujado mientras la pantalla está apagada

### v1.1.0 — `buddy.ino`
Optimizaciones de batería:
- CPU reducida de 240MHz a **80MHz** (~40mA de ahorro)
- **WiFi apagado** tras cada petición al clima y reconectado solo cuando es necesario
- Intervalo de actualización del clima ampliado de 10 min a **4 horas**
- Display y WiFi apagados antes del **deep sleep**
- Loop limitado a **~30fps** con `delay(33)`

### v1.0.0
Versión inicial.
