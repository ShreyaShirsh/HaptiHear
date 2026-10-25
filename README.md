# HaptiHear — Haptic Hearing-Aid Wearable (ESP32 + on-device ML)

---

## 1. Who / what / when

| | |
|---|---|
| Owner | Shreya — undergraduate CS student at VIT (Vellore Institute of Technology) |
| Collaborator | Rachel - undergraduate CS student at VIT (Vellore Institute of Technology) |
| Purpose | Course IoT project, to be shown at a course review (demo + viva) |
| Build window | **Review on 25 Oct 2026** (she said 19th on 11 Oct, then 25th on 13 Oct, confirm). Has 1 kΩ + 1N4007. **Transistor ordered online, arriving 21 Oct**. Plan: build mic + ML first, add motor circuit when the transistor arrives |
| Owner's hardware level | Self-described as **weak at hardware**. Explain hardware steps very simply and literally (which hole, which wire, which direction). |
| Owner's main ask | Answers must be **correct the first time**; she has very little time. Prefer verified, conservative solutions over clever ones. |
| Budget | **Whole project under ₹2000; money is tight.** No optional purchases. She keeps her full-size 830-point breadboard (do NOT suggest a smaller one). Wearable = wrist part (motor + mic, tape or a band) + breadboard/ESP32/power bank in a pouch or bag she owns |
| Development tool | **Arduino IDE 2.3.10** on Windows 11, esp32 core **3.3.12**. ESP32 = **COM5** (CP210x Universal driver installed 15 Oct; COM3/COM4 are Bluetooth, opening them freezes the Serial Monitor). **Test1 uploaded and printing OK on 15 Oct** (no motor wired yet). **Test2 OK**: mic on slotA, background about −65 to −50 dBFS (louder than the expected −85…−70; possibly a fan or talking), slotB flat −120. Motor wires extended by a shop with red/black wires, ends not yet seen |

**What the device does:** a wearable that listens through a microphone,
recognises important sounds (doorbell, someone's voice, alarm, knock) **on the
ESP32 itself**, and tells a deaf / hard-of-hearing wearer what it heard through
**distinct vibration patterns**. It includes a **sensor self-diagnosis**
feature (mic health check + runtime fault detection).

---

## 2. Hardware(current, authoritative)

| Part | Notes |
|---|---|
| ESP32 DevKit, micro-USB ("B type"), **CP2102** USB chip | ESP32-WROOM-32. Arduino board: *ESP32 Dev Module* |
| INMP441 I2S MEMS microphone | Header pins may need soldering — unsoldered pins = random failures |
| Mobile-phone vibration motor | Small ERM coin/cylinder motor, ~3 V, ~60–100 mA. Thin wires: solder or twist+tape onto jumper pins |
| Breadboard (one) | Used only as a junction box (ESP32 stays OFF the breadboard) |
| Jumper wires: male-to-female and male-to-male | No female-to-female |
| Micro-USB cable | Must be a data cable |

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
  
  Factor/offset are now `KNN::rejFactor/rejOffset` (defaults 3.0/2.0, so sim_test is unchanged).
  Sketches set levels via `u`: NORMAL 3.0/2.0, **STRICT 2.0/1.0 (default)**, VERY STRICT 1.5/0.8, saved in NVS key `rejlvl`.
  Reason: on her real data a song scored 3.7–4.0 (labelled DOG BARK) vs 0.7–2.3 for true sounds, with NORMAL limit 5.38.
  
  Real-room results were poor (alarm mid-stream → UNKNOWN 3.4–4.9 under STRICT). Changes: `MAXS` 8 → **12**
  (ModelData now 5,772 B; old saved model no longer loads → full retrain needed; NVS is 20 KB), `KNN::classifyEx` (skip one example)
  and a **`q` leave-one-out self-check** printing per-sound right/unknown/confused-with. Training prompt now says 8–10 examples.
  Advice given: vary examples (start + middle of long sounds, 20 and 40 cm), phone flat on the table, fixed volume.
  sim_test.cpp still NOT re-run (no desktop compiler).
  
  `KNN::rejFixed` (> 0 overrides automatic; default 0 keeps sim behaviour).
  `u` now cycles FIXED 4.0 / **FIXED 4.5 (default)** / AUTO STRICT (2·med+1) / AUTO NORMAL (3·med+2), saved in NVS key `rejmode`
  (old key `rejlvl` unused). Told her: higher limit = fewer false UNKNOWN but songs (3.7–4.0) get a class name again;
  distance values shift after retraining because normalisation changes.
  
  `EventDetector::onDb/absMinDb`
  (defaults = ON_DB/ABS_MIN_DB, sim unchanged). `v` cycles HIGH (+12 dB, > −75 dBFS) / **MEDIUM (+15, > −60, default)** /
  LOW (+18, > −52), NVS key `sens`. Click filter: events with < 3 active frames ignored (training says "too short").
  Every result and training line now prints the event's peak dBFS, for tuning from her screenshots. Model format unchanged.
- Untrained (0 examples) → every event reported as `LOUD SOUND`.

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

## 09. Rules for whoever continues

1. Keep pins as in §3 unless she asks.
2. Never suggest 5 V for the INMP441 or driving the motor straight from a GPIO.
3. Compile-check every change (arduino-cli, core 3.x) and re-run `tools/sim_test.cpp` after touching `hh_dsp.h`.
4. Explain hardware steps literally; give one action per step and a "done when" check.
5. Don't claim hardware behaviour was tested — only compile + simulation were.
