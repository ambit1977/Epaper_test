/*
 * Interactive Signal Control
 *
 * Default: all signals HIGH
 * Send single char via serial to control:
 *   r = RES toggle  (GPIO16 -> E-Paper PIN 2, White)
 *   d = D/C toggle  (GPIO17 -> E-Paper PIN 3, Orange)
 *   c = CS toggle   (GPIO5  -> E-Paper PIN 4, Green)
 *   s = SCL toggle  (GPIO18 -> E-Paper PIN 5, Yellow)
 *   a = SDA toggle  (GPIO23 -> E-Paper PIN 6, Blue)
 *   H = ALL HIGH
 *   L = ALL LOW
 *   ? = show status
 */

#define RES  16
#define DC   17
#define CS   5
#define SCL  18
#define SDA  23

int s_res = HIGH, s_dc = HIGH, s_cs = HIGH, s_scl = HIGH, s_sda = HIGH;

void apply() {
  digitalWrite(RES, s_res);
  digitalWrite(DC,  s_dc);
  digitalWrite(CS,  s_cs);
  digitalWrite(SCL, s_scl);
  digitalWrite(SDA, s_sda);
}

void show_status() {
  Serial.println();
  Serial.println("=== Current State ===");
  Serial.print(" RES (PIN 2, White ): "); Serial.println(s_res ? "HIGH (3.3V)" : "LOW (0V)");
  Serial.print(" D/C (PIN 3, Orange): "); Serial.println(s_dc  ? "HIGH (3.3V)" : "LOW (0V)");
  Serial.print(" CS  (PIN 4, Green ): "); Serial.println(s_cs  ? "HIGH (3.3V)" : "LOW (0V)");
  Serial.print(" SCL (PIN 5, Yellow): "); Serial.println(s_scl ? "HIGH (3.3V)" : "LOW (0V)");
  Serial.print(" SDA (PIN 6, Blue  ): "); Serial.println(s_sda ? "HIGH (3.3V)" : "LOW (0V)");
  Serial.println("=====================");
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(RES, OUTPUT);
  pinMode(DC,  OUTPUT);
  pinMode(CS,  OUTPUT);
  pinMode(SCL, OUTPUT);
  pinMode(SDA, OUTPUT);

  apply();

  Serial.println();
  Serial.println("==========================================");
  Serial.println(" Interactive Signal Control");
  Serial.println("==========================================");
  Serial.println(" Commands:");
  Serial.println("  r = RES toggle    (PIN 2)");
  Serial.println("  d = D/C toggle    (PIN 3)");
  Serial.println("  c = CS  toggle    (PIN 4)");
  Serial.println("  s = SCL toggle    (PIN 5)");
  Serial.println("  a = SDA toggle    (PIN 6)");
  Serial.println("  H = ALL HIGH");
  Serial.println("  L = ALL LOW");
  Serial.println("  ? = show status");
  Serial.println("==========================================");

  show_status();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    bool changed = true;

    switch (c) {
      case 'r': s_res = !s_res; break;
      case 'd': s_dc  = !s_dc;  break;
      case 'c': s_cs  = !s_cs;  break;
      case 's': s_scl = !s_scl; break;
      case 'a': s_sda = !s_sda; break;
      case 'H': s_res = s_dc = s_cs = s_scl = s_sda = HIGH; break;
      case 'L': s_res = s_dc = s_cs = s_scl = s_sda = LOW;  break;
      case '?': break;
      default: changed = false;
    }

    if (changed) {
      apply();
      show_status();
    }
  }
  delay(50);
}
