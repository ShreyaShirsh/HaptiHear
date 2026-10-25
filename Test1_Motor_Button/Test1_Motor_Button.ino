// TEST 1 - vibration motor (through the transistor), blue LED, BOOT button
// Open Tools > Serial Monitor at 115200.
// Expected:
//   * Every 3 seconds the motor plays the next pattern and its name is printed.
//     These are the same patterns the final program uses - learn them now.
//   * The blue LED on the board blinks.
//   * Pressing the BOOT button prints "BOOT button: PRESSED".
#define PIN_MOTOR 4    // -> 1 kOhm -> transistor base
#define PIN_LED   2    // blue LED on the board
#define PIN_BTN   0    // BOOT button on the board

struct Pat { const char* name; uint16_t ms[12]; };   // ON, OFF, ON ... ms, ends with 0
const Pat pats[] = {
  { "DOORBELL (two double-pulses)", { 150, 100, 150, 400, 150, 100, 150, 0 } },
  { "VOICE (one long buzz)",        { 700, 0 } },
  { "ALARM (five fast pulses)",     { 120, 80, 120, 80, 120, 80, 120, 80, 120, 0 } },
  { "DOG BARK (three short taps)",  { 80, 150, 80, 150, 80, 0 } },
  { "PHONE RING (two medium buzzes)", { 400, 200, 400, 0 } },
  { "COOKER (short, short, long)",  { 100, 100, 100, 100, 800, 0 } },
  { "UNKNOWN (one short buzz)",     { 250, 0 } },
};

void play(const uint16_t* p) {
  for (int i = 0; i < 12 && p[i] != 0; i++) {
    digitalWrite(PIN_MOTOR, (i % 2 == 0) ? HIGH : LOW);
    delay(p[i]);
  }
  digitalWrite(PIN_MOTOR, LOW);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_MOTOR, OUTPUT);
  digitalWrite(PIN_MOTOR, LOW);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);
  delay(500);
  Serial.println("\nTEST 1: motor + LED + BOOT button");
}

void loop() {
  static uint32_t last = 0;
  static int i = 0;
  static bool wasDown = false;

  bool down = digitalRead(PIN_BTN) == LOW;
  if (down != wasDown) { Serial.println(down ? "BOOT button: PRESSED" : "BOOT button: released"); wasDown = down; }

  digitalWrite(PIN_LED, (millis() / 500) % 2);

  if (millis() - last >= 3000) {
    last = millis();
    Serial.printf("Motor: %s\n", pats[i].name);
    play(pats[i].ms);
    i = (i + 1) % (sizeof(pats) / sizeof(pats[0]));
  }
  delay(10);
}
