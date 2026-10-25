# HaptiHear — Haptic Hearing-Aid Wearable (ESP32 + on-device ML)

> **Handoff document.** This file holds the full context of the project as of
> **Thursday 8 Oct 2026**, so a new assistant (e.g. Claude Code) can continue
> without the original conversation. Read it fully before changing anything.

---

## 1. Who / what / when

| | |
|---|---|
| Owner | Shreya — undergraduate CS student at VIT (Vellore Institute of Technology) |
| Purpose | Course IoT project, to be shown at a course review (demo + viva) |
| Build window | **Review on 25 Oct 2026** (she said 19th on 11 Oct, then 25th on 13 Oct, confirm). Has 1 kΩ + 1N4007. **Transistor ordered online, arriving 21 Oct**. Plan: build mic + ML first, add motor circuit when the transistor arrives |
| Owner's hardware level | Self-described as **weak at hardware**. Explain hardware steps very simply and literally (which hole, which wire, which direction). |
| Owner's main ask | Answers must be **correct the first time**; she has very little time. Prefer verified, conservative solutions over clever ones. |
| Budget | **Whole project under ₹2000; money is tight.** No optional purchases. She keeps her full-size 830-point breadboard (do NOT suggest a smaller one). Wearable = wrist part (motor + mic, tape or a band) + breadboard/ESP32/power bank in a pouch or bag she owns |
| Development tool | **Arduino IDE 2.3.10** on Windows 11, esp32 core **3.3.12**. ESP32 = **COM5** (CP210x Universal driver installed 15 Oct; COM3/COM4 are Bluetooth, opening them freezes the Serial Monitor). **Test1 uploaded and printing OK on 15 Oct** (no motor wired yet). **Test2 OK**: mic on slotA, background about −65 to −50 dBFS (louder than the expected −85…−70; possibly a fan or talking), slotB flat −120. Motor wires extended by a shop with red/black wires, ends not yet seen |
| Soldering | **She has NO soldering access.** Only no-solder solutions; mic header pins to be soldered by a shop/lab (see `BUILD_GUIDE.md`) |
| Checked from photos (8 Oct) | ESP32 headers factory-soldered ✓ · INMP441 header strips (2×3, yellow) loose, NOT soldered · motor is a coin ERM with very short thin red/blue wires → shop to solder mic pins + motor wires onto M-M jumpers |

**What the device does:** a wearable that listens through a microphone,
recognises important sounds (doorbell, someone's voice, alarm, knock) **on the
ESP32 itself**, and tells a deaf / hard-of-hearing wearer what it heard through
**distinct vibration patterns**. It includes a **sensor self-diagnosis**
feature (mic health check + runtime fault detection).

---

## 2. Hardware — what she ACTUALLY has (current, authoritative)

| Part | Notes |
|---|---|
| ESP32 DevKit, micro-USB ("B type"), **CP2102** USB chip | ESP32-WROOM-32. Arduino board: *ESP32 Dev Module* |
| INMP441 I2S MEMS microphone | Header pins may need soldering — unsoldered pins = random failures |
| Mobile-phone vibration motor | Small ERM coin/cylinder motor, ~3 V, ~60–100 mA. Thin wires: solder or twist+tape onto jumper pins |
| Breadboard (one) | Used only as a junction box (ESP32 stays OFF the breadboard) |
| Jumper wires: male-to-female and male-to-male | No female-to-female |
| Micro-USB cable | Must be a data cable |

### Must buy (told to her; required — project can't be completed safely without them)

| Part | Value | Alternatives |
|---|---|---|
| NPN transistor | **S8050** | 2N2222A / PN2222A (same E-B-C leg order). **P2N2222A** has C-B-E order — swap outer legs |
| Resistor | **1 kΩ** (brown-black-red) | anything 470 Ω – 2.2 kΩ |
| Diode | **1N4007** | 1N4148, 1N5819 |

Reason: an ESP32 GPIO can safely source ~20 mA; the motor needs ~60–100 mA.
The transistor switches the motor current from the 3V3 rail; the diode
absorbs the motor's inductive kick. BC337/BC547 were deliberately NOT
suggested (C-B-E pinout → confusion).

Optional: phone power bank to wear it without a laptop.

### Parts that were planned earlier but she does NOT have (do not reintroduce unless she buys them)

SSD1306 OLED, DRV2605L haptic driver, external LED, external push button,
3.7 V LiPo, TP4056 charger, MT3608 boost converter. An earlier firmware
version (v1, using OLED + DRV2605L + battery chain, LED on G4, button on G15,
I2C SDA G21 / SCL G22) was written and compiled, then **superseded**. Its
replacements:

| Old (v1) | Now (v2) |
|---|---|
| SSD1306 OLED | **Arduino IDE Serial Monitor** (115200 baud) is the display — her explicit choice |
| DRV2605L + motor | Motor via **S8050 transistor** on GPIO 4 |
| External LED on G4 | **On-board blue LED, GPIO 2** |
| External button on G15 | **On-board BOOT button, GPIO 0** |
| LiPo → TP4056 → MT3608 | USB power (laptop or power bank) |

---

## 3. Pin map (v2 — matches the code)

| Function | ESP32 GPIO | Board label (may read D26 / G26 / IO26 / 26) |
|---|---|---|
| INMP441 SCK (bit clock) | 26 | D26 |
| INMP441 WS (word select) | 25 | D25 |
| INMP441 SD (data) | 33 | D33 |
| Motor transistor base (via 1 kΩ) | 4 | D4 |
| Status LED (on-board, blue) | 2 | — |
| Button (on-board BOOT) | 0 | — |

Do not move I2S pins without reason: they match her existing documentation.

---

## 4. Wiring (breadboard used as junction box)

ESP32 sits on the table; **male-to-female** jumpers go from ESP32 pins and
mic pins into shared breadboard rows. Row numbers are examples; what matters
is which things share a row.

**From ESP32:** 3V3 → + rail · GND → − rail · D26 → row 10 · D25 → row 12 ·
D33 → row 14 · D4 → row 26

**From INMP441:** VDD → + rail (**3.3 V only, never 5V/VIN**) · GND → − rail ·
**L/R → − rail** (left channel; must not float) · SCK → row 10 · WS → row 12 ·
SD → row 14

**Motor driver** (transistor flat face toward you, legs down = E, B, C):

| Spot | Contents |
|---|---|
| row 20 | transistor **E** + M-M jumper to − rail |
| row 21 | transistor **B** + resistor leg 1 |
| row 22 | transistor **C** + motor wire 1 + diode plain (anode) leg |
| row 26 | resistor leg 2 (+ D4 jumper) |
| + rail | motor wire 2 + diode **striped** (cathode) leg |

Schematic in words: `3V3 → motor → collector`, `emitter → GND`,
`D4 → 1 kΩ → base`, diode across the motor with **cathode (stripe) to 3V3**.
A reversed diode shorts 3V3 whenever the motor turns on — always warn about this.
Motor polarity doesn't matter. Motor powered from 3V3 (not VIN/5V: phone motors are ~3 V).

---

## 5. Software setup

- Arduino IDE 2; Boards Manager URL
  `https://espressif.github.io/arduino-esp32/package_esp32_index.json`;
  install **"esp32 by Espressif Systems" 3.x** (not "esp32 by Arduino").
- Board: **ESP32 Dev Module**. Port: the CP210x COM port (driver from silabs.com if missing).
- **No external libraries** are needed for v2 (only the ESP32 core: Preferences, I2S IDF driver).
- Serial Monitor / Plotter at **115200**.
- Upload stuck on `Connecting.....` → hold BOOT until `Writing at…`.
- Never hold BOOT while pressing EN except to force upload mode (GPIO 0 is a strapping pin).

---

## 6. Repository layout

```
HaptiHear_v2/
├── README.md                     ← this file
├── README.txt                    ← short upload-order note for the user
├── Test1_Motor_Button/
│   └── Test1_Motor_Button.ino    ← motor patterns every 3 s, LED blink, BOOT prints
├── Test2_Microphone/
│   └── Test2_Microphone.ino      ← Serial Plotter: dBFS of both I2S slots
├── HaptiHear/
│   ├── HaptiHear.ino             ← FINAL firmware (hardware, UI, training, haptics)
│   └── hh_dsp.h                  ← pure C++ DSP + event detector + kNN (no Arduino deps)
└── tools/
    └── sim_test.cpp              ← desktop test of hh_dsp.h with synthetic sounds
```

Arduino rule: each `.ino` must live in a folder with the same name;
`hh_dsp.h` must stay next to `HaptiHear.ino`.

**Build order for the user:** Test1 → Test2 → HaptiHear.

---

## 7. How the firmware works

### 7.1 Audio capture (`HaptiHear.ino`)
- ESP-IDF 5 **standard I2S driver** (`driver/i2s_std.h`), master RX, Philips
  format, **16 kHz, 32-bit slots, stereo**. DMA: 8 descriptors × 480 frames
  (≈240 ms buffering) so short stalls in the main loop never drop audio.
- INMP441 gives 24-bit data left-aligned in 32-bit words → `>> 8`, scaled by 2^23.
- **Channel auto-detect:** both slots are read; the slot with higher variance
  is the mic. (ESP32 left/right slot order quirks made this the safe choice.)
- `gpio_pulldown_en(33)` after I2S init (INMP441 datasheet recommends a
  pull-down on SD): unused slot and a disconnected wire read as clean zeros.
- Compile guard: `#error` if `ESP_ARDUINO_VERSION_MAJOR < 3`.

### 7.2 Per-frame DSP (`hh_dsp.h`, `FrameDSP`)
- Frame N = 512 samples (32 ms), per-frame DC removal, **Hann window**,
  in-house iterative **radix-2 FFT** (no library dependency).
- 16 **mel-spaced bands**, 100–7900 Hz → band power in dB.
  Bin edges: `3 7 12 18 24 31 40 50 62 75 91 109 130 154 182 215 253`.
- Frame level `db` (dBFS of RMS) and **tonality** = peak-to-mean power ratio
  in 300 Hz–4 kHz (dB).

### 7.3 Event detector (`EventDetector`)
- Adaptive background floor: falls fast (`0.9/0.1` smoothing), rises slowly
  (`0.005`, or `0.001` while loud); frozen while capturing; min −100 dB.
- **Onset** when frame dB > floor + **12 dB** (`ON_DB`) and > **−75 dBFS** (`ABS_MIN_DB`, ≈45 dB SPL).
- Captures a **32-frame (~1 s) window**: 4 frames before onset + 27 after.
- **Cooldown** until 8 quiet frames (~0.25 s) or 62 frames (~2 s) max →
  a continuous alarm re-alerts about every 3 s instead of spamming.
- (Cooldown was raised from 3 to 8 quiet frames after simulation showed beeping
  alarms double-triggering.)

### 7.4 Features (20 numbers)
- 16 × spectral shape: mean over active frames (dB > floor + 6) of
  band dB minus that frame's mean band dB → loudness-independent.
- Duration: active frames / 32.
- Impulsiveness: max frame dB − mean active dB.
- Bursts: count of inactive→active transitions (beeps, knocks).
- Tonality: mean over active frames.

### 7.5 Classifier — on-device kNN (`KNN`)
- 4 classes × up to 8 examples × 20 floats = `ModelData` **2,568 bytes**,
  stored in NVS via `Preferences` (namespace `haptihear`, key `model`, magic `HHK1`).
- z-score normalisation from training data; per-dimension std floors
  (bands 2.0 dB, duration 0.08, impulsiveness 1.5, bursts 0.5, tonality 1.5).
- Weighted Euclidean distance (band dims ×1, the 4 temporal dims ×2).
- **k = 3**, inverse-distance weighted vote.
- **Unknown rejection:** threshold = 3 × median(within-class nearest-neighbour
  distance) + 2. Needs ≥ 3 such distances, else rejection is off.
  **21 Oct:** factor/offset are now `KNN::rejFactor/rejOffset` (defaults 3.0/2.0, so sim_test is unchanged).
  Sketches set levels via `u`: NORMAL 3.0/2.0, **STRICT 2.0/1.0 (default)**, VERY STRICT 1.5/0.8, saved in NVS key `rejlvl`.
  Reason: on her real data a song scored 3.7–4.0 (labelled DOG BARK) vs 0.7–2.3 for true sounds, with NORMAL limit 5.38.
  **21 Oct night:** real-room results were poor (alarm mid-stream → UNKNOWN 3.4–4.9 under STRICT). Changes: `MAXS` 8 → **12**
  (ModelData now 5,772 B; old saved model no longer loads → full retrain needed; NVS is 20 KB), `KNN::classifyEx` (skip one example)
  and a **`q` leave-one-out self-check** printing per-sound right/unknown/confused-with. Training prompt now says 8–10 examples.
  Advice given: vary examples (start + middle of long sounds, 20 and 40 cm), phone flat on the table, fixed volume.
  sim_test.cpp still NOT re-run (no desktop compiler).
  **Her request (21 Oct): fixed unknown limit.** `KNN::rejFixed` (> 0 overrides automatic; default 0 keeps sim behaviour).
  `u` now cycles FIXED 4.0 / **FIXED 4.5 (default)** / AUTO STRICT (2·med+1) / AUTO NORMAL (3·med+2), saved in NVS key `rejmode`
  (old key `rejlvl` unused). Told her: higher limit = fewer false UNKNOWN but songs (3.7–4.0) get a class name again;
  distance values shift after retraining because normalisation changes.
  **22 Oct: false triggers with nothing playing** (hostel noise + lenient 4.5 limit). `EventDetector::onDb/absMinDb`
  (defaults = ON_DB/ABS_MIN_DB, sim unchanged). `v` cycles HIGH (+12 dB, > −75 dBFS) / **MEDIUM (+15, > −60, default)** /
  LOW (+18, > −52), NVS key `sens`. Click filter: events with < 3 active frames ignored (training says "too short").
  Every result and training line now prints the event's peak dBFS, for tuning from her screenshots. Model format unchanged.
- Untrained (0 examples) → every event reported as `LOUD SOUND`.
- **Record-on-command training (21 Oct, her request):** in TRAIN mode a sound only counts if its onset comes within 10 s (`TRAIN_ARM_MS`) after she types `r`; one `r` = one example, then it disarms and waits. Timeout prints "nothing heard". `r` also works from the phone (v3). Capture window is still ~1 s (must match run mode). Replaces an earlier 3 s auto-gap she found too fast.
- Adding to a full class drops its oldest example. Retraining a class clears
  its old examples on the first new recording in that session; skipped classes keep theirs.
- **Why kNN, not a neural net:** trains on-device in ms from ~5 examples per
  sound, personalises to the user's real doorbell/voice, no dataset, no cloud,
  fits the weekend. A TinyML neural net (Edge Impulse / TFLite Micro) is listed
  as future work. Be honest in the viva: this is classic on-device ML.

### 7.6 Haptics
- Motor on GPIO 4 via transistor; patterns are ON/OFF millisecond lists ending in 0.
- Played by a **separate FreeRTOS task** (`hapticTask`, priority 2, queue of 1
  with `xQueueOverwrite`) so timing is exact while the 32 ms audio loop runs.

| Event | Pattern (ms ON, OFF, …) | Feels like |
|---|---|---|
| DOORBELL | 150,100,150,400,150,100,150 | two double-pulses |
| VOICE | 700 | one long buzz |
| ALARM | 120,80 ×4,120 | five fast pulses |
| KNOCK | 80,150,80,150,80 | three short taps |
| UNKNOWN | 250 | one short buzz |
| LOUD (untrained) | 400 | medium buzz |
| Tick (feedback) | 80 | tap |
| Boot / saved | 100,100,100,100,100 | three pulses |

- **Self-trigger protection:** the mic is next to the motor. Any sound whose
  *onset* happens while the motor is active (or ≤ 0.3 s after), or while BOOT
  is pressed (+0.4 s), is discarded (`eventTainted`). Prevents alert→alert loops.

### 7.7 User interface
- **Serial Monitor** (115200) is the display: self-test lines, help, training
  prompts, `>>> DOORBELL  match distance 1.84 (unknown above 5.9)` lines.
- Commands (single letters, Enter/whitespace ignored, case-insensitive):
  `t` train · `n` next sound · `s` save · `m` mute · `i` info · `e` erase ·
  `l` live level bar (40-char, every ~256 ms) · `h` help.
- **BOOT button:** tap = mute (RUN) / next sound (TRAIN); hold 2 s = enter
  training (RUN) / save (TRAIN). Sampled once per 32 ms frame; ≥ 50 ms press counts.
- **Blue LED:** 100 ms blip every 3 s = alive · solid 1 s = sound detected ·
  slow blink (250 ms) = training · fast blink (100 ms) = mic fault.

### 7.8 Self-diagnosis
- Boot: discard 12 frames (mic start-up), then 16 frames; fail if RMS < −110 dBFS
  (no signal) or > 50 % samples clipped (stuck line). On fail: print exact wiring
  hints and retry every 2 s, LED fast-blinks.
- Runtime: > 60 consecutive frames (~2 s) below −110 dB → `!!! MIC FAULT`;
  auto-recovers with `Mic recovered`. I2S read timeout also flags a fault.

---

## 8. Verification done (in a Linux sandbox, not on her hardware)

- All v2 sketches compiled with **arduino-cli + esp32:esp32 core 3.3.12**,
  `--warnings all`, using **Arduino's real ctags** for prototype generation:
  **0 errors, 0 warnings**.
  - HaptiHear: 311,420 B flash (23 %), 42,860 B RAM (13 %)
  - Test1: 20 % flash · Test2: 21 % flash
- `tools/sim_test.cpp` (g++ -std=c++17) on `hh_dsp.h`:
  FFT peak at bin 32 for 1 kHz (expected), frame dB −3.0 for full-scale sine
  (expected), **0 events in silence**, **160/160 correct** on synthetic
  doorbell/voice/alarm/knock in a quiet room (−80 dBFS bg) and in a noisy room
  (−58 dBFS bg), 0 missed, 0 extra triggers. White-noise bursts rejected as
  Unknown 20/20 in the quiet room, 0/20 in the noisy room (known limitation).
- **Not verified:** anything on physical hardware. Real rooms will be less
  accurate than synthetic tests; more training examples fix most confusions.

Gotchas found while setting up the toolchain (relevant if Claude Code compiles):
- `espressif.github.io` index may be blocked; the raw GitHub copy
  `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` works.
- Core 3.x depends on `arduino:dfu-util`; offline you can strip that dependency from the index.
- Debian's exuberant-ctags generates broken prototypes; use Arduino's ctags
  (`github.com/arduino/ctags`, builds after renaming its `__unused__`/`__printf__` macros).
- Avoid **default arguments** in `.ino` functions (Arduino's auto-prototypes can break on them).

---

## 9. Build plan given to the user

| When | Step | Program | Done when |
|---|---|---|---|
| Before Fri | Buy S8050 + 1 kΩ + 1N4007; solder mic pins; prepare motor wires; test USB cable | — | parts in hand |
| Fri 18:00 | Software setup | — | COM port + ESP32 Dev Module selected |
| Fri 18:45 | Motor circuit | Test1_Motor_Button | 5 patterns felt; BOOT prints |
| Fri 20:00 | Microphone | Test2_Microphone | one plotter line jumps on clap, other flat at −120 |
| Sat 10:00 | Final program + train 4 sounds (5–6 examples each) | HaptiHear | sounds recognised |
| Sat 12:00 | Make wearable (motor on skin, mic outward, tape legs, power bank) | HaptiHear | nothing loose |
| Sat 15:00 | Demo rehearsal | HaptiHear | demo runs twice |

**Demo script (5 min):** EN + self-test → `l` level bar → explain untrained
behaviour (don't erase) → retrain doorbell live (`t`, 5×, `s`; other classes
keep training) → classify all four, examiner guesses by vibration →
jingle keys → `UNKNOWN` → pull mic SD jumper → `MIC FAULT`, reconnect → recovers.

**Viva points:** 16 kHz (Nyquist 8 kHz covers speech/alarms); I2S = digital,
no ADC noise; FFT/mel features; kNN + rejection; model 2.5 KB; transistor
(20 mA GPIO vs ~80 mA motor, base current ≈ (3.3 − 0.7)/1 k ≈ 2.6 mA,
flyback diode); FreeRTOS haptic task; self-trigger suppression; limitations
(one mic → no direction, noisy rooms, USB power, on/off vibration only).

---

## 10. Open items / possible next work

- [x] **21 Oct: now 6 classes** (her faculty wants variety): DOORBELL, VOICE, ALARM, KNOCK,
      PHONE RING (400,200,400) and COOKER (100,100,100,100,800). **KNOCK renamed DOG BARK** (phone audio, same in hostel and lab; class 4, same 3-tap pattern). `#define HH_NCLS 6` in
      `HaptiHear.ino` before `#include "hh_dsp.h"` (hh_dsp.h defaults to 4, so `sim_test.cpp`
      still tests 4). `static_assert`s check CLASS_NAMES / PAT_CLASS have HH_NCLS rows. Test1 also
      plays the 2 new patterns. Compiled on her laptop with the IDE's bundled arduino-cli
      (`D:/ArduinoIDE/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe`,
      `--config-file C:/Users/shrey/.arduinoIDE/arduino-cli.yaml`): 0 warnings, 311,508 B flash (23 %),
      44,164 B RAM. **sim_test NOT re-run** (no desktop g++ on her laptop); KNN code is NCLS-generic.
      Ringtone vs ALARM may confuse: train with a melodic ringtone.
- [ ] Hardware not yet built; first real-world tuning may be needed:
      `ON_DB` (sensitivity), `ABS_MIN_DB`, rejection factor 3.0 in `KNN::finalize()`.
- [ ] If the motor feels weak: could add PWM strength control (LEDC) — not done.
- [ ] Possible upgrades if she buys parts later: OLED (v1 code exists),
      DRV2605L, LiPo + TP4056 + MT3608 (MT3608 must be set to 5.0 V with a
      multimeter BEFORE connecting — ships at arbitrary, sometimes > 20 V output).
- [ ] Future-work ideas: TinyML CNN via Edge Impulse, two mics for direction,
      BLE phone app, battery gauge.
- Earlier "on the horizon" idea from her notes: a Wokwi `diagram.json` for
  simulation/documentation (Wokwi has no INMP441 part; would be partial).

## 10b. v3 = `HaptiHear_IoT/` (added 21 Oct for the rubric; v2 `HaptiHear/` kept as fallback)

Rubric (both Review II and III are on 25 Oct): Review II needs >= 5 sensors, >= 3 actuators,
pin/circuit/architecture diagrams, front/back-end spec, code, test screenshots. Review III marks
Things, **Network (MQTT/HTTP)**, Application (data use), Novelty, Presentation.

v3 adds, at zero hardware cost: Wi-Fi to her **phone hotspot** (SSID `HaptiHear`, pass
`haptihear123`; hostel Wi-Fi has a login portal) + **MQTT** via the core's built-in esp-mqtt
(`mqtt_client.h`, non-blocking `esp_mqtt_client_enqueue`) to `broker.hivemq.com:1883`.
Topics `haptihear/<last 3 MAC bytes>/{event,status(retained, LWT offline),check,sos,cmd}`;
phone front end = "IoT MQTT Panel" app. Phone commands go through a FreeRTOS queue; `e` (erase)
refused from the phone. Touch sensors (ESP32 built-in): **wear** on GPIO27 (wire on skin),
**pad** on GPIO32 (tap = ack last alert, hold 3 s = SOS). Calibrated at boot (band off!), `c`
recalibrates, `w` prints raw values; "touched" = >25 % change from calibration (either direction).
**Alert check** (her friend's novelty idea, simplified): mean mic dB while motor pin HIGH minus
detector floor; < 6 dB twice = NOT HEARD (motor fault / loose band); after 3 good checks a
reference is learned, > 8 dB change = MIC DRIFT. Unverified on hardware: depends on the mic
actually hearing the coin motor. Optional parts behind flags (default 0): `HAS_LDR` (GPIO34 ADC1,
night mode = alerts twice, no flash), `HAS_EXT_LED` (GPIO18, mirrors motor), `HAS_BUZZER`
(GPIO19 via 2nd transistor, SOS only). Same NVS namespace/model as v2, so training carries over.
Compiled 21 Oct with flags 0 and all 1: 0 warnings, ~1,051,568 B flash (80 %), 70 KB RAM.
Her written document reportedly mentions an Isolation Forest: the code has none; told her to
fix the document.

**22 Oct hardware results:** motor circuit works (tested first without the diode, then diode added). Her phone is an
**iPhone**: hotspot name = phone name (set to `HaptiHear`), needs "Maximize Compatibility" ON (2.4 GHz). Wi-Fi + MQTT
connected. **Alert check works on real hardware:** motor heard 6–13 dB above background. Played sounds peak −16…−31 dBFS.
Fixes: topic ID now from `ESP.getEfuseMac()` (`WiFi.macAddress()` gave 000000 that early); result line is printed and
`Serial.flush()`ed before the motor starts (motor noise garbled one UART line). App: IoT MQTT Panel, broker.hivemq.com:1883 TCP.
After an EN reset the ESP32 did not rejoin the iPhone hotspot (iPhone hides it when idle): netLoop now re-runs
WiFi.begin every 15 s while offline and prints "looking for hotspot". The iOS app has "Text Output" (no "Text Log"):
Alerts panel = topic `haptihear/bf1504/event`, JsonPath `$.sound`, history + timestamp ticked.
Panels so far: Alerts, Mute (button, payload `m`), Mic status (`$.mic`), SOS (`/sos`, `$.sos`). Touch wear + SOS confirmed working.
**SW-420 knock sensor (22 Oct, her choice for sensor #5):** `HAS_KNOCK` (default 0), DO on **GPIO35** (input-only),
CHANGE interrupt counts pulses; checked every ~128 ms; 2 separate bursts within 2 s = KNOCK (3 s lockout), ignored
while the motor runs / in training. Pattern `PAT_KNOCK` 500,150,100,100,100. Publishes `{"sound":"KNOCK","source":"vibration"}`
on `/event`; mic events are ignored for 1.5 s after a knock; status JSON gains `knocks`. Compiled flags 0 and all 1: 0 warnings,
80–81 % flash. Rubric count with SW-420 + external LED: sensors = mic, wear touch, SOS touch, BOOT, SW-420 (5);
actuators = motor, on-board LED, external LED (3). She has 1 M-F jumper left; SW-420 needs 4, LED 1.
**23 Oct, built and working:** external LED is wired **from the D4 row** (new resistor D4-row -> LED row, LED short leg
-> transistor E row = GND), so it mirrors the motor with **no code** (`HAS_EXT_LED` stays 0, GPIO18 unused). Her breadboard's
two halves have row numbers running in opposite directions, and the two long edge strips are separate: describe positions
by parts/wires ("same line as the ESP32 3V3 wire"), not row numbers. SW-420 plugged into the breadboard (pins along the
long edge), M-F/M-M to the 3V3/GND lines, DO -> D35; `HAS_KNOCK 1`; potentiometer adjusted; knock detection confirmed.
Final count: 5 sensors (mic, wear touch, SOS touch, BOOT, SW-420), 3 actuators (motor, on-board LED, external LED).
**24 Oct: VOICE switched off (her request, no time to train it).** `CLASS_ON[]` in HaptiHear_IoT.ino (VOICE false):
skipped in training (t/n jump over it), q and i; a VOICE result is reported as UNKNOWN. NCLS stays 6 so the saved model
still loads (removing the class would have wiped her training). q before retrain: DOORBELL 7/12 (as DOG BARK / PHONE RING),
ALARM 11, DOG BARK 11, PHONE RING 10, COOKER 12, overall 85 %. Advised: retrain DOORBELL with one clip, DOG BARK with short
bursts. Deck updated (5 recognised sounds, 8 vibration patterns). Compiled: 0 warnings.

## 11. Rules for whoever continues

1. Keep pins as in §3 unless she asks.
2. Never suggest 5 V for the INMP441 or driving the motor straight from a GPIO.
3. Compile-check every change (arduino-cli, core 3.x) and re-run `tools/sim_test.cpp` after touching `hh_dsp.h`.
4. Explain hardware steps literally; give one action per step and a "done when" check.
5. Don't claim hardware behaviour was tested — only compile + simulation were.
