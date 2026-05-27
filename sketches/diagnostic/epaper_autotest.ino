/*
 * Auto-test #12: IL3820 BW (legacy)
 */
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>

#define EPAPER_CS   5
#define EPAPER_DC   17
#define EPAPER_RST  16
#define EPAPER_BUSY 4

GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT> display(GxEPD2_290(EPAPER_CS, EPAPER_DC, EPAPER_RST, EPAPER_BUSY));

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("=== AUTO-TEST #12 ===");
  Serial.println("Class: GxEPD2_290");
  Serial.println("Desc:  IL3820 BW (legacy)");
  Serial.print("BUSY initial state: ");
  pinMode(EPAPER_BUSY, INPUT);
  Serial.println(digitalRead(EPAPER_BUSY));

  display.init(115200, true, 50, false);
  display.setRotation(1);

  Serial.println("Drawing screen...");
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Big test number in center
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBold24pt7b);
    display.setCursor(20, 60);
    display.print("TEST 12");

    // Description below
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(20, 95);
    display.print("GxEPD2_290");

    display.setCursor(20, 115);
    display.print("rst=50ms");
  } while (display.nextPage());

  Serial.println("Done. Hibernating.");
  display.hibernate();
  Serial.println("=== TEST #12 COMPLETE ===");
}

void loop() { delay(1000); }
