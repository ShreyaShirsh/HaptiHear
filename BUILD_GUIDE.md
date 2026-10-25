# HaptiHear: no-soldering build guide (step by step)

Follow this from top to bottom. Each step has a **Done when** check. If the check
fails, stop and fix it first (see the Troubleshooting table at the end).

You never need to solder anything yourself. The only part that needs soldering
is the **microphone header pins**. Step 0 explains how to get someone else to
do that in about 5 minutes.

---

## Step 0: One shopping trip (Thursday or Friday before 6 pm)

Take the **INMP441 microphone and its loose pin strip** with you to an
electronics shop (Katpadi / near VIT) or to the VIT electronics lab.

| # | Ask for | Price (approx.) | Why |
|---|---|---|---|
| 1 | **S8050** NPN transistor ×2 (one spare). If they don't have it: **2N2222A** or **PN2222A** | ₹2–5 each | Switches the motor on and off |
| 2 | **1 kΩ resistor** ×2 (brown-black-red stripes) | ₹1 each | Protects the ESP32 pin |
| 3 | **1N4007 diode** ×2. If not: 1N4148 or 1N5819 | ₹1–2 each | Absorbs the motor's voltage spike |
| 4 | "Please **solder the header pins** on this microphone module" | ₹20–50 | Unsoldered pins make the mic fail at random |
| 5 | "Please **solder each motor wire onto one male-to-male jumper** (bring 2 from your kit) and tape the joints" | ₹20–50 | Your coin motor's wires are very short and thin. This turns them into plugs for the breadboard. If done, **skip Step 1** |

Do NOT accept BC547 or BC337 as substitutes (their legs are in a different order).
If they only have **P2N2222A** (with a "P" at the start), take it, but write
"C-B-E" on a sticker. Its legs are reversed (see Step 4).

**If no shop can solder:** ask a friend in ECE/EEE, the lab assistant in any
electronics lab, or a hostel robotics/maker club. VIT labs all have soldering
irons. Plan C at the end of this file covers the case where nobody can, but
treat it as an emergency fallback only.

Also check these two things at home:
- **ESP32 board:** look underneath. You should see two rows of metal pins pointing
  down. These are almost always factory-soldered. If they are, you're fine.
- **USB cable:** plug the ESP32 into the laptop. A red power LED should light up and
  Windows should make the "device connected" sound. Charge-only cables light the
  LED but no sound plays. If that happens, use another cable.

**Done when:** you have the 3 parts, and the mic pins are soldered (shiny
solder blob around each pin, pins don't wiggle).

---

## Step 1: Prepare the motor wires (no soldering)

The phone motor has two very thin wires. You will turn each one into a plug.

1. If the wire ends have no bare metal, scrape about **1 cm** of the coating off
   with a blade or your fingernail. (Many motors already have bare tips.)
2. Fold the bare end in half, then in half again, so it becomes a short, thick
   tip about 3 mm long.
3. Take a **male-to-female** jumper. Push the folded tip firmly into its
   **female** end (the hole).
4. Wrap a small piece of tape (cello tape or insulation tape) around the joint.
5. Repeat for the second motor wire with a second M-F jumper.

The motor now has two **male** pins. These will go into the breadboard. Which
wire goes where doesn't matter, because the motor works either way round.

**Done when:** you pull each wire gently and it stays in.

---

## Step 2: Software setup (Friday about 18:00)

1. Open **Arduino IDE 2**.
2. **File → Preferences → Additional boards manager URLs**, paste:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json` → OK.
3. **Tools → Board → Boards Manager**, search `esp32`, install
   **"esp32 by Espressif Systems"** (version 3.x). Do not install "esp32 by Arduino".
4. Plug in the ESP32. **Tools → Board → esp32 → ESP32 Dev Module**.
5. **Tools → Port**, choose the COM port that appeared (e.g. COM5).
   If no port appears, install the **CP210x driver** from silabs.com
   ("CP210x Universal Windows Driver"), then replug.

**Done when:** a COM port is selected and the board is "ESP32 Dev Module".

---

## Step 3: Understand the breadboard (2 minutes, important)

```
   +  ─────────────────────────────  ← red line  = "+ rail" (all holes connected left↔right)
   −  ─────────────────────────────  ← blue line = "− rail"
        1  2  3 ... 10 ... 20 21 22 ... 26 ... 30
   a    ●  ●  ●     ●      ●  ●  ●      ●
   b    ●  ●  ●     ●      ●  ●  ●      ●      ← In one numbered column, holes a,b,c,d,e
   c    ●  ●  ●     ●      ●  ●  ●      ●        are connected to each other.
   d    ●  ●  ●     ●      ●  ●  ●      ●        Different numbers are NOT connected.
   e    ●  ●  ●     ●      ●  ●  ●      ●
   ───────── middle gap ─────────
   f-j  (not used)
```

- "Row 20" in this guide means **holes a20 to e20**. Anything in a20…e20 is connected.
- Use the **+ / − rails on the side next to a–e**.
- Some long breadboards have the rail line **broken in the middle**. Keep everything
  between columns **1 and 30** so you stay on one half.

The ESP32 does **not** go on the breadboard. It sits on the table, and wires run from it.
Every ESP32 wire is a **male-to-female** jumper: female end on the ESP32 pin,
male end into the breadboard.

**Unplug USB while wiring.**

---

## Step 4: Wire the motor circuit (Friday about 18:45)

### 4a. Identify the parts
- **Transistor (S8050 / 2N2222A):** black half-cylinder with 3 legs. Hold it with the
  **flat face with writing toward you, legs pointing down**. Then, from left to right:
  **E, B, C** (Emitter, Base, Collector).
  *(P2N2222A only: the order is C, B, E, so swap the two outer legs in the steps below.)*
- **Resistor:** either direction is fine.
- **Diode:** small black cylinder with a **silver/grey stripe** at one end.
  **Direction matters a lot.** The stripe end is called the cathode.

### 4b. Push into the breadboard, one at a time

| Step | Do this | Hole(s) |
|---|---|---|
| 1 | Transistor, flat face toward you: E leg / B leg / C leg | **a20 / a21 / a22** |
| 2 | M-M jumper from row 20 to the **− rail** | e20 → − rail |
| 3 | Resistor: one leg in row 21, other leg in row 26 | b21 and b26 |
| 4 | Motor wire 1 (either one) into row 22 | c22 |
| 5 | Diode: **plain end (no stripe)** into row 22 | d22 |
| 6 | Diode: **striped end** into the **+ rail** | + rail |
| 7 | Motor wire 2 into the **+ rail** | + rail |

### 4c. Connect the ESP32 (M-F jumpers, female end on ESP32)

| ESP32 pin label | Goes to |
|---|---|
| **3V3** | + rail |
| **GND** (any one) | − rail |
| **D4** (may say G4 / IO4 / 4) | row 26 (e.g. c26) |

Use **3V3**, never **VIN** or **5V**.

### 4d. Safety check before plugging in USB
- [ ] Diode **stripe** is in the **+ rail**. If it's reversed, it shorts the power as soon as the motor turns on.
- [ ] Transistor flat face toward you, E in row 20, C in row 22.
- [ ] Nothing is plugged into VIN / 5V.
- [ ] No bare metal legs are touching each other above the breadboard.

### 4e. Upload Test 1
1. Open `Test1_Motor_Button/Test1_Motor_Button.ino`.
2. Click **Upload** (→ arrow). If it gets stuck on `Connecting.....`, **hold the BOOT
   button** on the ESP32 until you see `Writing at ...`, then let go.
3. **Tools → Serial Monitor**, set speed to **115200** (bottom-right).

**Done when:** every 3 s the motor buzzes a different pattern and the name prints,
the blue LED blinks, and pressing BOOT prints `BOOT button: PRESSED`.

---

## Step 5: Wire the microphone (Friday about 20:00)

Unplug USB. Leave the motor circuit as it is.

The INMP441 has 6 labelled pins. Read the labels printed on the mic board.
Use **M-F** jumpers: female end on the mic pin, male end into the breadboard.

| Mic pin | Goes to |
|---|---|
| **VDD** | + rail (3.3 V; never 5 V) |
| **GND** | − rail |
| **L/R** | − rail (must be connected, don't leave it loose) |
| **SCK** | row 10 (a10) |
| **WS** | row 12 (a12) |
| **SD** | row 14 (a14) |

ESP32 to the same rows (M-F, female end on ESP32):

| ESP32 pin | Goes to |
|---|---|
| **D26** | row 10 (b10) |
| **D25** | row 12 (b12) |
| **D33** | row 14 (b14) |

Upload `Test2_Microphone/Test2_Microphone.ino`, then open **Tools → Serial Plotter** at 115200.

**Done when:** one line stays around −85 to −70 when the room is quiet and jumps up
when you clap. The other line stays flat at −120, which is correct.
Also wiggle the mic wires gently. If the line drops to −120, a contact is loose.

---

## Step 6: Final program and training (Saturday about 10:00)

1. Open `HaptiHear/HaptiHear.ino` (`hh_dsp.h` must be in the same folder). Upload.
2. Serial Monitor at 115200. Press **EN** on the ESP32 to restart.
   You should see `Microphone ......... OK` and feel 3 pulses.
3. Type **t**, press Enter. It asks for DOORBELL. Make the sound **5–6 times**, about
   one second apart. Each time it prints `recorded DOORBELL example N`.
   (Doorbell: play a doorbell sound on your phone.)
4. Type **n** for the next sound (VOICE: say "hello" or a name), then ALARM
   (phone alarm sound), then KNOCK (knock on the table).
5. Type **s** to save. The training is kept even after unplugging.
6. Now make each sound again. You should see `>>> DOORBELL ...` and feel its pattern.

Tips: train in the room where you'll demo, with the phone at the same distance.
If two sounds get confused, retrain both with more examples (up to 8).

**Done when:** each of the 4 sounds is recognised correctly most of the time.

---

## Step 7: Make it wearable without solder (Saturday about 12:00)

- Put the **breadboard + ESP32 together on a piece of cardboard** and hold them with
  rubber bands or tape. Then run tape along every row of jumpers so none can pull out.
- Tape the **motor against the wrist** (or inside a watch strap / sweatband / hair band).
  It should touch skin.
- Point the **mic outward** (the tiny hole on the mic board must not be covered).
  Keep it a few cm away from the motor if possible.
- Power: phone **power bank** + the same USB cable, or stay on the laptop.

**Done when:** you can shake your arm gently and Test 1's motor patterns and the
mic level (type **l**) still work.

---

## Troubleshooting

| Symptom | Most likely cause → fix |
|---|---|
| No COM port | Charge-only USB cable, or the CP210x driver is missing |
| `Connecting.....` forever | Hold BOOT during upload |
| Motor never vibrates | Transistor reversed (flat face toward you, E-B-C) · D4 wire not in row 26 · motor wire fell out of jumper · 3V3/GND not on rails |
| Motor always on | Transistor C and E swapped · D4 jumper on wrong pin |
| ESP32 resets / goes dark when motor turns on | **Diode backwards**: unplug immediately, flip it (stripe to + rail) |
| `Microphone ... FAIL` / both plotter lines at −120 | Mic pins not making contact · VDD not on + rail · SCK/WS swapped · L/R not on − rail |
| Plotter line is random junk / stuck high | SD wire loose, or WS and SCK swapped |
| Mic works then randomly `MIC FAULT` | Loose contact, almost always unsoldered mic pins |
| Everything shows `LOUD SOUND` | Not trained yet. Type t |
| Wrong sound recognised | Retrain those classes with 6–8 examples each, same distance as the demo |

---

## Plan C: mic pins with NO soldering at all (emergency only)

Use this only if truly nobody can solder before Saturday. It can work, but
contacts may flicker. You'll see this as `MIC FAULT` in the monitor.

1. Push the pin strip's **short ends** up through the mic's 6 holes from underneath.
2. Push the female end of an M-F jumper onto each **long end**.
3. Gently bend each pin slightly sideways so it presses against the metal ring
   of its hole. A tilted pin makes much better contact than a straight one.
4. Wrap tape tightly around the mic board and the jumper ends to hold the pressure.
5. Run Test 2 and wiggle each wire. Re-tilt any pin that makes the line drop.

Even in the worst case, the demo's "pull the SD wire → MIC FAULT → reconnect →
recovers" step still works. That's the self-diagnosis feature.
