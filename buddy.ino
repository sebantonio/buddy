#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "time.h"
#include <math.h>
#include <DHT.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <esp_task_wdt.h>

// ================= DEEP SLEEP =================
RTC_DATA_ATTR bool hadNTPSync = false;
// Último clima conocido en RTC (sobrevive al deep sleep)
RTC_DATA_ATTR float    rtcApiTemp    = 0;
RTC_DATA_ATTR float    rtcFeelsLike  = 0;
RTC_DATA_ATTR char     rtcWeatherMain[16] = "";
RTC_DATA_ATTR char     rtcWeatherDesc[32] = "";
#define SLEEP_HOUR_START      21
#define SLEEP_HOUR_END         6
#define NIGHT_WAKE_MS      30000UL
unsigned long nightWakeStart = 0;
bool nightWakeActive = false;

// ================= PIN CONFIG =================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SDA_PIN 8
#define SCL_PIN 9
#define TOUCH_PIN 4
#define DHTPIN 5
#define DHTTYPE DHT22
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT dht(DHTPIN, DHTTYPE);

// ================= EMOTION BITMAPS (16x16) =================
const unsigned char bmp_heart[] PROGMEM = {
  0x00,0x00,0x0c,0x60,0x1e,0xf0,0x3f,0xf8,0x7f,0xfc,0x7f,0xfc,
  0x7f,0xfc,0x3f,0xf8,0x1f,0xf0,0x0f,0xe0,0x07,0xc0,0x03,0x80,
  0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
const unsigned char bmp_zzz[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3c,0x00,0x0c,0x00,0x18,
  0x00,0x30,0x00,0x7e,0x00,0x00,0x3c,0x00,0x0c,0x00,0x18,0x00,
  0x30,0x00,0x7c,0x00,0x00,0x00,0x00,0x00
};
const unsigned char bmp_anger[] PROGMEM = {
  0x00,0x00,0x11,0x10,0x2a,0x90,0x44,0x40,0x80,0x20,0x80,0x20,
  0x44,0x40,0x2a,0x90,0x11,0x10,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

// ================= MOOD CONSTANTS =================
#define MOOD_NORMAL    0
#define MOOD_HAPPY     1
#define MOOD_SURPRISED 2
#define MOOD_SLEEPY    3
#define MOOD_ANGRY     4
#define MOOD_SAD       5
#define MOOD_EXCITED   6
#define MOOD_LOVE      7
#define MOOD_SUSPICIOUS 8
int currentMood = MOOD_NORMAL;
const char* diasSemana[] = {"Dom", "Lun", "Mar", "Mie", "Jue", "Vie", "Sab"};

// ================= PHYSICS EYE ENGINE =================
struct Eye {
  float x, y, w, h;
  float targetX, targetY, targetW, targetH;
  float pupilX, pupilY;
  float targetPupilX, targetPupilY;
  float velX, velY, velW, velH;
  float pVelX, pVelY;
  float k  = 0.12;
  float d  = 0.60;
  float pk = 0.08;
  float pd = 0.50;
  bool blinking;
  unsigned long lastBlink;
  unsigned long nextBlinkTime;
  void init(float _x, float _y, float _w, float _h) {
    x = targetX = _x;
    y = targetY = _y;
    w = targetW = _w;
    h = targetH = _h;
    pupilX = targetPupilX = 0;
    pupilY = targetPupilY = 0;
    velX = velY = velW = velH = 0;
    pVelX = pVelY = 0;
    blinking = false;
    nextBlinkTime = millis() + random(1000, 4000);
  }
  void update() {
    float ax = (targetX - x) * k;
    float ay = (targetY - y) * k;
    float aw = (targetW - w) * k;
    float ah = (targetH - h) * k;
    velX = (velX + ax) * d;
    velY = (velY + ay) * d;
    velW = (velW + aw) * d;
    velH = (velH + ah) * d;
    x += velX;
    y += velY;
    w += velW;
    h += velH;
    float pax = (targetPupilX - pupilX) * pk;
    float pay = (targetPupilY - pupilY) * pk;
    pVelX = (pVelX + pax) * pd;
    pVelY = (pVelY + pay) * pd;
    pupilX += pVelX;
    pupilY += pVelY;
  }
};
Eye leftEye, rightEye;
unsigned long lastSaccade = 0;
unsigned long saccadeInterval = 3000;
float breathVal = 0;

// ================= WIFI DEFAULT =================
const char* DEFAULT_SSID = "MiFibra-8E5F";
const char* DEFAULT_PASS = "WdpsW9yc+*MjSaEAI#";
const char* ntpServer = "pool.ntp.org";
Preferences prefs;
WebServer server(80);
String wifiSsid;
String wifiPass;
String apiKey;
String city;
String tzString;
String latStr;
String lonStr;
bool inConfigMode = false;

// DHT local
float temperature = 0;
float humidity = 0;

// OpenWeatherMap
float apiTemperature = 0;
float feelsLike = 0;
String weatherMain = "";
String weatherDesc = "";
unsigned long lastWeatherUpdate = 0;
unsigned long lastDHTRead = 0;
char weatherUpdateTime[6] = "--:--";   // "HH:MM" de la última actualización

// Clima cada 4 horas
#define WEATHER_UPDATE_MS 14400000UL

// NTP resync cada 24 horas
#define NTP_RESYNC_MS 86400000UL
unsigned long lastNTPSync = 0;

// Páginas: 0=Ojos, 1=Reloj, 2=Clima local (DHT), 3=Clima online (API)
int currentPage = 0;
const int TOTAL_PAGES = 4;

// Debounce y detección de pulsación
bool lastTouch = false;
unsigned long lastTouchTime  = 0;
unsigned long touchStartTime = 0;
bool touchHandled = false;
#define TOUCH_DEBOUNCE_MS 300
#define LONG_PRESS_MS     800

// Auto-rotación
unsigned long lastAutoRotate = 0;
#define AUTO_ROTATE_MS 8000

// Chequeo modo noche
unsigned long lastMoodCheck = 0;

// Timeout pantalla OLED
#define SCREEN_TIMEOUT_MS 60000UL
unsigned long lastActivityTime = 0;
bool screenOn = true;

// Pomodoro
#define POMO_IDLE  0
#define POMO_WORK  1
#define POMO_BREAK 2
int pomoState = POMO_IDLE;
unsigned long pomoStart = 0;
int pomoRound = 0;
#define POMO_WORK_MS  1500000UL
#define POMO_BREAK_MS  300000UL

// =================================================
// CONFIG
// =================================================
void loadConfig() {
  prefs.begin("deskbuddy", true);
  wifiSsid = prefs.getString("ssid",    DEFAULT_SSID);
  wifiPass = prefs.getString("pass",    DEFAULT_PASS);
  apiKey   = prefs.getString("apikey",  "3b8f171b52e27a3b14f5bf701c1db22b");
  city     = prefs.getString("city",    "Alcazar de San Juan");
  tzString = prefs.getString("tz",      "CET-1CEST,M3.5.0/2,M10.5.0/3");
  latStr   = prefs.getString("lat",     "39.3942");
  lonStr   = prefs.getString("lon",     "-3.2097");
  prefs.end();
}

void saveConfig(String s, String p, String ak, String c, String tz, String lat, String lon) {
  prefs.begin("deskbuddy", false);
  prefs.putString("ssid",   s);
  prefs.putString("pass",   p);
  prefs.putString("apikey", ak);
  prefs.putString("city",   c);
  prefs.putString("tz",     tz);
  prefs.putString("lat",    lat);
  prefs.putString("lon",    lon);
  prefs.end();
}

// =================================================
// PORTAL
// =================================================
void startConfigPortal() {
  inConfigMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("DeskBuddy-Setup", "12345678");
  server.on("/", []() {
    server.send(200, "text/html",
      "<form action='/save' method='POST'>"
      "SSID:<input name='ssid'><br>"
      "PASS:<input name='pass'><br>"
      "API Key:<input name='apikey'><br>"
      "Ciudad (display):<input name='city'><br>"
      "TZ:<input name='tz'><br>"
      "Latitud:<input name='lat'><br>"
      "Longitud:<input name='lon'><br>"
      "<button type='submit'>Save</button></form>");
  });
  server.on("/save", HTTP_POST, []() {
    saveConfig(
      server.arg("ssid"), server.arg("pass"), server.arg("apikey"),
      server.arg("city"), server.arg("tz"), server.arg("lat"), server.arg("lon")
    );
    server.send(200, "text/html", "Guardado. Reiniciando...");
    delay(1500);
    ESP.restart();
  });
  server.begin();
  display.clearDisplay();
  display.setFont(NULL);
  display.setCursor(0, 20);
  display.print("CONFIG MODE");
  display.print("IP: 192.168.4.1");
  display.display();
}

// =================================================
// WIFI + NTP
// =================================================
bool connectWiFi() {
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  unsigned long start = millis();
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    intentos++;
    Serial.print(".");
    if (intentos % 10 == 0) {
      Serial.print(" [status=");
      Serial.print(WiFi.status());
      Serial.println("]");
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(">>> WiFi CONECTADO <<<");
    return true;
  }
  Serial.println(">>> ERROR WiFi <<<");
  return false;
}

void initTime() {
  configTime(0, 0, ntpServer);
  setenv("TZ", tzString.c_str(), 1);
  tzset();
  struct tm t;
  int intentos = 0;
  while (!getLocalTime(&t) && intentos < 10) { delay(500); intentos++; }
  lastNTPSync = millis();
}

// =================================================
// WEATHER
// =================================================
void updateMoodBasedOnWeather() {
  if      (weatherMain == "Clear")                             currentMood = MOOD_HAPPY;
  else if (weatherMain == "Rain" || weatherMain == "Drizzle") currentMood = MOOD_SAD;
  else if (weatherMain == "Snow")                             currentMood = MOOD_LOVE;
  else if (weatherMain == "Thunderstorm")                     currentMood = MOOD_SURPRISED;
  else if (weatherMain == "Fog"  || weatherMain == "Mist")    currentMood = MOOD_SUSPICIOUS;
  else if (apiTemperature > 30)                               currentMood = MOOD_EXCITED;
  else if (apiTemperature < 5)                                currentMood = MOOD_SLEEPY;
  else                                                        currentMood = MOOD_NORMAL;
}

void checkNightMode() {
  struct tm t;
  if (!getLocalTime(&t)) return;
  if (t.tm_hour >= 22 || t.tm_hour < 7)
    currentMood = MOOD_SLEEPY;
  else
    updateMoodBasedOnWeather();
}

bool isNightTime() {
  struct tm t;
  if (!getLocalTime(&t)) return false;
  return (t.tm_hour >= SLEEP_HOUR_START || t.tm_hour < SLEEP_HOUR_END);
}

void drawUltraProEye(Eye& e, bool isLeft);  // forward declaration

// Animación WiFi mientras conecta
void drawWiFiConnecting() {
  static int dots = 0;
  display.clearDisplay();
  display.setFont(NULL);
  display.setCursor(20, 20);
  display.print("Actualizando");
  display.setCursor(52, 34);
  for (int i = 0; i < (dots % 4); i++) display.print(".");
  display.display();
  dots++;
}

void goToDeepSleep() {
  currentMood = MOOD_SLEEPY;
  leftEye.targetW = rightEye.targetW = 38;
  leftEye.targetH = rightEye.targetH = 30;
  for (int i = 0; i < 50; i++) {
    leftEye.update(); rightEye.update();
    display.clearDisplay();
    drawUltraProEye(leftEye, true);
    drawUltraProEye(rightEye, false);
    display.drawBitmap(110, 0, bmp_zzz, 16, 16, SSD1306_WHITE);
    display.display();
    delay(30);
  }
  leftEye.targetH = rightEye.targetH = 2;
  for (int i = 0; i < 20; i++) {
    leftEye.update(); rightEye.update();
    display.clearDisplay();
    drawUltraProEye(leftEye, true);
    drawUltraProEye(rightEye, false);
    display.display();
    delay(30);
  }

  // Fade out brillo antes de apagar
  for (int b = 127; b >= 0; b -= 16) {
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(b);
    delay(20);
  }
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  esp_task_wdt_deinit();
  esp_deep_sleep_enable_gpio_wakeup(1ULL << TOUCH_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);
  esp_deep_sleep_start();
}

void getWeather() {
  // Mostrar animación mientras conecta
  drawWiFiConnecting();

  // Retry con backoff: hasta 3 intentos (2s, 4s)
  bool connected = false;
  for (int attempt = 0; attempt < 3 && !connected; attempt++) {
    if (attempt > 0) {
      Serial.printf("Reintento WiFi #%d\n", attempt);
      delay(2000 * attempt);
    }
    connected = connectWiFi();
  }
  if (!connected) {
    Serial.println(">>> getWeather: sin WiFi, usando datos RTC <<<");
    // Usar último clima conocido de RTC
    if (strlen(rtcWeatherMain) > 0) {
      apiTemperature = rtcApiTemp;
      feelsLike      = rtcFeelsLike;
      weatherMain    = String(rtcWeatherMain);
      weatherDesc    = String(rtcWeatherDesc);
    }
    return;
  }

  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?lat=" +
               latStr + "&lon=" + lonStr +
               "&appid=" + apiKey + "&units=metric";
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    JSONVar obj = JSON.parse(payload);
    if (JSON.typeof(obj) != "undefined") {
      apiTemperature = double(obj["main"]["temp"]);
      feelsLike      = double(obj["main"]["feels_like"]);
      weatherMain    = (const char*)obj["weather"][0]["main"];
      weatherDesc    = (const char*)obj["weather"][0]["description"];

      // Guardar en RTC para sobrevivir deep sleep
      rtcApiTemp   = apiTemperature;
      rtcFeelsLike = feelsLike;
      weatherMain.toCharArray(rtcWeatherMain, sizeof(rtcWeatherMain));
      weatherDesc.toCharArray(rtcWeatherDesc, sizeof(rtcWeatherDesc));

      // Guardar hora de actualización
      struct tm t;
      if (getLocalTime(&t))
        sprintf(weatherUpdateTime, "%02d:%02d", t.tm_hour, t.tm_min);

      checkNightMode();
    }
  } else {
    Serial.printf(">>> HTTP error %d, usando datos RTC <<<\n", httpCode);
    if (strlen(rtcWeatherMain) > 0) {
      apiTemperature = rtcApiTemp;
      feelsLike      = rtcFeelsLike;
      weatherMain    = String(rtcWeatherMain);
      weatherDesc    = String(rtcWeatherDesc);
    }
  }
  http.end();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println(">>> WiFi OFF <<<");
}

// =================================================
// EYE DRAWING
// =================================================
void drawEyelidMask(float x, float y, float w, float h, int mood, bool isLeft) {
  int ix = (int)x, iy = (int)y, iw = (int)w, ih = (int)h;
  if (mood == MOOD_ANGRY) {
    if (isLeft)
      for (int i = 0; i < 16; i++) display.drawLine(ix, iy + i, ix + iw, iy - 6 + i, SSD1306_BLACK);
    else
      for (int i = 0; i < 16; i++) display.drawLine(ix, iy - 6 + i, ix + iw, iy + i, SSD1306_BLACK);
  } else if (mood == MOOD_SAD) {
    if (isLeft)
      for (int i = 0; i < 16; i++) display.drawLine(ix, iy - 6 + i, ix + iw, iy + i, SSD1306_BLACK);
    else
      for (int i = 0; i < 16; i++) display.drawLine(ix, iy + i, ix + iw, iy - 6 + i, SSD1306_BLACK);
  } else if (mood == MOOD_LOVE) {
    display.fillRect(ix, iy + ih * 4 / 5, iw, ih, SSD1306_BLACK);
  } else if (mood == MOOD_SLEEPY) {
    display.fillRect(ix, iy, iw, ih / 2 + 2, SSD1306_BLACK);
  } else if (mood == MOOD_SUSPICIOUS) {
    if (isLeft) display.fillRect(ix, iy, iw, ih / 2 - 2, SSD1306_BLACK);
    else        display.fillRect(ix, iy + ih - 8, iw, 8, SSD1306_BLACK);
  }
}

void drawUltraProEye(Eye& e, bool isLeft) {
  int ix = (int)e.x, iy = (int)e.y, iw = (int)e.w, ih = (int)e.h;
  int r = (iw < 20) ? 3 : 8;
  display.fillRoundRect(ix, iy, iw, ih, r, SSD1306_WHITE);
  int cx = ix + iw / 2;
  int cy = iy + ih / 2;
  int pw = iw / 2.2;
  int ph = ih / 2.2;
  int px = cx + (int)e.pupilX - (pw / 2);
  int py = cy + (int)e.pupilY - (ph / 2);
  if (px < ix)           px = ix;
  if (px + pw > ix + iw) px = ix + iw - pw;
  if (py < iy)           py = iy;
  if (py + ph > iy + ih) py = iy + ih - ph;
  display.fillRoundRect(px, py, pw, ph, r / 2, SSD1306_BLACK);
  if (iw > 15 && ih > 15)
    display.fillCircle(px + pw - 4, py + 4, 2, SSD1306_WHITE);
  drawEyelidMask(e.x, e.y, e.w, e.h, currentMood, isLeft);
}

void updatePhysicsAndMood() {
  unsigned long now = millis();
  breathVal = sin(now / 800.0) * 1.5;
  if (now > leftEye.nextBlinkTime) {
    leftEye.blinking = rightEye.blinking = true;
    leftEye.lastBlink = now;
    leftEye.nextBlinkTime = now + random(2000, 6000);
  }
  if (leftEye.blinking) {
    leftEye.targetH = rightEye.targetH = 2;
    if (now - leftEye.lastBlink > 120)
      leftEye.blinking = rightEye.blinking = false;
  }
  if (!leftEye.blinking && now - lastSaccade > saccadeInterval) {
    lastSaccade = now;
    saccadeInterval = random(500, 3000);
    int dir = random(0, 10);
    float lx = 0, ly = 0;
    if      (dir == 4) { lx = -6; ly = -4; }
    else if (dir == 5) { lx =  6; ly = -4; }
    else if (dir == 6) { lx = -6; ly =  4; }
    else if (dir == 7) { lx =  6; ly =  4; }
    else if (dir == 8) { lx =  8; ly =  0; }
    else if (dir == 9) { lx = -8; ly =  0; }
    leftEye.targetPupilX  = lx;
    leftEye.targetPupilY  = ly;
    rightEye.targetPupilX = lx;
    rightEye.targetPupilY = ly;
    leftEye.targetX  = 14 + (lx * 0.3);
    leftEye.targetY  = 10 + (ly * 0.3);
    rightEye.targetX = 72 + (lx * 0.3);
    rightEye.targetY = 10 + (ly * 0.3);
  }
  if (!leftEye.blinking) {
    float bW = 42, bH = 42 + breathVal;
    switch (currentMood) {
      case MOOD_NORMAL:
        leftEye.targetW  = bW; leftEye.targetH  = bH;
        rightEye.targetW = bW; rightEye.targetH = bH;
        break;
      case MOOD_HAPPY:
      case MOOD_LOVE:
        leftEye.targetW  = 40; leftEye.targetH  = 32;
        rightEye.targetW = 40; rightEye.targetH = 32;
        break;
      case MOOD_SURPRISED:
        leftEye.targetW  = 30; leftEye.targetH  = 45;
        rightEye.targetW = 30; rightEye.targetH = 45;
        leftEye.targetPupilX += random(-1, 2);
        break;
      case MOOD_SLEEPY:
        leftEye.targetW  = 38; leftEye.targetH  = 30;
        rightEye.targetW = 38; rightEye.targetH = 30;
        break;
      case MOOD_ANGRY:
        leftEye.targetW  = 34; leftEye.targetH  = 32;
        rightEye.targetW = 34; rightEye.targetH = 32;
        break;
      case MOOD_SAD:
        leftEye.targetW  = 34; leftEye.targetH  = 40;
        rightEye.targetW = 34; rightEye.targetH = 40;
        break;
      case MOOD_EXCITED:
        leftEye.targetW  = 40; leftEye.targetH  = 40;
        rightEye.targetW = 40; rightEye.targetH = 40;
        break;
      case MOOD_SUSPICIOUS:
        leftEye.targetW  = 36; leftEye.targetH  = 20;
        rightEye.targetW = 36; rightEye.targetH = 42;
        break;
    }
  }
  leftEye.update();
  rightEye.update();
}

void drawEmoPage() {
  updatePhysicsAndMood();
  if (currentMood == MOOD_LOVE)
    display.drawBitmap(56, 0, bmp_heart, 16, 16, SSD1306_WHITE);
  else if (currentMood == MOOD_SLEEPY)
    display.drawBitmap(110, 0, bmp_zzz, 16, 16, SSD1306_WHITE);
  else if (currentMood == MOOD_ANGRY)
    display.drawBitmap(56, 0, bmp_anger, 16, 16, SSD1306_WHITE);
  drawUltraProEye(leftEye, true);
  drawUltraProEye(rightEye, false);
}

// =================================================
// DISPLAY PAGES
// =================================================
void drawClock() {
  struct tm t;
  if (!getLocalTime(&t)) {
    display.setFont(NULL);
    display.setCursor(0, 20);
    display.print("No Time Sync");
    return;
  }
  display.setFont(&FreeSansBold18pt7b);
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", t.tm_hour, t.tm_min);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 40);
  display.print(timeStr);
  display.setFont(NULL);
  char dateStr[16];
  sprintf(dateStr, "%s %02d/%02d/%04d", diasSemana[t.tm_wday], t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
  display.getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 56);
  display.print(dateStr);
}

void drawLocalWeather() {
  display.setFont(NULL);
  display.setCursor(0, 0);
  display.print("-- LOCAL --");
  display.setFont(&FreeSansBold18pt7b);
  char tempStr[6];
  sprintf(tempStr, "%d C", (int)temperature);
  display.setCursor(0, 38);
  display.print(tempStr);
  display.setFont(NULL);
  display.setCursor(0, 55);
  display.print("Humedad: ");
  display.print((int)humidity);
  display.print("%");
}

void drawOnlineWeather() {
  display.setFont(NULL);
  display.setCursor(0, 0);
  display.print("-- ");
  display.print(city);
  display.print(" --");
  display.setFont(&FreeSansBold18pt7b);
  char tempStr[6];
  sprintf(tempStr, "%d C", (int)apiTemperature);
  display.setCursor(0, 38);
  display.print(tempStr);
  display.setFont(NULL);
  display.setCursor(0, 45);
  display.print(weatherDesc);
  display.setCursor(0, 56);
  display.print("ST:");
  display.print((int)feelsLike);
  display.print("C ");
  display.print(weatherUpdateTime); // hora última actualización
}

// =================================================
// BOOT ANIMATION
// =================================================
void bootAnimation() {
  // Restaurar brillo normal al arrancar
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(127);

  leftEye.h  = rightEye.h  = 2;
  leftEye.velH = rightEye.velH = 0;
  leftEye.targetH = rightEye.targetH = 2;
  display.clearDisplay();
  drawUltraProEye(leftEye, true);
  drawUltraProEye(rightEye, false);
  display.display();
  delay(400);
  leftEye.targetH = rightEye.targetH = 42;
  for (int i = 0; i < 55; i++) {
    leftEye.update();
    rightEye.update();
    display.clearDisplay();
    drawUltraProEye(leftEye, true);
    drawUltraProEye(rightEye, false);
    display.display();
    delay(25);
  }
}

// =================================================
// POMODORO
// =================================================
void updatePomodoro() {
  if (pomoState == POMO_IDLE) return;
  unsigned long elapsed = millis() - pomoStart;
  if (pomoState == POMO_WORK && elapsed >= POMO_WORK_MS) {
    pomoState = POMO_BREAK;
    pomoStart = millis();
    currentMood = MOOD_SLEEPY;
  } else if (pomoState == POMO_BREAK && elapsed >= POMO_BREAK_MS) {
    pomoRound++;
    pomoState = POMO_WORK;
    pomoStart = millis();
    currentMood = MOOD_EXCITED;
  }
}

void drawPomodoro() {
  display.setFont(NULL);
  display.setCursor(0, 0);
  if (pomoState == POMO_IDLE) {
    display.print("-- POMODORO --");
    display.setCursor(24, 22); display.print("Corta: iniciar");
    display.setCursor(24, 34); display.print("Larga:  salir");
    return;
  }
  unsigned long elapsed   = millis() - pomoStart;
  unsigned long totalMs   = (pomoState == POMO_WORK) ? POMO_WORK_MS : POMO_BREAK_MS;
  unsigned long remaining = totalMs - elapsed;
  unsigned long secs      = remaining / 1000;
  unsigned long mins      = secs / 60;
  secs %= 60;
  if (pomoState == POMO_WORK) {
    display.print("TRABAJO #"); display.print(pomoRound);
  } else {
    display.print("DESCANSO");
  }
  display.setFont(&FreeSansBold18pt7b);
  char timeStr[6];
  sprintf(timeStr, "%02lu:%02lu", mins, secs);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 44);
  display.print(timeStr);
  display.setFont(NULL);
  int prog = (int)((elapsed * 126UL) / totalMs);
  display.drawRect(0, 56, 128, 7, SSD1306_WHITE);
  display.fillRect(1, 57, prog, 5, SSD1306_WHITE);
}

// =================================================
// SETUP
// =================================================
void setup() {
  // Reducir CPU de 240MHz a 80MHz
  setCpuFrequencyMhz(80);

  // Watchdog: reinicia si se cuelga más de 30s
  const esp_task_wdt_config_t wdtCfg = { .timeout_ms = 30000, .idle_core_mask = 0, .trigger_panic = true };
  esp_task_wdt_reconfigure(&wdtCfg);
  esp_task_wdt_add(NULL);

  Serial.begin(115200);
  Serial.println("\n===== INICIO =====");
  Wire.begin(SDA_PIN, SCL_PIN);
  pinMode(TOUCH_PIN, INPUT_PULLDOWN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 no encontrado");
    for (;;);
  }
  display.setTextColor(SSD1306_WHITE);
  dht.begin();
  leftEye.init(14, 10, 42, 42);
  rightEye.init(72, 10, 42, 42);
  loadConfig();

  bool touchWake = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO);
  if (touchWake && hadNTPSync && isNightTime()) {
    Serial.println(">>> Wake nocturno por touch <<<");
    // Usar datos RTC sin conectar WiFi
    if (strlen(rtcWeatherMain) > 0) {
      apiTemperature = rtcApiTemp;
      feelsLike      = rtcFeelsLike;
      weatherMain    = String(rtcWeatherMain);
      weatherDesc    = String(rtcWeatherDesc);
    }
    nightWakeActive = true;
    nightWakeStart  = millis();
    bootAnimation();
    return;
  }
  if (!connectWiFi()) {
    startConfigPortal();
    return;
  }
  initTime();
  hadNTPSync = true;
  getWeather(); // WiFi se apaga dentro de getWeather()
  lastWeatherUpdate = millis();
  lastActivityTime  = millis();
  bootAnimation();
  if (isNightTime() && !touchWake) {
    goToDeepSleep();
  }
}

// =================================================
// LOOP
// =================================================
void loop() {
  esp_task_wdt_reset(); // alimentar watchdog

  if (inConfigMode) {
    server.handleClient();
    return;
  }

  unsigned long now = millis();
  bool touch = digitalRead(TOUCH_PIN);

  if (touch && !lastTouch) {
    touchStartTime = now;
    touchHandled   = false;
    // Si la pantalla estaba apagada, solo encenderla y consumir el toque
    if (!screenOn) {
      display.ssd1306_command(SSD1306_DISPLAYON);
      // Restaurar brillo normal
      display.ssd1306_command(SSD1306_SETCONTRAST);
      display.ssd1306_command(127);
      screenOn = true;
      lastActivityTime = now;
      lastAutoRotate   = now;
      touchHandled = true;
    }
  }
  if (touch) lastActivityTime = now;

  if (touch && !touchHandled && now - touchStartTime > LONG_PRESS_MS) {
    touchHandled = true;
    if (currentPage == 4) {
      currentPage = 0;
      pomoState   = POMO_IDLE;
      checkNightMode();
    } else {
      currentPage = 4;
      pomoState   = POMO_IDLE;
    }
    lastAutoRotate = now;
  }
  if (!touch && lastTouch && !touchHandled) {
    if (now - touchStartTime < LONG_PRESS_MS && now - lastTouchTime > TOUCH_DEBOUNCE_MS) {
      lastTouchTime  = now;
      lastAutoRotate = now;
      if (currentPage == 4) {
        if (pomoState == POMO_IDLE) {
          pomoState   = POMO_WORK;
          pomoStart   = now;
          pomoRound   = 1;
          currentMood = MOOD_EXCITED;
        } else {
          pomoState = POMO_IDLE;
          checkNightMode();
        }
      } else {
        currentPage = (currentPage + 1) % TOTAL_PAGES;
        lastSaccade = 0;
      }
    }
    touchHandled = true;
  }
  lastTouch = touch;

  if (currentPage < 4 && now - lastAutoRotate > AUTO_ROTATE_MS) {
    currentPage    = (currentPage + 1) % TOTAL_PAGES;
    lastAutoRotate = now;
    lastSaccade    = 0;
  }

  // Solo leer DHT si la pantalla está encendida
  if (screenOn && now - lastDHTRead > 2000) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) { temperature = t; humidity = h; }
    else Serial.println("Error DHT");
    lastDHTRead = now;
  }

  // Actualizar clima cada 4 horas
  if (now - lastWeatherUpdate > WEATHER_UPDATE_MS) {
    getWeather();
    lastWeatherUpdate = now;
  }

  // Resync NTP cada 24 horas
  if (now - lastNTPSync > NTP_RESYNC_MS) {
    if (connectWiFi()) {
      initTime();
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      Serial.println(">>> NTP resync OK <<<");
    }
  }

  if (nightWakeActive && now - nightWakeStart > NIGHT_WAKE_MS) {
    goToDeepSleep();
  }
  if (now - lastMoodCheck > 60000) {
    if (currentPage != 4 || pomoState == POMO_IDLE) checkNightMode();
    lastMoodCheck = now;
    if (pomoState == POMO_IDLE && isNightTime()) goToDeepSleep();
  }

  // Timeout pantalla: fade y apagar si no hay actividad en 1 minuto
  if (screenOn && now - lastActivityTime > SCREEN_TIMEOUT_MS) {
    // Fade out suave antes de apagar
    for (int b = 127; b >= 0; b -= 16) {
      display.ssd1306_command(SSD1306_SETCONTRAST);
      display.ssd1306_command(b);
      delay(15);
    }
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    screenOn = false;
  }

  updatePomodoro();
  if (!screenOn) {
    // Light sleep 33ms en lugar de delay (ahorra ~15mA adicionales)
    esp_sleep_enable_timer_wakeup(33 * 1000);
    esp_light_sleep_start();
    return;
  }

  display.clearDisplay();
  switch (currentPage) {
    case 0: drawEmoPage();       break;
    case 1: drawClock();         break;
    case 2: drawLocalWeather();  break;
    case 3: drawOnlineWeather(); break;
    case 4: drawPomodoro();      break;
  }
  display.display();

  // ~30fps, libera CPU entre frames
  delay(33);
}
