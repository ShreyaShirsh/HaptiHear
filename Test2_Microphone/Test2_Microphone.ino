// TEST 2 - INMP441 I2S microphone (SCK = G26, WS = G25, SD = G33)
// Open Tools > Serial Plotter (115200).  You will see two lines:
//   slotA and slotB  = sound level in dBFS for the two I2S channels.
// Expected: ONE line sits around -85..-70 when the room is quiet and jumps
// up (towards -40..-20) when you clap or talk. The other line stays flat
// at the bottom (-120). That is correct - the mic only uses one channel.
// If BOTH lines are flat at -120: check the mic wiring (VDD=3V3, GND, L/R=GND, SCK, WS, SD).
#include "driver/i2s_std.h"

i2s_chan_handle_t rx;
int32_t buf[512 * 2];

void setup() {
  Serial.begin(115200);
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num = 8;
  cc.dma_frame_num = 480;
  i2s_new_channel(&cc, NULL, &rx);
  i2s_std_config_t sc = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = GPIO_NUM_26,
      .ws   = GPIO_NUM_25,
      .dout = I2S_GPIO_UNUSED,
      .din  = GPIO_NUM_33,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };
  if (i2s_channel_init_std_mode(rx, &sc) != ESP_OK || i2s_channel_enable(rx) != ESP_OK) {
    Serial.println("I2S start FAILED");
    while (true) delay(1000);
  }
  gpio_pulldown_en(GPIO_NUM_33);   // unused channel / loose wire reads as zero
}

void loop() {
  size_t got = 0;
  while (got < sizeof(buf)) {
    size_t br = 0;
    if (i2s_channel_read(rx, (uint8_t*)buf + got, sizeof(buf) - got, &br, 1000) != ESP_OK) {
      Serial.println("I2S read timeout");
      return;
    }
    got += br;
  }
  double s[2] = {0, 0}, s2[2] = {0, 0};
  for (int i = 0; i < 512; i++)
    for (int c = 0; c < 2; c++) { double v = (double)(buf[2 * i + c] >> 8) / 8388608.0; s[c] += v; s2[c] += v * v; }
  float db[2];
  for (int c = 0; c < 2; c++) {
    double m = s[c] / 512, var = s2[c] / 512 - m * m;
    if (var < 1e-12) var = 1e-12;
    db[c] = 10.0f * log10f((float)var);
  }
  Serial.printf("slotA:%.1f,slotB:%.1f\n", db[0], db[1]);
}
