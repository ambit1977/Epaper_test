/*
 * Today's Weather Display - JP version with icons & forecast
 * WeAct 2.9" E-Paper + ESP32 + Open-Meteo API
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <GxEPD2_3C.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <time.h>
#include "config.h"   // <-- copy config.h.example to config.h and edit

// =============================================
//  General settings
// =============================================
const float LAT = 35.6895;
const float LON = 139.6917;
const char* CITY = "Tokyo";
const int FORECAST_DAYS = 3;             // today + 2 future
const uint64_t SLEEP_MINUTES = 10;       // refresh interval
// =============================================

// E-Paper pins (verified for this WeAct module)
#define EPAPER_BUSY 4
#define EPAPER_RST  17
#define EPAPER_DC   16
#define EPAPER_CS   23
#define SPI_CLK     5
#define SPI_MOSI    18
#define SPI_MISO    19

GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT> display(
  GxEPD2_290_C90c(EPAPER_CS, EPAPER_DC, EPAPER_RST, EPAPER_BUSY)
);
U8G2_FOR_ADAFRUIT_GFX u8g2;

// Weather data
struct DayForecast {
  String date;       // "2026-05-27"
  int    code;       // WMO
  float  tmax;
  float  tmin;
  int    rain_pct;
};

float        current_temp = 0;
int          current_humid = 0;     // %
int          current_code = 0;
DayForecast  forecasts[5];
int          forecast_count = 0;
String       update_time = "";
bool         data_ok = false;

// Round to nearest 10% (JMA style)
int round10(int v) { return ((v + 5) / 10) * 10; }

// ============================================================================
// Weather code -> JP description
// ============================================================================
// Weather description using only edu-kanji (japanese1 compatible)
const char* weatherDescJP(int code) {
  if (code == 0) return "晴";
  if (code == 1) return "晴";
  if (code == 2) return "晴/雲";
  if (code == 3) return "雲";
  if (code >= 45 && code <= 48) return "霧";       // may need fallback
  if (code >= 51 && code <= 57) return "小雨";
  if (code >= 61 && code <= 65) return "雨";
  if (code >= 66 && code <= 67) return "雨";
  if (code >= 71 && code <= 77) return "雪";
  if (code >= 80 && code <= 82) return "雨";
  if (code >= 85 && code <= 86) return "雪";
  if (code == 95) return "雷雨";
  if (code >= 96 && code <= 99) return "雷雨";
  return "?";
}

// ============================================================================
// Icon drawing primitives (procedurally drawn, no bitmaps)
// ============================================================================
void drawSun(int cx, int cy, int r, uint16_t color) {
  display.fillCircle(cx, cy, r, color);
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4.0;
    int x1 = cx + cos(a) * (r + 2);
    int y1 = cy + sin(a) * (r + 2);
    int x2 = cx + cos(a) * (r + 6);
    int y2 = cy + sin(a) * (r + 6);
    display.drawLine(x1, y1, x2, y2, color);
  }
}

void drawCloud(int cx, int cy, int size, uint16_t color) {
  int r = size / 4;
  display.fillCircle(cx - size/3, cy + r/2, r, color);
  display.fillCircle(cx, cy - r/2, r + 1, color);
  display.fillCircle(cx + size/3, cy + r/2, r, color);
  display.fillRect(cx - size/3, cy + r/2, (size/3)*2, r, color);
}

void drawRain(int cx, int cy, int size) {
  drawCloud(cx, cy - size/4, size, GxEPD_BLACK);
  // raindrops
  for (int i = -1; i <= 1; i++) {
    int x = cx + i * size/4;
    int y = cy + size/4;
    display.drawLine(x, y, x - 2, y + 5, GxEPD_RED);
    display.drawLine(x + 1, y, x - 1, y + 5, GxEPD_RED);
  }
}

void drawSnow(int cx, int cy, int size) {
  drawCloud(cx, cy - size/4, size, GxEPD_BLACK);
  // snowflakes
  for (int i = -1; i <= 1; i++) {
    int x = cx + i * size/4;
    int y = cy + size/3;
    display.drawPixel(x, y, GxEPD_BLACK);
    display.drawPixel(x - 1, y, GxEPD_BLACK);
    display.drawPixel(x + 1, y, GxEPD_BLACK);
    display.drawPixel(x, y - 1, GxEPD_BLACK);
    display.drawPixel(x, y + 1, GxEPD_BLACK);
  }
}

void drawPartlyCloudy(int cx, int cy, int size) {
  drawSun(cx - size/3, cy - size/4, size/4, GxEPD_RED);
  drawCloud(cx + size/6, cy + size/6, size, GxEPD_BLACK);
}

void drawThunder(int cx, int cy, int size) {
  drawCloud(cx, cy - size/4, size, GxEPD_BLACK);
  // lightning bolt
  int bx = cx - 2, by = cy + size/6;
  display.drawLine(bx + 2, by,     bx,     by + 4, GxEPD_RED);
  display.drawLine(bx,     by + 4, bx + 4, by + 4, GxEPD_RED);
  display.drawLine(bx + 4, by + 4, bx + 1, by + 9, GxEPD_RED);
}

void drawWeatherIcon(int cx, int cy, int size, int code) {
  if (code == 0 || code == 1) {
    drawSun(cx, cy, size/3, GxEPD_RED);
  } else if (code == 2) {
    drawPartlyCloudy(cx, cy, size);
  } else if (code == 3 || (code >= 45 && code <= 48)) {
    drawCloud(cx, cy, size, GxEPD_BLACK);
  } else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    drawRain(cx, cy, size);
  } else if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
    drawSnow(cx, cy, size);
  } else if (code >= 95) {
    drawThunder(cx, cy, size);
  } else {
    display.drawRect(cx - size/2, cy - size/2, size, size, GxEPD_BLACK);
  }
}

// ============================================================================
// WiFi & API
// ============================================================================
bool connectWiFi() {
  Serial.print("Connecting WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 30; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println();
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      return true;
    }
    delay(1000);
    Serial.print(".");
  }
  Serial.println(" FAILED");
  return false;
}

void syncTime() {
  // Try multiple NTP servers, longer retry
  configTime(9 * 3600, 0, "pool.ntp.org", "time.google.com", "ntp.nict.jp");
  Serial.print("Syncing time");
  for (int i = 0; i < 40; i++) {  // up to 20 sec
    time_t now = time(nullptr);
    if (now > 1700000000) {
      struct tm t;
      localtime_r(&now, &t);
      char buf[16];
      snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
      update_time = String(buf);
      Serial.println(" done: " + update_time);
      return;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println(" failed");
  update_time = "--:--";
}

bool fetchWeather() {
  String url = "http://api.open-meteo.com/v1/forecast";
  url += "?latitude=" + String(LAT, 4);
  url += "&longitude=" + String(LON, 4);
  url += "&current=temperature_2m,weather_code,relative_humidity_2m";
  url += "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max";
  url += "&timezone=Asia%2FTokyo&forecast_days=" + String(FORECAST_DAYS);

  Serial.println("GET " + url);
  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("HTTP %d\n", code);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;

  current_temp  = doc["current"]["temperature_2m"].as<float>();
  current_code  = doc["current"]["weather_code"].as<int>();
  current_humid = doc["current"]["relative_humidity_2m"].as<int>();

  // Fallback time from API if NTP failed: "2026-05-27T11:00" -> "11:00"
  if (update_time == "--:--" || update_time == "") {
    const char* iso = doc["current"]["time"];
    if (iso && strlen(iso) >= 16) {
      String s = String(iso);
      update_time = s.substring(11, 16);
      Serial.println("Time from API: " + update_time);
    }
  }

  JsonArray dates = doc["daily"]["time"];
  JsonArray codes = doc["daily"]["weather_code"];
  JsonArray tmax  = doc["daily"]["temperature_2m_max"];
  JsonArray tmin  = doc["daily"]["temperature_2m_min"];
  JsonArray rain  = doc["daily"]["precipitation_probability_max"];

  forecast_count = min((int)dates.size(), 5);
  for (int i = 0; i < forecast_count; i++) {
    forecasts[i].date     = dates[i].as<String>();
    forecasts[i].code     = codes[i].as<int>();
    forecasts[i].tmax     = tmax[i].as<float>();
    forecasts[i].tmin     = tmin[i].as<float>();
    forecasts[i].rain_pct = rain[i].as<int>();
  }

  Serial.printf("Now: %.1fC code=%d, %d forecast days\n",
                current_temp, current_code, forecast_count);
  return true;
}

// ============================================================================
// Date helper: "2026-05-27" -> "05/27 (水)"
// ============================================================================
String formatDate(const String& iso) {
  if (iso.length() < 10) return iso;
  String mm = iso.substring(5, 7);
  String dd = iso.substring(8, 10);

  // Day of week (Zeller-like)
  int y = iso.substring(0, 4).toInt();
  int m = mm.toInt();
  int d = dd.toInt();
  if (m < 3) { m += 12; y--; }
  int dow = (d + (13 * (m + 1)) / 5 + y + y/4 - y/100 + y/400) % 7;
  // dow: 0=Sat, 1=Sun, 2=Mon ...
  const char* names[] = {"土", "日", "月", "火", "水", "木", "金"};
  return mm + "/" + dd + "(" + names[dow] + ")";
}

String shortDate(const String& iso) {
  if (iso.length() < 10) return iso;
  return iso.substring(5);  // "05-27"
}

// ============================================================================
// Drawing
// ============================================================================
void drawDisplay() {
  display.setRotation(1);  // 296x128 landscape
  display.setFullWindow();

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2.setFontMode(1);  // transparent
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setBackgroundColor(GxEPD_WHITE);

    if (!data_ok) {
      u8g2.setFont(u8g2_font_b12_b_t_japanese1);
      u8g2.setCursor(20, 60);
      u8g2.print("接続エラー");
      continue;
    }

    // === Header (y: 0-16) ===
    u8g2.setFont(u8g2_font_b10_b_t_japanese1);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setCursor(4, 13);
    u8g2.print(CITY);
    u8g2.setCursor(200, 13);
    u8g2.print("Update ");
    u8g2.print(update_time);
    display.drawLine(0, 17, 296, 17, GxEPD_BLACK);

    // === Main area (y: 17-87) ===
    // Big weather icon on the left
    drawWeatherIcon(40, 52, 36, current_code);

    // Big temperature, red
    u8g2.setFont(u8g2_font_logisoso30_tn);
    u8g2.setForegroundColor(GxEPD_RED);
    char tempBuf[8];
    snprintf(tempBuf, sizeof(tempBuf), "%.1f", current_temp);
    int tw = u8g2.getUTF8Width(tempBuf);
    int tx = 84;
    u8g2.setCursor(tx, 60);
    u8g2.print(tempBuf);

    // °C
    u8g2.setFont(u8g2_font_b12_b_t_japanese1);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setCursor(tx + tw + 3, 48);
    u8g2.print("°C");

    // Weather description below temperature
    u8g2.setFont(u8g2_font_b12_b_t_japanese1);
    u8g2.setForegroundColor(GxEPD_BLACK);
    const char* desc = weatherDescJP(current_code);
    int dw = u8g2.getUTF8Width(desc);
    int dx = tx + (tw - dw) / 2;
    if (dx < 70) dx = 70;
    u8g2.setCursor(dx, 84);
    u8g2.print(desc);

    // Right column: today's high/low/rain/humidity
    if (forecast_count > 0) {
      int rx = 212;
      u8g2.setFont(u8g2_font_b10_b_t_japanese1);

      u8g2.setForegroundColor(GxEPD_BLACK);
      u8g2.setCursor(rx, 33);
      u8g2.print("最高 ");
      u8g2.setForegroundColor(GxEPD_RED);
      u8g2.print((int)round(forecasts[0].tmax));
      u8g2.print("°");

      u8g2.setForegroundColor(GxEPD_BLACK);
      u8g2.setCursor(rx, 49);
      u8g2.print("最低 ");
      u8g2.setForegroundColor(GxEPD_RED);
      u8g2.print((int)round(forecasts[0].tmin));
      u8g2.print("°");

      u8g2.setForegroundColor(GxEPD_BLACK);
      u8g2.setCursor(rx, 67);
      u8g2.print("Rain ");
      u8g2.setForegroundColor(GxEPD_RED);
      u8g2.print(round10(forecasts[0].rain_pct));
      u8g2.print("%");

      u8g2.setForegroundColor(GxEPD_BLACK);
      u8g2.setCursor(rx, 83);
      u8g2.print("RH   ");
      u8g2.setForegroundColor(GxEPD_RED);
      u8g2.print(current_humid);
      u8g2.print("%");
      u8g2.setForegroundColor(GxEPD_BLACK);
    }

    // === Separator ===
    display.drawLine(0, 90, 296, 90, GxEPD_BLACK);

    // === Multi-day forecast (y: 91-128) ===
    int col_w = 296 / forecast_count;
    for (int i = 0; i < forecast_count; i++) {
      int x0 = col_w * i;
      int cx = x0 + col_w / 2;

      // Day label centered
      u8g2.setFont(u8g2_font_b10_b_t_japanese1);
      u8g2.setForegroundColor(GxEPD_BLACK);
      const char* label = (i == 0) ? "今日" : (i == 1) ? "明日" : "明後日";
      int lw = u8g2.getUTF8Width(label);
      u8g2.setCursor(cx - lw / 2, 104);
      u8g2.print(label);

      // Icon
      drawWeatherIcon(x0 + 16, 119, 12, forecasts[i].code);

      // Temp max/min
      u8g2.setFont(u8g2_font_b10_b_t_japanese1);
      u8g2.setForegroundColor(GxEPD_RED);
      char tbuf[12];
      snprintf(tbuf, sizeof(tbuf), "%d", (int)round(forecasts[i].tmax));
      u8g2.setCursor(x0 + 36, 117);
      u8g2.print(tbuf);
      u8g2.setForegroundColor(GxEPD_BLACK);
      u8g2.print("/");
      u8g2.print((int)round(forecasts[i].tmin));

      // Rain% (rounded to 10%)
      u8g2.setForegroundColor(GxEPD_RED);
      snprintf(tbuf, sizeof(tbuf), "%d%%", round10(forecasts[i].rain_pct));
      u8g2.setCursor(x0 + 36, 128);
      u8g2.print(tbuf);
      u8g2.setForegroundColor(GxEPD_BLACK);
    }
  } while (display.nextPage());
}

// ============================================================================
// Setup / Loop
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("=== Weather Display (JP) ===");

  SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, EPAPER_CS);
  display.init(115200, true, 50, false);
  display.epd2.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  u8g2.begin(display);
  u8g2.setFontMode(1);
  u8g2.setFontDirection(0);

  if (connectWiFi()) {
    syncTime();
    data_ok = fetchWeather();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  drawDisplay();
  display.hibernate();

  // Sleep then wake up to refresh
  uint64_t sleep_us = SLEEP_MINUTES * 60ULL * 1000000ULL;
  Serial.printf("Going to deep sleep for %llu minutes...\n", SLEEP_MINUTES);
  Serial.flush();
  esp_sleep_enable_timer_wakeup(sleep_us);
  esp_deep_sleep_start();
}

void loop() {
  // Never reached (deep sleep restarts via setup)
}
