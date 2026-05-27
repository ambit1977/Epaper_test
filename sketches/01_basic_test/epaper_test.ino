/*
 * WeAct 2.9" E-Paper Module Test
 * ESP32 + Arduino IDE
 * 
 * Pin Configuration:
 * - SPI_CLK: GPIO18 (D18)
 * - SPI_MOSI: GPIO23 (D23)
 * - SPI_MISO: GPIO19 (D19)
 * - CS: GPIO5 (D5)
 * - D/C: GPIO12 (D12) - SCL cable color: Blue
 * - RST: GPIO14 (D14) - RES cable color: White
 * - BUSY: GPIO16 (D16) - BUSY cable color: Purple
 */

#include <SPI.h>

// Pin definitions
#define SPI_CLK 18
#define SPI_MOSI 23
#define SPI_MISO 19
#define EPAPER_CS 5
#define EPAPER_DC 12    // Changed from GPIO9 (not available on board)
#define EPAPER_RST 14   // Changed from GPIO10 (not available on board)
#define EPAPER_BUSY 16  // Changed from GPIO11 (not available on board)

// Display commands
#define CMD_SW_RESET 0x12
#define CMD_DRIVER_OUTPUT_CTRL 0x01
#define CMD_BOOSTER_START 0x06
#define CMD_WRITE_BW_DATA 0x10
#define CMD_WRITE_RED_DATA 0x13
#define CMD_DISPLAY_UPDATE 0x20

class EpaperDriver {
private:
  SPIClass* spi;
  
public:
  EpaperDriver() {
    // Pin setup
    pinMode(EPAPER_CS, OUTPUT);
    pinMode(EPAPER_DC, OUTPUT);
    pinMode(EPAPER_RST, OUTPUT);
    pinMode(EPAPER_BUSY, INPUT);
    
    digitalWrite(EPAPER_CS, HIGH);
    digitalWrite(EPAPER_DC, LOW);
    
    // SPI setup
    spi = new SPIClass(VSPI);
    spi->begin(SPI_CLK, SPI_MISO, SPI_MOSI);
    spi->setFrequency(1000000);  // Reduced from 4MHz to 1MHz
    spi->setDataMode(SPI_MODE0);
    
    Serial.println("[EPAPER] Driver initialized");
  }
  
  void reset() {
    digitalWrite(EPAPER_RST, LOW);
    delay(100);
    digitalWrite(EPAPER_RST, HIGH);
    delay(200);
    wait_busy();
    Serial.println("[EPAPER] Reset completed");
  }
  
  void wait_busy() {
    unsigned long timeout = millis() + 5000;  // 5 second timeout
    int busy_state = digitalRead(EPAPER_BUSY);
    Serial.print("[EPAPER] BUSY pin state: ");
    Serial.println(busy_state);

    while (digitalRead(EPAPER_BUSY) == 0) {  // Changed from == 1 to == 0
      if (millis() > timeout) {
        Serial.println("[EPAPER] ERROR: wait_busy() timeout!");
        break;
      }
      delay(10);
    }
  }
  
  void write_cmd(uint8_t cmd) {
    digitalWrite(EPAPER_DC, LOW);
    digitalWrite(EPAPER_CS, LOW);
    spi->write(cmd);
    digitalWrite(EPAPER_CS, HIGH);
  }
  
  void write_data(uint8_t data) {
    digitalWrite(EPAPER_DC, HIGH);
    digitalWrite(EPAPER_CS, LOW);
    spi->write(data);
    digitalWrite(EPAPER_CS, HIGH);
  }
  
  void init_display() {
    Serial.println("[EPAPER] Initializing display...");
    
    reset();
    wait_busy();
    
    // Software reset
    write_cmd(CMD_SW_RESET);
    wait_busy();
    
    // Driver Output Control
    write_cmd(CMD_DRIVER_OUTPUT_CTRL);
    write_data(0x27);  // MUX = 295
    write_data(0x01);
    write_data(0x00);
    
    // Booster Soft Start
    write_cmd(CMD_BOOSTER_START);
    write_data(0x17);
    write_data(0x17);
    write_data(0x28);
    write_data(0x17);
    
    Serial.println("[EPAPER] Display initialized!");
  }
  
  void clear_display() {
    Serial.println("[EPAPER] Clearing display...");
    
    // Write B/W data (white)
    write_cmd(CMD_WRITE_BW_DATA);
    for (int i = 0; i < 4736; i++) {  // 128 * 296 / 8
      write_data(0xFF);
    }
    
    // Write RED data (no red)
    write_cmd(CMD_WRITE_RED_DATA);
    for (int i = 0; i < 4736; i++) {
      write_data(0x00);
    }
    
    // Display update
    write_cmd(CMD_DISPLAY_UPDATE);
    wait_busy();
    
    Serial.println("[EPAPER] Display cleared");
  }
  
  void test_pattern() {
    Serial.println("[EPAPER] Displaying test pattern...");
    
    // Write B/W data (checkerboard)
    write_cmd(CMD_WRITE_BW_DATA);
    for (int i = 0; i < 4736; i++) {
      write_data((i % 2 == 0) ? 0xAA : 0x55);
    }
    
    // Write RED data (none)
    write_cmd(CMD_WRITE_RED_DATA);
    for (int i = 0; i < 4736; i++) {
      write_data(0x00);
    }
    
    // Display update
    write_cmd(CMD_DISPLAY_UPDATE);
    wait_busy();
    
    Serial.println("[EPAPER] Test pattern displayed");
  }
};

// Global driver instance
EpaperDriver* epd = NULL;

void setup() {
  Serial.begin(115200);
  delay(2000);  // Wait for serial connection

  Serial.println("\n" + String(50, '='));
  Serial.println("WeAct 2.9\" E-Paper Module - Arduino Test");
  Serial.println(String(50, '='));

  // Pin diagnostics before driver initialization
  Serial.println("\n[DIAG] Pin configuration:");
  Serial.print("[DIAG] BUSY (GPIO16) initial state: ");
  pinMode(EPAPER_BUSY, INPUT);
  Serial.println(digitalRead(EPAPER_BUSY));
  Serial.print("[DIAG] RST (GPIO14) initial state: ");
  pinMode(EPAPER_RST, OUTPUT);
  Serial.println(digitalRead(EPAPER_RST));
  Serial.println("[DIAG] Pins configured successfully\n");

  // Initialize driver
  epd = new EpaperDriver();
  
  // Initialize display
  Serial.println("\n[TEST 1] Display initialization");
  epd->init_display();
  delay(2000);
  
  Serial.println("\n[TEST 2] Clear display");
  epd->clear_display();
  delay(2000);
  
  Serial.println("\n[TEST 3] Test pattern");
  epd->test_pattern();
  
  Serial.println("\n" + String(50, '='));
  Serial.println("✓ All tests completed!");
  Serial.println(String(50, '=') + "\n");
}

void loop() {
  // Nothing to do
  delay(1000);
}
