/*
 * WeAct 2.9" E-Paper Module Test (GxEPD2 Library)
 *
 * Display: WeAct 2.9" BWR 128x296, SSD1680
 *
 * IMPORTANT: This module's JST cable colors don't match silk-screen.
 * Verified by loopback testing - the physical wiring is:
 *   GPIO4  (purple) -> BUSY (PIN 1)
 *   GPIO17 (orange) -> RES  (PIN 2)
 *   GPIO16 (white)  -> D/C  (PIN 3)
 *   GPIO23 (blue)   -> CS   (PIN 4)
 *   GPIO5  (green)  -> SCL  (PIN 5)
 *   GPIO18 (yellow) -> SDA  (PIN 6)
 */

#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>

// Pin definitions matched to actual E-Paper signal routing
#define EPAPER_BUSY 4
#define EPAPER_RST  17
#define EPAPER_DC   16
#define EPAPER_CS   23
#define SPI_CLK     5    // E-Paper SCL is wired to GPIO5
#define SPI_MOSI    18   // E-Paper SDA is wired to GPIO18
#define SPI_MISO    19   // unused

GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT> display(
  GxEPD2_290_C90c(EPAPER_CS, EPAPER_DC, EPAPER_RST, EPAPER_BUSY)
);

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("==========================================");
  Serial.println(" WeAct 2.9\" E-Paper (FULL REMAP)");
  Serial.println("==========================================");
  Serial.print(" CS  =GPIO");  Serial.println(EPAPER_CS);
  Serial.print(" D/C =GPIO");  Serial.println(EPAPER_DC);
  Serial.print(" RES =GPIO");  Serial.println(EPAPER_RST);
  Serial.print(" BUSY=GPIO");  Serial.println(EPAPER_BUSY);
  Serial.print(" SCL =GPIO");  Serial.println(SPI_CLK);
  Serial.print(" SDA =GPIO");  Serial.println(SPI_MOSI);
  Serial.println("==========================================");

  Serial.print("[INIT] BUSY initial state: ");
  pinMode(EPAPER_BUSY, INPUT);
  Serial.println(digitalRead(EPAPER_BUSY));

  // Initialize SPI with custom pins
  SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, EPAPER_CS);

  // Initialize display
  display.init(115200, true, 50, false);

  // Override SPI to use our custom pin assignment
  display.epd2.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));

  display.setRotation(1);

  Serial.println("[DRAW] Drawing Hello World...");
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBold24pt7b);
    display.setCursor(20, 50);
    display.print("Hello!");

    display.setTextColor(GxEPD_RED);
    display.setFont(&FreeMonoBold24pt7b);
    display.setCursor(20, 100);
    display.print("WORKING");

    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(20, 125);
    display.print("remap fix v2");
  } while (display.nextPage());

  Serial.println("[DONE] Display updated");
  display.hibernate();
}

void loop() {
  delay(1000);
}
