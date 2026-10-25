// =====================================================================
//  HaptiHear  -  haptic hearing-aid wearable  (final firmware, v2)
//
//  Sounds: DOORBELL, VOICE, ALARM, DOG BARK, PHONE RING, COOKER (6, trained on-device)
//  Parts : ESP32 DevKit (CP2102), INMP441 I2S mic, mobile vibration motor
//          driven through an NPN transistor (S8050 / 2N2222A).
//  Board : "ESP32 Dev Module"   Core: "esp32 by Espressif Systems" 3.x
//  Libs  : NONE (only the ESP32 core)
//  Display: Arduino IDE Serial Monitor at 115200 baud
//
//  What it does
//   * Listens through the INMP441 (16 kHz), detects sound events.
//   * Classifies them with an on-device k-Nearest-Neighbour model that
//     you train on the ESP32 itself.
//   * Vibrates a different pattern for each sound, lights the blue LED,
//     and prints what it heard in the Serial Monitor.
//   * Self-diagnosis: checks the mic at boot, auto-detects its channel,
//     and raises MIC FAULT if the mic stops sending audio.
//
//  Controls (BOOT button on the board  OR  letters in the Serial Monitor)
//   BOOT tap        / m : mute or unmute vibration
//   BOOT hold 2 s   / t : start TRAINING
//   in training: BOOT tap / n : next sound     BOOT hold 2 s / s : save
//   i = info,  e = erase training,  l = live sound level on/off,  h = help
// =====================================================================
#include <Arduino.h>
#if !defined(ESP_ARDUINO_VERSION_MAJOR) || ESP_ARDUINO_VERSION_MAJOR < 3
#error "Please install 'esp32 by Espressif Systems' VERSION 3.x in Tools > Board > Boards Manager"
#endif
#include <Preferences.h>
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#define HH_NCLS 6          // number of sounds you can train (must match CLASS_NAMES below)
#include "hh_dsp.h"

// ============================ USER SETTINGS ============================
#define PIN_I2S_SCK  26   // INMP441 SCK
#define PIN_I2S_WS   25   // INMP441 WS
#define PIN_I2S_SD   33   // INMP441 SD
#define PIN_MOTOR     4   // -> 1 kOhm resistor -> transistor base
#define PIN_LED       2   // blue LED on the ESP32 board
#define PIN_BTN       0   // BOOT button on the ESP32 board

// Names of the sounds you can train (one per class, in training order)
const char* CLASS_NAMES[] = { "DOORBELL", "VOICE", "ALARM", "DOG BARK", "PHONE RING", "COOKER" };
static_assert(sizeof(CLASS_NAMES) / sizeof(CLASS_NAMES[0]) == hh::NCLS, "CLASS_NAMES must have HH_NCLS names");

// Vibration patterns: motor ON ms, OFF ms, ON ms, ... ended by 0
const uint16_t PAT_CLASS[][12] = {
  { 150, 100, 150, 400, 150, 100, 150, 0 },            // DOORBELL  : two double-pulses
  { 700, 0 },                                          // VOICE     : one long buzz
  { 120, 80, 120, 80, 120, 80, 120, 80, 120, 0 },      // ALARM     : five fast pulses
  { 80, 150, 80, 150, 80, 0 },                         // DOG BARK  : three short taps
  { 400, 200, 400, 0 },                                // PHONE RING: two medium buzzes
  { 100, 100, 100, 100, 800, 0 },                      // COOKER    : short, short, long (whistle)
};
static_assert(sizeof(PAT_CLASS) / sizeof(PAT_CLASS[0]) == hh::NCLS, "PAT_CLASS must have HH_NCLS patterns");
const uint16_t PAT_UNKNOWN[] = { 250, 0 };             // one short buzz
const uint16_t PAT_LOUD[]    = { 400, 0 };             // untrained: any loud sound
const uint16_t PAT_TICK[]    = { 80, 0 };              // feedback tap
const uint16_t PAT_BOOT[]    = { 100, 100, 100, 100, 100, 0 };
// =======================================================================

Preferences prefs;
hh::FrameDSP dsp;
hh::EventDetector det;
hh::KNN knn;
i2s_chan_handle_t rxChan = nullptr;
QueueHandle_t hapticQ = nullptr;
volatile bool motorBusy = false;

bool micOk = false, micFault = false, muted = false, showLevel = false;
int micCh = 0;
float micRmsDb = -200;

enum Mode { RUN, TRAIN } mode = RUN;
int trainIdx = 0;
bool trainedThisSession[hh::NCLS];

uint32_t alertUntil = 0, ignoreSoundUntil = 0, frameNo = 0;
const uint32_t TRAIN_ARM_MS = 10000;  // training: after 'r', listen this long for one example
uint32_t trainArmUntil = 0;
bool trainArmed = false;              // training records only after 'r', one example at a time

// Unknown-sound filter: a sound further than (factor x typical training distance + offset)
// from every example is reported as UNKNOWN. Higher level = stricter. Changed with 'u'.
const char* REJ_NAMES[]  = { "FIXED 4.0", "FIXED 4.5", "AUTO STRICT", "AUTO NORMAL" };
const float REJ_FIXED[]  = { 4.0f, 4.5f, 0.0f, 0.0f };   // 0 = automatic from the training data
const float REJ_FACTOR[] = { 3.0f, 3.0f, 2.0f, 3.0f };
const float REJ_OFFSET[] = { 2.0f, 2.0f, 1.0f, 2.0f };
const int   REJ_COUNT = 4;
int rejLevel = 1;                     // default: FIXED 4.5

// Sensitivity: which sounds are loud enough to be checked at all. Changed with 'v'.
const char* SENS_NAMES[] = { "HIGH", "MEDIUM", "LOW" };
const float SENS_ON[]    = { 12.0f, 15.0f, 18.0f };    // dB above the background
const float SENS_ABS[]   = { -75.0f, -60.0f, -52.0f }; // and at least this loud (dBFS)
int sensLevel = 1;                    // default: MEDIUM
const int MIN_ACTIVE_FRAMES = 3;      // shorter than ~0.1 s = a click/tap, ignored
bool eventTainted = false;
int silentFrames = 0;

static int32_t raw[hh::N * 2];
static float   xbuf[hh::N];

// ------------------------------ microphone -----------------------------
bool micBegin() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num  = 8;
  cc.dma_frame_num = 480;          // 8 x 480 frames = 240 ms of buffering
  if (i2s_new_channel(&cc, NULL, &rxChan) != ESP_OK) return false;
  i2s_std_config_t sc = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(hh::SR),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)PIN_I2S_SCK,
      .ws   = (gpio_num_t)PIN_I2S_WS,
      .dout = I2S_GPIO_UNUSED,
      .din  = (gpio_num_t)PIN_I2S_SD,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };
  if (i2s_channel_init_std_mode(rxChan, &sc) != ESP_OK) return false;
  if (i2s_channel_enable(rxChan) != ESP_OK) return false;
  // Pull-down on SD (the INMP441 datasheet recommends one): the unused channel
  // and a disconnected data wire then read as clean zeros -> reliable fault check.
  gpio_pulldown_en((gpio_num_t)PIN_I2S_SD);
  return true;
}

bool readFrame() {   // one frame = 512 stereo samples (32 ms)
  size_t got = 0;
  while (got < sizeof(raw)) {
    size_t br = 0;
    if (i2s_channel_read(rxChan, (uint8_t*)raw + got, sizeof(raw) - got, &br, 1000) != ESP_OK) return false;
    got += br;
  }
  return true;
}

void toFloat(int ch) {
  for (int i = 0; i < hh::N; i++) xbuf[i] = (float)(raw[2 * i + ch] >> 8) / 8388608.0f;  // 24-bit -> [-1,1)
}

// Self-test: is real audio arriving, and on which channel?
bool micSelfTest() {
  for (int i = 0; i < 12; i++) if (!readFrame()) return false;   // mic start-up (~0.4 s)
  double s[2] = {0, 0}, s2[2] = {0, 0}; long clip[2] = {0, 0}; long n = 0;
  for (int f = 0; f < 16; f++) {
    if (!readFrame()) return false;
    for (int i = 0; i < hh::N; i++)
      for (int c = 0; c < 2; c++) {
        int32_t v = raw[2 * i + c] >> 8;
        s[c] += v; s2[c] += (double)v * v;
        if (v > 8300000 || v < -8300000) clip[c]++;
      }
    n += hh::N;
  }
  double var[2];
  for (int c = 0; c < 2; c++) { double m = s[c] / n; var[c] = s2[c] / n - m * m; if (var[c] < 0) var[c] = 0; }
  micCh = var[1] > var[0] ? 1 : 0;
  micRmsDb = 10.0f * log10f((float)(var[micCh] / (8388608.0 * 8388608.0)) + 1e-14f);
  if (micRmsDb < -110.0f) return false;       // no signal -> wiring problem
  if (clip[micCh] > n / 2) return false;      // garbage / stuck data line
  return true;
}

// ------------------------- haptics (own task) --------------------------
// Runs separately so vibration timing is exact while audio keeps flowing.
void hapticTask(void*) {
  const uint16_t* p;
  for (;;) {
    if (xQueueReceive(hapticQ, &p, portMAX_DELAY) == pdTRUE) {
      motorBusy = true;
      for (int i = 0; i < 12 && p[i] != 0; i++) {
        digitalWrite(PIN_MOTOR, (i % 2 == 0) ? HIGH : LOW);
        vTaskDelay(pdMS_TO_TICKS(p[i]));
      }
      digitalWrite(PIN_MOTOR, LOW);
      motorBusy = false;
    }
  }
}
void haptic(const uint16_t* p, bool force) {
  if (muted && !force) return;
  xQueueOverwrite(hapticQ, &p);
}
bool hapticActive() { return motorBusy || uxQueueMessagesWaiting(hapticQ) > 0; }

// ------------------------------- LED -----------------------------------
void ledUpdate() {
  uint32_t t = millis(); bool on;
  if (micFault || !micOk)   on = (t / 100) % 2;     // fast blink  = fault
  else if (mode == TRAIN)   on = (t / 250) % 2;     // slow blink  = training
  else if (t < alertUntil)  on = true;              // solid       = sound detected
  else                      on = (t % 3000) < 100;  // short blip  = alive
  digitalWrite(PIN_LED, on ? HIGH : LOW);
}

// ------------------------------ storage --------------------------------
void saveModel() {
  knn.finalize();
  prefs.begin("haptihear", false);
  prefs.putBytes("model", &knn.m, sizeof(knn.m));
  prefs.end();
}
void loadModel() {
  prefs.begin("haptihear", true);
  bool ok = prefs.getBytesLength("model") == sizeof(knn.m) &&
            prefs.getBytes("model", &knn.m, sizeof(knn.m)) == sizeof(knn.m);
  prefs.end();
  if (!ok || !knn.valid()) knn.clear(); else knn.finalize();
}

void printInfo() {
  Serial.println("------------------------------------------------");
  Serial.printf(" Mic       : %s (channel %d)\n", micFault ? "FAULT" : "OK", micCh);
  Serial.printf(" Unknown filter: %s (unknown above %.2f)\n", REJ_NAMES[rejLevel], knn.rejectDist > 1e8f ? 0.0f : knn.rejectDist);
  Serial.printf(" Sensitivity: %s (needs +%.0f dB and > %.0f dBFS)\n", SENS_NAMES[sensLevel], SENS_ON[sensLevel], SENS_ABS[sensLevel]);
  Serial.printf(" Background: %.1f dBFS   now: %.1f dBFS\n", det.floorDb, det.levelDb);
  Serial.printf(" Vibration : %s\n", muted ? "MUTED" : "on");
  Serial.println(" Training examples:");
  for (int c = 0; c < hh::NCLS; c++) Serial.printf("   %-11s %d\n", CLASS_NAMES[c], knn.m.cnt[c]);
  Serial.println("------------------------------------------------");
}
void printHelp() {
  Serial.println("Commands (type a letter, press Enter):");
  Serial.println("  t = start training     r = record one example   n = next sound   s = save training");
  Serial.println("  u = unknown-sound filter (fixed 4.0 / fixed 4.5 / auto strict / auto normal)");
  Serial.println("  q = self-check: how well each sound is learned");
  Serial.println("  v = sensitivity (high / medium / low) - lower = ignores more background noise");
  Serial.println("  m = mute/unmute        i = info           e = erase training");
  Serial.println("  l = live level on/off  h = help");
  Serial.println("BOOT button: tap = mute (or next sound in training), hold 2 s = train / save");
}

// ------------------------- events, training, UI ------------------------
void onSoundEvent(const float* v) {
  bool tooShort = v[hh::NBANDS] * hh::WIN < MIN_ACTIVE_FRAMES - 0.5f;
  if (mode == TRAIN) {
    if (tooShort) { Serial.println("   too short (a click?) - not recorded. Type r and try again."); trainArmed = false; return; }
    if (!trainedThisSession[trainIdx]) { knn.clearClass(trainIdx); trainedThisSession[trainIdx] = true; }
    knn.add(trainIdx, v);
    haptic(PAT_TICK, true);
    alertUntil = millis() + 300;
    trainArmed = false;
    Serial.printf("   recorded %s example %d of %d (peak %.0f dBFS).   r = record another,  n = next,  s = save\n",
                  CLASS_NAMES[trainIdx], knn.m.cnt[trainIdx], hh::MAXS, eventPeakDb());
    return;
  }
  if (tooShort) return;            // clicks and taps are not alerts
  float d = 0;
  int r = knn.classify(v, &d);
  const char* label; const uint16_t* pat;
  if (r >= 0)                    { label = CLASS_NAMES[r]; pat = PAT_CLASS[r]; }
  else if (r == hh::RES_UNKNOWN) { label = "UNKNOWN SOUND"; pat = PAT_UNKNOWN; }
  else                           { label = "LOUD SOUND"; pat = PAT_LOUD; }
  alertUntil = millis() + 1000;
  if (r == hh::RES_UNTRAINED)
    Serial.printf(">>> %-13s  level %.0f dBFS   (not trained yet - type t to train)\n", label, det.levelDb);
  else
    Serial.printf(">>> %-13s  match distance %.2f (unknown above %.2f)  peak %.0f dBFS%s\n",
                  label, d, knn.rejectDist > 1e8f ? 0.0f : knn.rejectDist, eventPeakDb(), muted ? "  [muted]" : "");
  Serial.flush();          // finish sending the text before the motor starts (motor noise can garble it)
  haptic(pat, false);
}

void announceTrainSound() {
  trainArmed = false;
  Serial.printf("\n[TRAINING %d/%d] %s: type r + Enter, then make the sound ONCE. Repeat 8-10 times.\n",
                trainIdx + 1, hh::NCLS, CLASS_NAMES[trainIdx]);
  Serial.println("   Then: n (or tap BOOT) = next sound,  s (or hold BOOT) = save and finish");
}
void enterTrain() {
  if (mode == TRAIN) return;
  mode = TRAIN; trainIdx = 0;
  for (int c = 0; c < hh::NCLS; c++) trainedThisSession[c] = false;
  haptic(PAT_TICK, true);
  Serial.println("\n========== TRAINING MODE ==========");
  announceTrainSound();
}
void exitTrain() {
  if (mode != TRAIN) return;
  saveModel();
  mode = RUN;
  haptic(PAT_BOOT, true);
  Serial.printf("\n========== TRAINING SAVED (%d of %d sounds trained) ==========\n",
                knn.classesTrained(), hh::NCLS);
  printInfo();
  Serial.println("Listening...");
}
void nextTrainSound() {
  if (mode != TRAIN) return;
  trainIdx++;
  if (trainIdx >= hh::NCLS) { exitTrain(); return; }
  haptic(PAT_TICK, true);
  announceTrainSound();
}
void armRecord() {
  if (mode != TRAIN) { Serial.println("(r only works in training - type t first)"); return; }
  trainArmed = true; trainArmUntil = millis() + TRAIN_ARM_MS;
  Serial.printf("   listening... make the %s sound now\n", CLASS_NAMES[trainIdx]);
}
void cycleRejectLevel() {
  rejLevel = (rejLevel + 1) % REJ_COUNT;
  knn.rejFixed = REJ_FIXED[rejLevel]; knn.rejFactor = REJ_FACTOR[rejLevel]; knn.rejOffset = REJ_OFFSET[rejLevel];
  knn.finalize();
  prefs.begin("haptihear", false); prefs.putUChar("rejmode", (uint8_t)rejLevel); prefs.end();
  if (knn.rejectDist > 1e8f)
    Serial.printf("Unknown filter: %s (train at least 2 examples of 2 sounds first)\n", REJ_NAMES[rejLevel]);
  else
    Serial.printf("Unknown filter: %s -> sounds further than %.2f are UNKNOWN\n", REJ_NAMES[rejLevel], knn.rejectDist);
}
// Leave-one-out self-check: hide each training example, classify it with the
// rest, and report per sound how many come back right / unknown / as what.
void selfCheck() {
  if (knn.classesTrained() < 2) { Serial.println("Self-check: train at least 2 sounds first."); return; }
  Serial.println("------------------------------------------------");
  Serial.println(" SELF-CHECK (each example is hidden and recognised by the others)");
  int ok = 0, tot = 0;
  for (int c = 0; c < hh::NCLS; c++) {
    if (knn.m.cnt[c] == 0) { Serial.printf("   %-11s not trained\n", CLASS_NAMES[c]); continue; }
    int good = 0, unk = 0, wrong[hh::NCLS] = {0};
    for (int i = 0; i < knn.m.cnt[c]; i++) {
      int r = knn.classifyEx(knn.m.x[c][i], c, i, nullptr);
      tot++;
      if (r == c) { good++; ok++; } else if (r < 0) unk++; else wrong[r]++;
    }
    Serial.printf("   %-11s %2d/%-2d right", CLASS_NAMES[c], good, knn.m.cnt[c]);
    if (unk) Serial.printf(", %d unknown", unk);
    for (int d = 0; d < hh::NCLS; d++) if (wrong[d]) Serial.printf(", %d as %s", wrong[d], CLASS_NAMES[d]);
    if (knn.m.cnt[c] < 5) Serial.print("   <- needs more examples");
    else if (good * 10 < knn.m.cnt[c] * 7) Serial.print("   <- retrain with more varied examples");
    Serial.println();
  }
  Serial.printf(" Overall: %d of %d right (%d %%)\n", ok, tot, tot ? ok * 100 / tot : 0);
  Serial.println("------------------------------------------------");
}
void applySensitivity() { det.onDb = SENS_ON[sensLevel]; det.absMinDb = SENS_ABS[sensLevel]; }
void cycleSensitivity() {
  sensLevel = (sensLevel + 1) % 3;
  applySensitivity();
  prefs.begin("haptihear", false); prefs.putUChar("sens", (uint8_t)sensLevel); prefs.end();
  Serial.printf("Sensitivity: %s (sound must be %.0f dB above background and louder than %.0f dBFS)\n",
                SENS_NAMES[sensLevel], SENS_ON[sensLevel], SENS_ABS[sensLevel]);
}
float eventPeakDb() {   // loudest frame of the event that was just captured
  float p = -200;
  for (int i = 0; i < hh::WIN; i++) if (det.ring[i].db > p) p = det.ring[i].db;
  return p;
}
void toggleMute() {
  muted = !muted;
  haptic(PAT_TICK, true);
  Serial.println(muted ? "Vibration MUTED" : "Vibration ON");
}

void handleButton() {
  static bool wasDown = false, longFired = false;
  static uint32_t downAt = 0;
  bool down = digitalRead(PIN_BTN) == LOW;
  uint32_t t = millis();
  if (down || wasDown) ignoreSoundUntil = t + 400;   // don't "hear" the button click
  if (down && !wasDown) { downAt = t; longFired = false; }
  if (down && !longFired && t - downAt >= 2000) {
    longFired = true;
    if (mode == RUN) enterTrain(); else exitTrain();
  }
  if (!down && wasDown && !longFired && t - downAt >= 50) {
    if (mode == RUN) toggleMute(); else nextTrainSound();
  }
  wasDown = down;
}

void handleSerial() {
  while (Serial.available()) {
    char ch = tolower(Serial.read());
    switch (ch) {
      case 't': enterTrain(); break;
      case 'r': armRecord(); break;
      case 'u': cycleRejectLevel(); break;
      case 'q': selfCheck(); break;
      case 'v': cycleSensitivity(); break;
      case 'n': nextTrainSound(); break;
      case 's': exitTrain(); break;
      case 'm': toggleMute(); break;
      case 'i': printInfo(); break;
      case 'e': knn.clear(); saveModel(); Serial.println("Training ERASED. Every loud sound now gives LOUD SOUND."); break;
      case 'l': showLevel = !showLevel; break;
      case 'h': printHelp(); break;
      default: break;   // ignore Enter, spaces, etc.
    }
  }
}

void printLevel() {
  int n = (int)((det.levelDb + 90.0f) / 60.0f * 40.0f);
  if (n < 0) n = 0; if (n > 40) n = 40;
  char bar[41]; for (int i = 0; i < 40; i++) bar[i] = i < n ? '#' : '.'; bar[40] = 0;
  Serial.printf("level %6.1f dBFS |%s|\n", det.levelDb, bar);
}

// --------------------------------- setup -------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(PIN_MOTOR, OUTPUT); digitalWrite(PIN_MOTOR, LOW);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);
  digitalWrite(PIN_LED, HIGH);

  hapticQ = xQueueCreate(1, sizeof(const uint16_t*));
  xTaskCreate(hapticTask, "haptic", 2048, NULL, 2, NULL);

  Serial.println("\n\n==================================================");
  Serial.println("  HaptiHear - haptic hearing aid");
  Serial.println("==================================================");
  Serial.println("SELF-TEST");
  dsp.init();

  micOk = micBegin() && micSelfTest();
  while (!micOk) {
    Serial.println("  Microphone ......... FAIL  (no audio arriving)");
    Serial.println("     Check INMP441: VDD->3V3, GND->GND, L/R->GND, SCK->D26, WS->D25, SD->D33");
    Serial.println("     Retrying in 2 s...");
    for (int i = 0; i < 40; i++) { ledUpdate(); delay(50); }
    micOk = micSelfTest();
  }
  Serial.printf("  Microphone ......... OK   (channel %d, background %.0f dBFS)\n", micCh, micRmsDb);
  Serial.println("  Vibration motor .... you should feel 3 pulses now");
  haptic(PAT_BOOT, true);

  prefs.begin("haptihear", true); rejLevel = prefs.getUChar("rejmode", 1); prefs.end();
  if (rejLevel >= REJ_COUNT) rejLevel = 1;
  prefs.begin("haptihear", true); sensLevel = prefs.getUChar("sens", 1); prefs.end();
  if (sensLevel > 2) sensLevel = 1;
  applySensitivity();
  knn.rejFixed = REJ_FIXED[rejLevel]; knn.rejFactor = REJ_FACTOR[rejLevel]; knn.rejOffset = REJ_OFFSET[rejLevel];
  loadModel();
  Serial.printf("  Training memory .... %d sounds trained\n", knn.classesTrained());
  for (int c = 0; c < hh::NCLS; c++) if (knn.m.cnt[c]) Serial.printf("     %s: %d examples\n", CLASS_NAMES[c], knn.m.cnt[c]);
  Serial.println();
  printHelp();
  Serial.println("\nListening...");
  delay(800);
  det.reset();
  readFrame();   // discard audio buffered during the delay
}

// --------------------------------- loop --------------------------------
void loop() {
  if (!readFrame()) {
    if (!micFault) Serial.println("!!! MIC FAULT: I2S read timeout");
    micFault = true; ledUpdate();
    return;
  }
  frameNo++;
  toFloat(micCh);

  hh::Frame fr;
  dsp.process(xbuf, fr);

  // microphone watchdog: ~2 s of digital silence = mic disconnected
  if (fr.db < -110.0f) {
    if (++silentFrames > 60 && !micFault) { micFault = true; Serial.println("!!! MIC FAULT: no signal - check the mic wires"); }
  } else {
    silentFrames = 0;
    if (micFault) { micFault = false; Serial.println("Mic recovered. Listening..."); }
  }

  handleButton();

  // Motor sits next to the mic: ignore sounds that START while it vibrates
  // (or within 0.3 s after), so an alert can never trigger another alert.
  if (hapticActive()) ignoreSoundUntil = millis() + 300;

  if (!micFault) {
    int before = det.state;
    bool ready = det.push(fr);
    if (before == 0 && det.state == 1) eventTainted = millis() < ignoreSoundUntil || (mode == TRAIN && !(trainArmed && millis() < trainArmUntil));
    if (ready && !eventTainted) { float v[hh::DIM]; det.features(v); onSoundEvent(v); }
  }

  handleSerial();
  ledUpdate();
  if (mode == TRAIN && trainArmed && det.state != 1 && millis() >= trainArmUntil) {
    trainArmed = false;
    Serial.println("   nothing heard in 10 s - type r to try again (a bit louder or closer)");
  }
  if (showLevel && frameNo % 8 == 0) printLevel();
}
