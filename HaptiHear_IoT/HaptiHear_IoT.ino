// =====================================================================
//  HaptiHear IoT  -  haptic hearing-aid wearable, connected version (v3)
//
//  Same sound recognition as HaptiHear (v2), plus:
//   * Wi-Fi + MQTT: every alert goes to a phone app (IoT MQTT Panel)
//     through a public MQTT broker; the phone can also send commands.
//   * Wear sensor   : ESP32 touch pin + a wire touching the skin.
//   * Touch pad     : tap = acknowledge alert, hold 3 s = SOS to phone.
//   * Alert check   : every vibration is also a self-test. The mic listens
//     to the motor's own buzz: no buzz heard = motor fault / loose band;
//     buzz level drifting while the room is unchanged = mic degrading.
//   * Optional parts (set HAS_... to 1 only after wiring them):
//     LDR light sensor (night mode), external LED (flash alerts),
//     buzzer (SOS siren for people nearby).
//
//  Board : "ESP32 Dev Module"   Core: "esp32 by Espressif Systems" 3.x
//  Libs  : NONE (Wi-Fi and MQTT are part of the ESP32 core)
//  Phone : hotspot name/password must match WIFI_SSID / WIFI_PASS below.
//
//  Training is stored in the same memory as HaptiHear v2, so sounds
//  trained with v2 are kept when you upload this program.
// =====================================================================
#include <Arduino.h>
#if !defined(ESP_ARDUINO_VERSION_MAJOR) || ESP_ARDUINO_VERSION_MAJOR < 3
#error "Please install 'esp32 by Espressif Systems' VERSION 3.x in Tools > Board > Boards Manager"
#endif
#include <Preferences.h>
#include <WiFi.h>
#include "mqtt_client.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#define HH_NCLS 6          // number of sounds you can train (must match CLASS_NAMES below)
#include "hh_dsp.h"

// ============================ USER SETTINGS ============================
#define WIFI_SSID    "HaptiHear"                  // phone hotspot name
#define WIFI_PASS    "haptihear123"               // phone hotspot password
#define MQTT_BROKER  "mqtt://broker.hivemq.com"   // free public broker, port 1883

// Optional parts: change 0 -> 1 only after the part is wired
#define HAS_LDR      0    // light sensor on D34  (night mode)
#define HAS_EXT_LED  0    // external LED on D18  (flashes with every alert)
#define HAS_BUZZER   0    // buzzer via 2nd transistor on D19 (SOS siren)
#define HAS_KNOCK    1    // SW-420 vibration sensor on D35 (door-knock detector)

#define PIN_I2S_SCK  26   // INMP441 SCK
#define PIN_I2S_WS   25   // INMP441 WS
#define PIN_I2S_SD   33   // INMP441 SD
#define PIN_MOTOR     4   // -> 1 kOhm resistor -> transistor base
#define PIN_LED       2   // blue LED on the ESP32 board
#define PIN_BTN       0   // BOOT button on the ESP32 board
#define PIN_WEAR     27   // touch: bare jumper end taped against the skin
#define PIN_PAD      32   // touch: bare jumper end on the outside of the band
#define PIN_LDR      34   // LDR divider (3V3 - LDR - D34 - 10 kOhm - GND)
#define PIN_EXT_LED  18   // external LED (-> 1 kOhm -> LED -> GND)
#define PIN_BUZZER   19   // -> 1 kOhm -> 2nd transistor base, buzzer on collector
#define PIN_KNOCK    35   // SW-420 module DO pin (D35 is input-only, fine for a sensor)

// Names of the sounds you can train (one per class, in training order)
const char* CLASS_NAMES[] = { "DOORBELL", "VOICE", "ALARM", "DOG BARK", "PHONE RING", "COOKER" };
static_assert(sizeof(CLASS_NAMES) / sizeof(CLASS_NAMES[0]) == hh::NCLS, "CLASS_NAMES must have HH_NCLS names");
// Sounds switched off are skipped in training, the self-check and info, and never reported.
// (Switching off keeps the saved training; removing a class would erase it.)
const bool CLASS_ON[] = { true, false, true, true, true, true };   // VOICE off
static_assert(sizeof(CLASS_ON) / sizeof(CLASS_ON[0]) == hh::NCLS, "CLASS_ON must have HH_NCLS entries");

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
const uint16_t PAT_SOS[]     = { 100, 100, 100, 100, 100, 300, 400, 100, 400, 100, 400, 0 };  // . . . - - -
const uint16_t PAT_KNOCK[]   = { 500, 150, 100, 100, 100, 0 };   // KNOCK (vibration sensor): long, short, short

// Touch, light and alert-check tuning
const float TOUCH_ON  = 0.25f;   // touched when reading moves > 25 % away from its calibrated value
const float TOUCH_OFF = 0.15f;   // released again below 15 %
const int   LDR_DARK  = 600;     // ADC below this = dark (night mode), 0..4095
const int   LDR_LIGHT = 900;     // ADC above this = light again
const float CHECK_HEARD_DB = 6.0f;   // motor must raise the mic level this much above the background
const float CHECK_DRIFT_DB = 8.0f;   // change from the learned reference that counts as mic drift
// =======================================================================

struct HapticJob { const uint16_t* p; uint8_t reps; bool buzz; };
struct TouchPad  { uint8_t pin; float base; bool on; uint32_t since; };

Preferences prefs;
hh::FrameDSP dsp;
hh::EventDetector det;
hh::KNN knn;
i2s_chan_handle_t rxChan = nullptr;
QueueHandle_t hapticQ = nullptr;
QueueHandle_t cmdQ = nullptr;          // commands arriving from the phone
volatile bool motorBusy = false;
volatile bool motorOnNow = false;      // motor pin is HIGH right now

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

// sensors
TouchPad wear = { PIN_WEAR, 0, false, 0 };
TouchPad pad  = { PIN_PAD,  0, false, 0 };
bool worn = true;                      // assumed worn until the sensor says otherwise
bool night = false;
int  lightRaw = -1;
bool sosFired = false;
uint32_t lastAlertAt = 0;
int lastAlertClass = -2;

// statistics for the phone (since power-on)
uint16_t eventCount[hh::NCLS + 2];     // classes, then UNKNOWN, LOUD
uint16_t totalEvents = 0;
uint16_t knockCount = 0;               // knocks felt by the SW-420 since power-on
uint32_t micQuietUntil = 0;            // after a knock, ignore the mic's version of the same sound
#if HAS_KNOCK
volatile uint32_t knockEdges = 0;      // pulses from the SW-420, counted in an interrupt
void IRAM_ATTR knockISR() { knockEdges = knockEdges + 1; }
#endif

// alert check (motor -> mic loopback)
bool  chkActive = false;
float chkSum = 0, chkFloor = 0;
int   chkN = 0;
float chkRef = 0;                      // learned "normal" loopback level (dB above background)
int   chkRefN = 0;                     // good checks that built the reference
int   chkMissStreak = 0;
const char* chkState = "waiting";      // waiting / ok / NOT HEARD / MIC DRIFT

// network
char devId[8] = "000000";
char topicBase[32], tEvent[48], tStatus[48], tCheck[48], tSos[48], tCmd[48];
esp_mqtt_client_handle_t mqtt = nullptr;
volatile bool mqttUp = false;
volatile bool mqttJustUp = false;
bool mqttStarted = false;
bool wifiWasUp = false;
uint32_t lastStatusAt = 0;

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
  gpio_pulldown_en((gpio_num_t)PIN_I2S_SD);   // unused slot / loose wire -> clean zeros
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

// ---------------------- haptics + flash + buzzer -----------------------
// Runs separately so vibration timing is exact while audio keeps flowing.
void outputsSet(bool on, bool buzz) {
  digitalWrite(PIN_MOTOR, on ? HIGH : LOW);
  motorOnNow = on;
#if HAS_EXT_LED
  digitalWrite(PIN_EXT_LED, (on && !night) ? HIGH : LOW);   // no flashing light at night
#endif
#if HAS_BUZZER
  digitalWrite(PIN_BUZZER, (on && buzz) ? HIGH : LOW);
#else
  (void)buzz;
#endif
}

void hapticTask(void*) {
  HapticJob j;
  for (;;) {
    if (xQueueReceive(hapticQ, &j, portMAX_DELAY) == pdTRUE) {
      motorBusy = true;
      for (int r = 0; r < j.reps; r++) {
        if (r > 0) vTaskDelay(pdMS_TO_TICKS(400));
        for (int i = 0; i < 12 && j.p[i] != 0; i++) {
          outputsSet(i % 2 == 0, j.buzz);
          vTaskDelay(pdMS_TO_TICKS(j.p[i]));
        }
        outputsSet(false, false);
      }
      motorBusy = false;
    }
  }
}
void hapticSend(const uint16_t* p, bool force, bool buzz) {
  if (muted && !force) return;
  HapticJob j = { p, (uint8_t)((night && !force) ? 2 : 1), buzz };   // night: play alerts twice
  xQueueOverwrite(hapticQ, &j);
}
void haptic(const uint16_t* p, bool force) { hapticSend(p, force, false); }
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

// ------------------------------- network -------------------------------
void mqttEvent(void*, esp_event_base_t, int32_t eventId, void* eventData) {
  esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)eventData;
  switch ((esp_mqtt_event_id_t)eventId) {
    case MQTT_EVENT_CONNECTED:
      mqttUp = true; mqttJustUp = true;
      esp_mqtt_client_subscribe_single(e->client, tCmd, 0);
      break;
    case MQTT_EVENT_DISCONNECTED:
      mqttUp = false;
      break;
    case MQTT_EVENT_DATA:   // a command from the phone: pass each letter to the main loop
      if (e->topic_len == (int)strlen(tCmd) && strncmp(e->topic, tCmd, e->topic_len) == 0)
        for (int i = 0; i < e->data_len && i < 8; i++) { char c = e->data[i]; xQueueSend(cmdQ, &c, 0); }
      break;
    default: break;
  }
}

void netPublish(const char* topic, const char* msg, bool retain) {
  if (!mqtt || !mqttUp) return;      // offline: alerts still work, just not sent to the phone
  esp_mqtt_client_enqueue(mqtt, topic, msg, 0, 0, retain ? 1 : 0, true);   // non-blocking
}

void netBegin() {
  WiFi.mode(WIFI_STA);
  uint64_t chip = ESP.getEfuseMac();   // factory MAC; WiFi.macAddress() can still read 0 this early
  uint8_t mac[6]; for (int i = 0; i < 6; i++) mac[i] = (uint8_t)(chip >> (8 * i));
  snprintf(devId, sizeof(devId), "%02x%02x%02x", mac[3], mac[4], mac[5]);
  snprintf(topicBase, sizeof(topicBase), "haptihear/%s", devId);
  snprintf(tEvent,  sizeof(tEvent),  "%s/event",  topicBase);
  snprintf(tStatus, sizeof(tStatus), "%s/status", topicBase);
  snprintf(tCheck,  sizeof(tCheck),  "%s/check",  topicBase);
  snprintf(tSos,    sizeof(tSos),    "%s/sos",    topicBase);
  snprintf(tCmd,    sizeof(tCmd),    "%s/cmd",    topicBase);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);   // connects in the background
}

void netStartMqtt() {
  esp_mqtt_client_config_t cfg = {};
  cfg.broker.address.uri = MQTT_BROKER;
  cfg.session.keepalive = 30;
  cfg.session.last_will.topic = tStatus;
  cfg.session.last_will.msg = "{\"online\":false}";
  cfg.session.last_will.retain = 1;
  mqtt = esp_mqtt_client_init(&cfg);
  if (!mqtt) return;
  esp_mqtt_client_register_event(mqtt, MQTT_EVENT_ANY, mqttEvent, NULL);
  esp_mqtt_client_start(mqtt);
  mqttStarted = true;
}

void publishStatus() {
  char counts[64]; int p = 0;
  for (int c = 0; c < hh::NCLS + 2; c++) p += snprintf(counts + p, sizeof(counts) - p, "%s%u", c ? "," : "", eventCount[c]);
  char msg[384];
  snprintf(msg, sizeof(msg),
    "{\"online\":true,\"mic\":\"%s\",\"worn\":%s,\"night\":%s,\"muted\":%s,\"mode\":\"%s\","
    "\"trained\":%d,\"events\":%u,\"counts\":[%s],\"check\":\"%s\",\"checkRef\":%.1f,"
    "\"light\":%d,\"knocks\":%u,\"bg\":%.1f,\"rssi\":%d,\"up\":%lu}",
    micFault ? "FAULT" : "ok", worn ? "true" : "false", night ? "true" : "false", muted ? "true" : "false",
    mode == TRAIN ? "train" : "run", knn.classesTrained(), totalEvents, counts, chkState, chkRef,
    lightRaw, (unsigned)knockCount, det.floorDb, (int)WiFi.RSSI(), (unsigned long)(millis() / 1000));
  netPublish(tStatus, msg, true);
  lastStatusAt = millis();
}

void netLoop() {
  bool up = WiFi.status() == WL_CONNECTED;
  if (up && !wifiWasUp) Serial.printf("Wi-Fi connected (%s, signal %d dBm)\n", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  if (!up && wifiWasUp) Serial.println("Wi-Fi lost - alerts keep working, phone updates paused");
  wifiWasUp = up;
  // iPhones hide their hotspot when nobody is connected: keep looking every 15 s
  static uint32_t lastTry = 0;
  if (!up && millis() - lastTry > 15000) {
    lastTry = millis();
    Serial.printf("Wi-Fi: looking for hotspot \"%s\"... (keep the phone's hotspot screen open)\n", WIFI_SSID);
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
  if (up && !mqttStarted) netStartMqtt();
  if (mqttJustUp) { mqttJustUp = false; Serial.printf("Phone link (MQTT) connected. Topic: %s/#\n", topicBase); publishStatus(); }
  if (mqttUp && millis() - lastStatusAt > 30000) publishStatus();
}

// ------------------------------- sensors -------------------------------
void touchCalibrate(TouchPad& t) {
  touchRead(t.pin);
  delay(60);
  float s = 0; int n = 0;
  for (int i = 0; i < 10; i++) { uint32_t v = touchRead(t.pin); if (v) { s += v; n++; } delay(15); }
  t.base = n ? s / n : 0; t.on = false; t.since = millis();
}
bool touchUpdate(TouchPad& t) {      // returns true when the state changed
  if (t.base <= 0) return false;
  float dev = fabsf((float)touchRead(t.pin) - t.base) / t.base;
  bool on = t.on ? dev > TOUCH_OFF : dev > TOUCH_ON;
  if (on == t.on) return false;
  t.on = on; t.since = millis();
  return true;
}

// SW-420: a knock makes a short burst of pulses. Two separate bursts within
// 2 s ("knock-knock") = KNOCK alert; a single bump of the table is ignored.
void onKnock() {
  knockCount++; totalEvents++;
  alertUntil = millis() + 1000; lastAlertAt = millis(); lastAlertClass = -1;
  micQuietUntil = millis() + 1500;
  Serial.printf(">>> %-13s  (vibration sensor)%s%s\n", "KNOCK", muted ? "  [muted]" : "", worn ? "" : "  [band not worn]");
  Serial.flush();
  haptic(PAT_KNOCK, false);
  char msg[160];
  snprintf(msg, sizeof(msg), "{\"sound\":\"KNOCK\",\"source\":\"vibration\",\"worn\":%s,\"muted\":%s,\"n\":%u}",
           worn ? "true" : "false", muted ? "true" : "false", totalEvents);
  netPublish(tEvent, msg, false);
}
void knockUpdate() {   // called every ~128 ms
#if HAS_KNOCK
  static uint32_t lastEdges = 0, firstBurstAt = 0, lastKnockAt = 0;
  static bool wasActive = false;
  static int bursts = 0;
  uint32_t e = knockEdges;
  bool active = e != lastEdges;
  lastEdges = e;
  uint32_t t = millis();
  if (mode == TRAIN || hapticActive() || t < ignoreSoundUntil) { wasActive = active; bursts = 0; return; }  // our own motor shakes it too
  if (active && !wasActive) {                       // a new burst started
    if (bursts == 0 || t - firstBurstAt > 2000) { bursts = 0; firstBurstAt = t; }
    bursts++;
    if (bursts >= 2 && t - lastKnockAt > 3000) { lastKnockAt = t; bursts = 0; onKnock(); }
  }
  wasActive = active;
#endif
}

void sensorsUpdate() {
  knockUpdate();
  if (touchUpdate(wear)) {
    worn = wear.on;
    Serial.println(worn ? "Band is ON the wrist" : "Band taken OFF - alerts will be marked on the phone");
    publishStatus();
  }
  // tap / hold detection on the outside touch pad
  static bool padWas = false; static uint32_t padDownAt = 0;
  touchUpdate(pad);
  if (pad.on && !padWas) { padDownAt = millis(); sosFired = false; ignoreSoundUntil = millis() + 400; }
  if (pad.on && !sosFired && millis() - padDownAt >= 3000) {      // hold 3 s = SOS
    sosFired = true;
    Serial.println("\n!!! SOS sent to phone !!!");
    hapticSend(PAT_SOS, true, true);
    char msg[96]; snprintf(msg, sizeof(msg), "{\"sos\":true,\"worn\":%s,\"up\":%lu}", worn ? "true" : "false", (unsigned long)(millis() / 1000));
    netPublish(tSos, msg, false);
  }
  if (!pad.on && padWas && !sosFired && millis() - padDownAt < 1000) {   // short tap = acknowledge
    haptic(PAT_TICK, true);
    if (lastAlertClass != -2 && millis() - lastAlertAt < 15000) {
      Serial.println("Alert acknowledged");
      char msg[96]; snprintf(msg, sizeof(msg), "{\"ack\":\"%s\",\"after_s\":%.1f}",
        lastAlertClass >= 0 ? CLASS_NAMES[lastAlertClass] : "OTHER", (millis() - lastAlertAt) / 1000.0f);
      netPublish(tSos, msg, false);
      lastAlertClass = -2;
    }
  }
  padWas = pad.on;
#if HAS_LDR
  lightRaw = analogRead(PIN_LDR);
  bool n = night ? lightRaw < LDR_LIGHT : lightRaw < LDR_DARK;
  if (n != night) { night = n; Serial.println(night ? "Night mode ON (dark): alerts vibrate twice, no flashing" : "Night mode OFF"); publishStatus(); }
#endif
}

// -------------------- alert check (motor -> mic) -----------------------
// Each alert is a known stimulus: while the motor runs, the mic should hear
// it. Room noise moves the background (floor) but not the motor-to-mic
// level; a failing mic or motor moves that level. That tells them apart.
void alertCheckFrame(float frameDb) {
  if (motorBusy && !chkActive) { chkActive = true; chkSum = 0; chkN = 0; chkFloor = det.floorDb; }
  if (chkActive && motorOnNow) { chkSum += frameDb; chkN++; }
  if (chkActive && !motorBusy) {
    chkActive = false;
    if (chkN < 3) return;                       // too short to judge (e.g. a feedback tap)
    float delta = chkSum / chkN - chkFloor;     // how loud the motor sounded above the background
    const char* verdict;
    if (delta < CHECK_HEARD_DB) {
      chkMissStreak++;
      verdict = chkMissStreak >= 2 ? "NOT HEARD" : "weak";
      if (chkMissStreak == 2) Serial.println("!!! ALERT CHECK: mic did not hear the motor twice - motor fault or band loose");
    } else {
      chkMissStreak = 0;
      if (chkRefN < 3) { chkRef = (chkRef * chkRefN + delta) / (chkRefN + 1); chkRefN++; verdict = "learning"; }
      else if (fabsf(delta - chkRef) > CHECK_DRIFT_DB) {
        verdict = "MIC DRIFT";
        Serial.printf("!!! ALERT CHECK: motor sounds %.1f dB vs usual %.1f dB while the room is %.0f dBFS - mic sensitivity changed\n", delta, chkRef, chkFloor);
      } else { chkRef = 0.9f * chkRef + 0.1f * delta; verdict = "ok"; }
    }
    chkState = verdict;
    Serial.printf("   alert check: motor heard %.1f dB above background (usual %.1f) -> %s\n", delta, chkRef, verdict);
    char msg[160];
    snprintf(msg, sizeof(msg), "{\"heard_db\":%.1f,\"ref_db\":%.1f,\"background\":%.1f,\"result\":\"%s\"}", delta, chkRef, chkFloor, verdict);
    netPublish(tCheck, msg, false);
  }
}

// ------------------------------ UI text --------------------------------
void printInfo() {
  Serial.println("------------------------------------------------");
  Serial.printf(" Mic       : %s (channel %d)\n", micFault ? "FAULT" : "OK", micCh);
  Serial.printf(" Unknown filter: %s (unknown above %.2f)\n", REJ_NAMES[rejLevel], knn.rejectDist > 1e8f ? 0.0f : knn.rejectDist);
  Serial.printf(" Sensitivity: %s (needs +%.0f dB and > %.0f dBFS)\n", SENS_NAMES[sensLevel], SENS_ON[sensLevel], SENS_ABS[sensLevel]);
  Serial.printf(" Background: %.1f dBFS   now: %.1f dBFS\n", det.floorDb, det.levelDb);
  Serial.printf(" Vibration : %s%s\n", muted ? "MUTED" : "on", night ? " (night mode)" : "");
  Serial.printf(" Worn      : %s\n", worn ? "yes" : "NO");
  Serial.printf(" Alert check: %s (usual %.1f dB)\n", chkState, chkRef);
  Serial.printf(" Wi-Fi     : %s   Phone link: %s\n", WiFi.status() == WL_CONNECTED ? "connected" : "not connected", mqttUp ? "connected" : "not connected");
  Serial.printf(" Phone app topic: %s/#   (broker broker.hivemq.com, port 1883)\n", topicBase);
  Serial.println(" Training examples / heard since power-on:");
  for (int c = 0; c < hh::NCLS; c++) if (CLASS_ON[c]) Serial.printf("   %-11s %d / %u\n", CLASS_NAMES[c], knn.m.cnt[c], eventCount[c]);
  Serial.println("------------------------------------------------");
}
void printSensors() {
#if HAS_KNOCK
  Serial.printf("Knock     : %u pulses so far, %u knocks (pin now %s)\n", (unsigned)knockEdges, knockCount, digitalRead(PIN_KNOCK) ? "HIGH" : "LOW");
#else
  Serial.println("Knock     : not fitted (HAS_KNOCK 0)");
#endif
  Serial.printf("Touch wear: raw %u (calibrated %.0f) -> %s\n", (unsigned)touchRead(PIN_WEAR), wear.base, wear.on ? "TOUCHED" : "free");
  Serial.printf("Touch pad : raw %u (calibrated %.0f) -> %s\n", (unsigned)touchRead(PIN_PAD), pad.base, pad.on ? "TOUCHED" : "free");
#if HAS_LDR
  Serial.printf("Light     : %d of 4095 -> %s\n", lightRaw, night ? "dark (night mode)" : "light");
#else
  Serial.println("Light     : not fitted (HAS_LDR 0)");
#endif
}
void printHelp() {
  Serial.println("Commands (type a letter, press Enter):");
  Serial.println("  t = start training     r = record one example   n = next sound   s = save training");
  Serial.println("  u = unknown-sound filter (fixed 4.0 / fixed 4.5 / auto strict / auto normal)");
  Serial.println("  q = self-check: how well each sound is learned");
  Serial.println("  v = sensitivity (high / medium / low) - lower = ignores more background noise");
  Serial.println("  m = mute/unmute        i = info           e = erase training");
  Serial.println("  l = live level on/off  w = sensor values  c = recalibrate touch (hands off!)");
  Serial.println("  h = help");
  Serial.println("BOOT button: tap = mute (or next sound in training), hold 2 s = train / save");
  Serial.println("Touch pad  : tap = acknowledge alert, hold 3 s = SOS");
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
  if (millis() < micQuietUntil) return;   // the vibration sensor already reported this as KNOCK
  float d = 0;
  int r = knn.classify(v, &d);
  const char* label; const uint16_t* pat;
  if (r >= 0 && !CLASS_ON[r]) r = hh::RES_UNKNOWN;   // a switched-off sound is never reported
  if (r >= 0)                    { label = CLASS_NAMES[r]; pat = PAT_CLASS[r]; eventCount[r]++; }
  else if (r == hh::RES_UNKNOWN) { label = "UNKNOWN SOUND"; pat = PAT_UNKNOWN; eventCount[hh::NCLS]++; }
  else                           { label = "LOUD SOUND"; pat = PAT_LOUD; eventCount[hh::NCLS + 1]++; }
  totalEvents++;
  alertUntil = millis() + 1000;
  lastAlertAt = millis(); lastAlertClass = r >= 0 ? r : -1;
  if (r == hh::RES_UNTRAINED)
    Serial.printf(">>> %-13s  level %.0f dBFS   (not trained yet - type t to train)\n", label, det.levelDb);
  else
    Serial.printf(">>> %-13s  match distance %.2f (unknown above %.2f)  peak %.0f dBFS%s%s\n",
                  label, d, knn.rejectDist > 1e8f ? 0.0f : knn.rejectDist, eventPeakDb(), muted ? "  [muted]" : "", worn ? "" : "  [band not worn]");
  Serial.flush();          // finish sending the text before the motor starts (motor noise can garble it)
  haptic(pat, false);
  char msg[200];
  snprintf(msg, sizeof(msg), "{\"sound\":\"%s\",\"distance\":%.2f,\"level\":%.1f,\"worn\":%s,\"muted\":%s,\"n\":%u}",
           label, d, det.levelDb, worn ? "true" : "false", muted ? "true" : "false", totalEvents);
  netPublish(tEvent, msg, false);
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
  while (trainIdx < hh::NCLS - 1 && !CLASS_ON[trainIdx]) trainIdx++;
  for (int c = 0; c < hh::NCLS; c++) trainedThisSession[c] = false;
  haptic(PAT_TICK, true);
  Serial.println("\n========== TRAINING MODE ==========");
  announceTrainSound();
  publishStatus();
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
  publishStatus();
}
void nextTrainSound() {
  if (mode != TRAIN) return;
  do { trainIdx++; } while (trainIdx < hh::NCLS && !CLASS_ON[trainIdx]);   // skip switched-off sounds
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
    if (!CLASS_ON[c]) continue;
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
  publishStatus();
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

void handleCommand(char ch, bool fromPhone) {
  switch (tolower(ch)) {
    case 't': enterTrain(); break;
    case 'r': armRecord(); break;
    case 'u': cycleRejectLevel(); break;
    case 'q': selfCheck(); break;
    case 'v': cycleSensitivity(); break;
    case 'n': nextTrainSound(); break;
    case 's': exitTrain(); break;
    case 'm': toggleMute(); break;
    case 'i': printInfo(); publishStatus(); break;
    case 'e':
      if (fromPhone) { Serial.println("(erase is only allowed from the laptop)"); break; }
      knn.clear(); saveModel(); Serial.println("Training ERASED. Every loud sound now gives LOUD SOUND."); break;
    case 'l': showLevel = !showLevel; break;
    case 'w': printSensors(); break;
    case 'c': touchCalibrate(wear); touchCalibrate(pad); worn = false; Serial.println("Touch recalibrated."); printSensors(); break;
    case 'h': printHelp(); break;
    default: break;   // ignore Enter, spaces, etc.
  }
}
void handleSerial() {
  while (Serial.available()) handleCommand((char)Serial.read(), false);
  char c;
  while (xQueueReceive(cmdQ, &c, 0) == pdTRUE) {
    if (c > ' ') Serial.printf("[phone] %c\n", c);
    handleCommand(c, true);
  }
}

void printLevel() {
  int n = (int)((det.levelDb + 90.0f) / 60.0f * 40.0f);
  if (n < 0) n = 0;
  if (n > 40) n = 40;
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
#if HAS_EXT_LED
  pinMode(PIN_EXT_LED, OUTPUT); digitalWrite(PIN_EXT_LED, LOW);
#endif
#if HAS_BUZZER
  pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW);
#endif
#if HAS_KNOCK
  pinMode(PIN_KNOCK, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_KNOCK), knockISR, CHANGE);
#endif
  digitalWrite(PIN_LED, HIGH);

  hapticQ = xQueueCreate(1, sizeof(HapticJob));
  cmdQ = xQueueCreate(16, sizeof(char));
  xTaskCreate(hapticTask, "haptic", 2048, NULL, 2, NULL);

  Serial.println("\n\n==================================================");
  Serial.println("  HaptiHear IoT - connected haptic hearing aid");
  Serial.println("==================================================");
  netBegin();
  Serial.printf("Connecting to phone hotspot \"%s\" in the background...\n", WIFI_SSID);
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

  touchCalibrate(wear); touchCalibrate(pad);
  worn = false;
  Serial.println("  Touch sensors ...... calibrated (now put the band on)");
#if HAS_LDR
  analogReadResolution(12);
  lightRaw = analogRead(PIN_LDR);
  Serial.printf("  Light sensor ....... %d of 4095\n", lightRaw);
#endif
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
  Serial.printf("  Phone app topic .... %s/#\n\n", topicBase);
  printHelp();
  Serial.println("\nListening...");
  delay(800);
  det.reset();
  readFrame();   // discard audio buffered during the delay
}

// --------------------------------- loop --------------------------------
void loop() {
  if (!readFrame()) {
    if (!micFault) { Serial.println("!!! MIC FAULT: I2S read timeout"); micFault = true; publishStatus(); }
    ledUpdate();
    return;
  }
  frameNo++;
  toFloat(micCh);

  hh::Frame fr;
  dsp.process(xbuf, fr);

  // microphone watchdog: ~2 s of digital silence = mic disconnected
  if (fr.db < -110.0f) {
    if (++silentFrames > 60 && !micFault) { micFault = true; Serial.println("!!! MIC FAULT: no signal - check the mic wires"); publishStatus(); }
  } else {
    silentFrames = 0;
    if (micFault) { micFault = false; Serial.println("Mic recovered. Listening..."); publishStatus(); }
  }

  handleButton();
  if (frameNo % 4 == 0) sensorsUpdate();     // touch + light every ~128 ms
  if (!micFault) alertCheckFrame(fr.db);

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
  if (frameNo % 8 == 0) netLoop();
  ledUpdate();
  if (mode == TRAIN && trainArmed && det.state != 1 && millis() >= trainArmUntil) {
    trainArmed = false;
    Serial.println("   nothing heard in 10 s - type r to try again (a bit louder or closer)");
  }
  if (showLevel && frameNo % 8 == 0) printLevel();
}
