/*
 * GPIO Loopback Test
 *
 * Setup: Connect D17 directly to D4 via jumper wire (E-Paper disconnected)
 *
 * This tests:
 * 1. ESP32 GPIO4 and GPIO17 are working
 * 2. Jumper wire is not broken
 * 3. Pin sockets have proper contact
 *
 * Expected: D4 reads same value as D17 was set to
 * If FAILED: Either GPIO is broken, jumper is broken, or socket has bad contact
 */

#define PIN_OUT 17  // Output (D/C connection - orange)
#define PIN_IN  4   // Input  (BUSY connection - purple)

int pass_count = 0;
int fail_count = 0;

void test_state(int state, const char* label) {
  digitalWrite(PIN_OUT, state);
  delay(50);
  int read_val = digitalRead(PIN_IN);

  Serial.print("[TEST] Set D17=");
  Serial.print(state);
  Serial.print(", Read D4=");
  Serial.print(read_val);
  Serial.print(" -> ");

  if (read_val == state) {
    Serial.print("PASS");
    pass_count++;
  } else {
    Serial.print("FAIL");
    fail_count++;
  }
  Serial.print(" (");
  Serial.print(label);
  Serial.println(")");
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("==========================================");
  Serial.println(" GPIO LOOPBACK TEST: D17 -> D4");
  Serial.println(" (Test ESP32 GPIO + jumper wire integrity)");
  Serial.println("==========================================");

  pinMode(PIN_OUT, OUTPUT);
  pinMode(PIN_IN, INPUT);

  Serial.println();
  Serial.println("[RUNNING] 20 cycles of HIGH/LOW...");
  Serial.println();

  for (int i = 0; i < 10; i++) {
    test_state(HIGH, "HIGH");
    test_state(LOW,  "LOW");
  }

  Serial.println();
  Serial.println("==========================================");
  Serial.print(" RESULT: ");
  Serial.print(pass_count);
  Serial.print(" PASS / ");
  Serial.print(fail_count);
  Serial.println(" FAIL");
  Serial.println("==========================================");

  if (fail_count == 0) {
    Serial.println(" -> GPIO + jumper wire WORKING");
    Serial.println(" -> Problem is on E-Paper side");
  } else if (pass_count == 0) {
    Serial.println(" -> All FAILED!");
    Serial.println(" -> GPIO broken OR jumper wire broken");
    Serial.println(" -> OR pin socket has bad contact");
  } else {
    Serial.println(" -> INTERMITTENT! Bad contact");
    Serial.println(" -> Press jumpers firmly into sockets");
  }
  Serial.println();
}

void loop() {
  delay(5000);
  Serial.println("[IDLE] Test complete. Restart ESP32 to run again.");
}
