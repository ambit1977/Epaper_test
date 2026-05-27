/*
 * Dual-size Weather Display (2.9" / 4.2")
 * Auto-detects the connected WeAct E-Paper panel by measuring full-update timing,
 * then renders an appropriate layout for the detected panel.
 *
 * Hardware (same wiring for both 2.9" and 4.2"):
 *   BUSY: GPIO4 (purple)
 *   RES:  GPIO17 (orange)
 *   D/C:  GPIO16 (white)
 *   CS:   GPIO23 (blue)
 *   SCL:  GPIO5 (green)
 *   SDA:  GPIO18 (yellow)
 *   VCC:  3.3V
 *   GND:  GND
 */

#define ENABLE_GxEPD2_GFX 1
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <GxEPD2_3C.h>
#include <GxEPD2_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <time.h>
#include <esp_sleep.h>

#include "config.h"

// ============================================================================
// Settings
// ============================================================================
const float LAT = 35.6895;
const float LON = 139.6917;
const char* CITY = "Tokyo";
const uint64_t SLEEP_MINUTES = 10;

// Pins
#define EPAPER_BUSY 4
#define EPAPER_RST  17
#define EPAPER_DC   16
#define EPAPER_CS   23
#define SPI_CLK     5
#define SPI_MOSI    18
#define SPI_MISO    19

// Panel detection
#define PANEL_UNKNOWN 0
#define PANEL_29      1
#define PANEL_42      2
RTC_DATA_ATTR int detected_panel = PANEL_UNKNOWN;

// Drivers
GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT> d29(
  GxEPD2_290_C90c(EPAPER_CS, EPAPER_DC, EPAPER_RST, EPAPER_BUSY)
);
GxEPD2_3C<GxEPD2_420c_GDEY042Z98, GxEPD2_420c_GDEY042Z98::HEIGHT> d42(
  GxEPD2_420c_GDEY042Z98(EPAPER_CS, EPAPER_DC, EPAPER_RST, EPAPER_BUSY)
);
U8G2_FOR_ADAFRUIT_GFX u8g2;

// ============================================================================
// Weather data
// ============================================================================
struct DayForecast {
  String date;
  int    code;
  float  tmax;
  float  tmin;
  int    rain_pct;
};

const int MAX_DAYS = 6;
const int MAX_HOURS = 24;

float       current_temp = 0;
int         current_humid = 0;
int         current_code = 0;
DayForecast forecasts[MAX_DAYS];
int         forecast_count = 0;
float       hourly_temps[MAX_HOURS];
int         hourly_count = 0;
String      update_time = "";
bool        data_ok = false;

int round10(int v) { return ((v + 5) / 10) * 10; }

const char* weatherDescJP(int code) {
  if (code == 0) return "晴";
  if (code == 1) return "晴";
  if (code == 2) return "晴/雲";
  if (code == 3) return "雲";
  if (code >= 45 && code <= 48) return "霧";
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
// Icon drawing (templated on display reference)
// ============================================================================
void drawSun(GxEPD2_GFX& disp, int cx, int cy, int r, uint16_t color) {
  disp.fillCircle(cx, cy, r, color);
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4.0;
    int x1 = cx + cos(a) * (r + 2);
    int y1 = cy + sin(a) * (r + 2);
    int x2 = cx + cos(a) * (r + 6);
    int y2 = cy + sin(a) * (r + 6);
    disp.drawLine(x1, y1, x2, y2, color);
  }
}

void drawCloud(GxEPD2_GFX& disp, int cx, int cy, int size, uint16_t color) {
  int r = size / 4;
  disp.fillCircle(cx - size/3, cy + r/2, r, color);
  disp.fillCircle(cx, cy - r/2, r + 1, color);
  disp.fillCircle(cx + size/3, cy + r/2, r, color);
  disp.fillRect(cx - size/3, cy + r/2, (size/3)*2, r, color);
}

void drawRain(GxEPD2_GFX& disp, int cx, int cy, int size) {
  drawCloud(disp, cx, cy - size/4, size, GxEPD_BLACK);
  for (int i = -1; i <= 1; i++) {
    int x = cx + i * size/4;
    int y = cy + size/4;
    disp.drawLine(x, y, x - 2, y + 5, GxEPD_RED);
    disp.drawLine(x + 1, y, x - 1, y + 5, GxEPD_RED);
  }
}

void drawSnow(GxEPD2_GFX& disp, int cx, int cy, int size) {
  drawCloud(disp, cx, cy - size/4, size, GxEPD_BLACK);
  for (int i = -1; i <= 1; i++) {
    int x = cx + i * size/4;
    int y = cy + size/3;
    disp.drawPixel(x, y, GxEPD_BLACK);
    disp.drawPixel(x - 1, y, GxEPD_BLACK);
    disp.drawPixel(x + 1, y, GxEPD_BLACK);
    disp.drawPixel(x, y - 1, GxEPD_BLACK);
    disp.drawPixel(x, y + 1, GxEPD_BLACK);
  }
}

void drawPartlyCloudy(GxEPD2_GFX& disp, int cx, int cy, int size) {
  drawSun(disp, cx - size/3, cy - size/4, size/4, GxEPD_RED);
  drawCloud(disp, cx + size/6, cy + size/6, size, GxEPD_BLACK);
}

void drawThunder(GxEPD2_GFX& disp, int cx, int cy, int size) {
  drawCloud(disp, cx, cy - size/4, size, GxEPD_BLACK);
  int bx = cx - 2, by = cy + size/6;
  disp.drawLine(bx + 2, by, bx, by + 4, GxEPD_RED);
  disp.drawLine(bx, by + 4, bx + 4, by + 4, GxEPD_RED);
  disp.drawLine(bx + 4, by + 4, bx + 1, by + 9, GxEPD_RED);
}

void drawWeatherIcon(GxEPD2_GFX& disp, int cx, int cy, int size, int code) {
  if (code == 0 || code == 1) {
    drawSun(disp, cx, cy, size/3, GxEPD_RED);
  } else if (code == 2) {
    drawPartlyCloudy(disp, cx, cy, size);
  } else if (code == 3 || (code >= 45 && code <= 48)) {
    drawCloud(disp, cx, cy, size, GxEPD_BLACK);
  } else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    drawRain(disp, cx, cy, size);
  } else if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
    drawSnow(disp, cx, cy, size);
  } else if (code >= 95) {
    drawThunder(disp, cx, cy, size);
  } else {
    disp.drawRect(cx - size/2, cy - size/2, size, size, GxEPD_BLACK);
  }
}

// ============================================================================
// WiFi + NTP
// ============================================================================
bool connectWiFi() {
  Serial.print("WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 30; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf(" OK %s\n", WiFi.localIP().toString().c_str());
      return true;
    }
    delay(1000);
    Serial.print(".");
  }
  Serial.println(" failed");
  return false;
}

void syncTime() {
  configTime(9 * 3600, 0, "pool.ntp.org", "time.google.com", "ntp.nict.jp");
  Serial.print("NTP");
  for (int i = 0; i < 40; i++) {
    time_t now = time(nullptr);
    if (now > 1700000000) {
      struct tm t;
      localtime_r(&now, &t);
      char buf[16];
      snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
      update_time = String(buf);
      Serial.println(" " + update_time);
      return;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println(" fail");
  update_time = "--:--";
}

bool fetchWeather(int forecast_days) {
  String url = "http://api.open-meteo.com/v1/forecast";
  url += "?latitude=" + String(LAT, 4);
  url += "&longitude=" + String(LON, 4);
  url += "&current=temperature_2m,weather_code,relative_humidity_2m";
  url += "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max";
  url += "&hourly=temperature_2m";
  url += "&timezone=Asia%2FTokyo&forecast_days=" + String(forecast_days);

  Serial.println("GET");
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

  if (update_time == "--:--" || update_time == "") {
    const char* iso = doc["current"]["time"];
    if (iso && strlen(iso) >= 16) {
      String s = String(iso);
      update_time = s.substring(11, 16);
    }
  }

  JsonArray dates = doc["daily"]["time"];
  JsonArray codes = doc["daily"]["weather_code"];
  JsonArray tmax  = doc["daily"]["temperature_2m_max"];
  JsonArray tmin  = doc["daily"]["temperature_2m_min"];
  JsonArray rain  = doc["daily"]["precipitation_probability_max"];

  forecast_count = min((int)dates.size(), MAX_DAYS);
  for (int i = 0; i < forecast_count; i++) {
    forecasts[i].date     = dates[i].as<String>();
    forecasts[i].code     = codes[i].as<int>();
    forecasts[i].tmax     = tmax[i].as<float>();
    forecasts[i].tmin     = tmin[i].as<float>();
    forecasts[i].rain_pct = rain[i].as<int>();
  }

  JsonArray htemps = doc["hourly"]["temperature_2m"];
  hourly_count = min((int)htemps.size(), MAX_HOURS);
  for (int i = 0; i < hourly_count; i++) {
    hourly_temps[i] = htemps[i].as<float>();
  }

  Serial.printf("OK: %.1fC, %d days, %d hours\n",
                current_temp, forecast_count, hourly_count);
  return true;
}

// ============================================================================
// Panel detection
// ============================================================================
int detectPanel() {
  Serial.println("Detecting panel...");
  unsigned long t0 = millis();
  d42.init(115200, true, 50, false);
  d42.setRotation(0);
  d42.setFullWindow();
  d42.firstPage();
  do {
    d42.fillScreen(GxEPD_WHITE);
  } while (d42.nextPage());
  unsigned long elapsed = millis() - t0;
  d42.hibernate();
  Serial.printf("  4.2\" driver test: %lu ms\n", elapsed);
  int p = (elapsed >= 8000) ? PANEL_42 : PANEL_29;
  Serial.printf("  Detected: %s\n", (p == PANEL_42) ? "4.2-inch" : "2.9-inch");
  return p;
}

// ============================================================================
// 2.9" layout (296x128)
// ============================================================================
void drawLayout29() {
  d29.setRotation(1);
  d29.setFullWindow();
  d29.firstPage();
  do {
    d29.fillScreen(GxEPD_WHITE);
    u8g2.setFontMode(1);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setBackgroundColor(GxEPD_WHITE);

    if (!data_ok) {
      u8g2.setFont(u8g2_font_b12_b_t_japanese1);
      u8g2.setCursor(20, 60);
      u8g2.print("接続エラー");
      continue;
    }

    u8g2.setFont(u8g2_font_b10_b_t_japanese1);
    u8g2.setCursor(4, 13);
    u8g2.print(CITY);
    u8g2.setCursor(200, 13);
    u8g2.print("Update ");
    u8g2.print(update_time);
    d29.drawLine(0, 17, 296, 17, GxEPD_BLACK);

    drawWeatherIcon(d29, 40, 52, 36, current_code);
    u8g2.setFont(u8g2_font_logisoso30_tn);
    u8g2.setForegroundColor(GxEPD_RED);
    char tempBuf[8];
    snprintf(tempBuf, sizeof(tempBuf), "%.1f", current_temp);
    int tw = u8g2.getUTF8Width(tempBuf);
    u8g2.setCursor(84, 60);
    u8g2.print(tempBuf);

    u8g2.setFont(u8g2_font_b12_b_t_japanese1);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setCursor(84 + tw + 3, 48);
    u8g2.print("°C");

    const char* desc = weatherDescJP(current_code);
    int dw = u8g2.getUTF8Width(desc);
    int dx = 84 + (tw - dw) / 2;
    if (dx < 70) dx = 70;
    u8g2.setCursor(dx, 84);
    u8g2.print(desc);

    int rx = 212;
    u8g2.setFont(u8g2_font_b10_b_t_japanese1);
    u8g2.setCursor(rx, 33); u8g2.print("最高 ");
    u8g2.setForegroundColor(GxEPD_RED);
    u8g2.print((int)round(forecasts[0].tmax)); u8g2.print("°");
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setCursor(rx, 49); u8g2.print("最低 ");
    u8g2.setForegroundColor(GxEPD_RED);
    u8g2.print((int)round(forecasts[0].tmin)); u8g2.print("°");
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setCursor(rx, 67); u8g2.print("Rain ");
    u8g2.setForegroundColor(GxEPD_RED);
    u8g2.print(round10(forecasts[0].rain_pct)); u8g2.print("%");
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setCursor(rx, 83); u8g2.print("RH   ");
    u8g2.setForegroundColor(GxEPD_RED);
    u8g2.print(current_humid); u8g2.print("%");
    u8g2.setForegroundColor(GxEPD_BLACK);

    d29.drawLine(0, 90, 296, 90, GxEPD_BLACK);
    int show_days = min(forecast_count, 3);
    int col_w = 296 / show_days;
    for (int i = 0; i < show_days; i++) {
      int x0 = col_w * i;
      int cx = x0 + col_w / 2;
      const char* label = (i == 0) ? "今日" : (i == 1) ? "明日" : "明後日";
      u8g2.setFont(u8g2_font_b10_b_t_japanese1);
      int lw = u8g2.getUTF8Width(label);
      u8g2.setCursor(cx - lw/2, 104);
      u8g2.print(label);
      drawWeatherIcon(d29, x0 + 16, 119, 12, forecasts[i].code);
      char tbuf[12];
      u8g2.setForegroundColor(GxEPD_RED);
      snprintf(tbuf, sizeof(tbuf), "%d", (int)round(forecasts[i].tmax));
      u8g2.setCursor(x0 + 36, 117);
      u8g2.print(tbuf);
      u8g2.setForegroundColor(GxEPD_BLACK);
      u8g2.print("/");
      u8g2.print((int)round(forecasts[i].tmin));
      u8g2.setForegroundColor(GxEPD_RED);
      snprintf(tbuf, sizeof(tbuf), "%d%%", round10(forecasts[i].rain_pct));
      u8g2.setCursor(x0 + 36, 128);
      u8g2.print(tbuf);
      u8g2.setForegroundColor(GxEPD_BLACK);
    }
  } while (d29.nextPage());
  d29.hibernate();
}

// ============================================================================
// 4.2" layout (400x300)
// ============================================================================
void drawLayout42() {
  d42.setRotation(0);
  d42.setFullWindow();
  d42.firstPage();
  do {
    d42.fillScreen(GxEPD_WHITE);
    u8g2.setFontMode(1);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setBackgroundColor(GxEPD_WHITE);

    if (!data_ok) {
      u8g2.setFont(u8g2_font_b16_b_t_japanese1);
      u8g2.setCursor(30, 150);
      u8g2.print("接続エラー");
      continue;
    }

    // Header
    u8g2.setFont(u8g2_font_b16_b_t_japanese1);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setCursor(8, 22);
    u8g2.print(CITY);

    u8g2.setFont(u8g2_font_b12_b_t_japanese1);
    u8g2.setCursor(270, 22);
    u8g2.print("Update ");
    u8g2.print(update_time);
    d42.drawLine(0, 30, 400, 30, GxEPD_BLACK);

    // Main
    drawWeatherIcon(d42, 60, 85, 60, current_code);

    u8g2.setFont(u8g2_font_logisoso46_tn);
    u8g2.setForegroundColor(GxEPD_RED);
    char tempBuf[8];
    snprintf(tempBuf, sizeof(tempBuf), "%.1f", current_temp);
    u8g2.setCursor(130, 95);
    u8g2.print(tempBuf);
    int tw = u8g2.getUTF8Width(tempBuf);

    u8g2.setFont(u8g2_font_b16_b_t_japanese1);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setCursor(130 + tw + 5, 65);
    u8g2.print("°C");

    u8g2.setCursor(130 + tw + 5, 95);
    u8g2.print(weatherDescJP(current_code));

    // Right column
    int rx = 295;
    u8g2.setFont(u8g2_font_b12_b_t_japanese1);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setCursor(rx, 55); u8g2.print("最高 ");
    u8g2.setForegroundColor(GxEPD_RED);
    u8g2.print((int)round(forecasts[0].tmax)); u8g2.print("°");
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setCursor(rx, 75); u8g2.print("最低 ");
    u8g2.setForegroundColor(GxEPD_RED);
    u8g2.print((int)round(forecasts[0].tmin)); u8g2.print("°");
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setCursor(rx, 95); u8g2.print("Rain ");
    u8g2.setForegroundColor(GxEPD_RED);
    u8g2.print(round10(forecasts[0].rain_pct)); u8g2.print("%");
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setCursor(rx, 115); u8g2.print("RH   ");
    u8g2.setForegroundColor(GxEPD_RED);
    u8g2.print(current_humid); u8g2.print("%");
    u8g2.setForegroundColor(GxEPD_BLACK);

    // Hourly graph
    d42.drawLine(0, 130, 400, 130, GxEPD_BLACK);
    u8g2.setFont(u8g2_font_b10_b_t_japanese1);
    u8g2.setCursor(8, 145);
    u8g2.print("24h Temperature");

    if (hourly_count >= 24) {
      float hmin = hourly_temps[0], hmax = hourly_temps[0];
      for (int i = 0; i < 24; i++) {
        if (hourly_temps[i] < hmin) hmin = hourly_temps[i];
        if (hourly_temps[i] > hmax) hmax = hourly_temps[i];
      }
      if (hmax - hmin < 5) hmax = hmin + 5;

      int gx0 = 25, gx1 = 380;
      int gy0 = 155, gy1 = 200;
      int gw = gx1 - gx0;
      int gh = gy1 - gy0;

      // Y labels
      u8g2.setFont(u8g2_font_b10_b_t_japanese1);
      u8g2.setForegroundColor(GxEPD_BLACK);
      u8g2.setCursor(0, gy0 + 4);
      u8g2.print((int)round(hmax));
      u8g2.setCursor(0, gy1);
      u8g2.print((int)round(hmin));

      // Plot
      int prev_x = -1, prev_y = -1;
      for (int i = 0; i < 24; i++) {
        int x = gx0 + (gw * i) / 23;
        int y = gy1 - (int)((hourly_temps[i] - hmin) / (hmax - hmin) * gh);
        if (prev_x >= 0) {
          d42.drawLine(prev_x, prev_y, x, y, GxEPD_RED);
          d42.drawLine(prev_x, prev_y + 1, x, y + 1, GxEPD_RED);
        }
        d42.fillCircle(x, y, 1, GxEPD_BLACK);
        prev_x = x; prev_y = y;
      }

      // X labels
      u8g2.setCursor(gx0 - 3, gy1 + 12);
      u8g2.print("0");
      u8g2.setCursor(gx0 + gw/4 - 4, gy1 + 12);
      u8g2.print("6");
      u8g2.setCursor(gx0 + gw/2 - 6, gy1 + 12);
      u8g2.print("12");
      u8g2.setCursor(gx0 + 3*gw/4 - 6, gy1 + 12);
      u8g2.print("18");
      u8g2.setCursor(gx1 - 12, gy1 + 12);
      u8g2.print("24");
    }

    // Multi-day forecast
    d42.drawLine(0, 220, 400, 220, GxEPD_BLACK);
    int show_days = min(forecast_count, MAX_DAYS);
    int col_w = 400 / show_days;
    for (int i = 0; i < show_days; i++) {
      int x0 = col_w * i;
      int cx = x0 + col_w / 2;
      String label;
      if (i == 0) label = "今日";
      else if (i == 1) label = "明日";
      else if (i == 2) label = "明後日";
      else if (forecasts[i].date.length() >= 10) {
        label = forecasts[i].date.substring(5, 7) + "/" + forecasts[i].date.substring(8, 10);
      } else {
        label = String("D") + (i + 1);
      }
      u8g2.setFont(u8g2_font_b10_b_t_japanese1);
      u8g2.setForegroundColor(GxEPD_BLACK);
      int lw = u8g2.getUTF8Width(label.c_str());
      u8g2.setCursor(cx - lw/2, 238);
      u8g2.print(label);

      drawWeatherIcon(d42, cx, 260, 20, forecasts[i].code);

      u8g2.setFont(u8g2_font_b10_b_t_japanese1);
      u8g2.setForegroundColor(GxEPD_RED);
      char tbuf[12];
      snprintf(tbuf, sizeof(tbuf), "%d", (int)round(forecasts[i].tmax));
      int tw1 = u8g2.getUTF8Width(tbuf);
      u8g2.setCursor(cx - tw1 - 6, 285);
      u8g2.print(tbuf);
      u8g2.setForegroundColor(GxEPD_BLACK);
      u8g2.print("/");
      u8g2.print((int)round(forecasts[i].tmin));

      u8g2.setForegroundColor(GxEPD_RED);
      snprintf(tbuf, sizeof(tbuf), "%d%%", round10(forecasts[i].rain_pct));
      int tw2 = u8g2.getUTF8Width(tbuf);
      u8g2.setCursor(cx - tw2/2, 298);
      u8g2.print(tbuf);
      u8g2.setForegroundColor(GxEPD_BLACK);
    }
  } while (d42.nextPage());
  d42.hibernate();
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("=== Dual Weather Display ===");

  SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, EPAPER_CS);

  if (detected_panel == PANEL_UNKNOWN) {
    detected_panel = detectPanel();
  } else {
    Serial.printf("Cached panel: %s\n",
                  (detected_panel == PANEL_42) ? "4.2" : "2.9");
  }

  if (detected_panel == PANEL_42) {
    d42.init(115200, true, 50, false);
    u8g2.begin(d42);
  } else {
    d29.init(115200, true, 50, false);
    u8g2.begin(d29);
  }
  u8g2.setFontMode(1);

  int days = (detected_panel == PANEL_42) ? 6 : 3;

  if (connectWiFi()) {
    syncTime();
    data_ok = fetchWeather(days);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  if (detected_panel == PANEL_42) {
    drawLayout42();
  } else {
    drawLayout29();
  }

  Serial.printf("Sleep %llu min\n", SLEEP_MINUTES);
  Serial.flush();
  esp_sleep_enable_timer_wakeup(SLEEP_MINUTES * 60ULL * 1000000ULL);
  esp_deep_sleep_start();
}

void loop() {}
