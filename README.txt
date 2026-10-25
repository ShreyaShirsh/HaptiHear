HaptiHear (v2 - for: ESP32 + INMP441 + vibration motor) - upload in this order
==============================================================================

Arduino IDE settings:
  Board : ESP32 Dev Module      ("esp32 by Espressif Systems", version 3.x)
  Port  : the COM port of your CP2102 board
  Serial Monitor / Serial Plotter speed: 115200
  No extra libraries needed.

  Test1_Motor_Button  -> motor plays 5 patterns, blue LED blinks, BOOT button prints
  Test2_Microphone    -> Serial Plotter: one line jumps when you clap
  HaptiHear           -> FINAL program (keep hh_dsp.h in the same folder!)

Keep the folder names exactly as they are (Arduino needs folder name = .ino name).
Checked with ESP32 core 3.3.12: 0 errors, 0 warnings.
  HaptiHear_IoT       -> CONNECTED version (Wi-Fi + phone app, touch sensors, alert check).
                         Keep hh_dsp.h in the same folder. Training from HaptiHear is kept.
