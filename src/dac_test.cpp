// PCM5102A test: plays a continuous 440 Hz tone at -12 dBFS on I2S1, nothing
// else. Rate and pins come from build flags, so the DAC can be checked at a
// common rate (48 kHz by default) apart from the pedal firmware.
#include <Arduino.h>
#include <driver/i2s_std.h>

#ifndef DAC_RATE
#define DAC_RATE 48000
#endif
#ifndef DAC_BCK
#define DAC_BCK 18
#endif
#ifndef DAC_WS
#define DAC_WS 19
#endif
#ifndef DAC_DOUT
#define DAC_DOUT 23
#endif
#define FRAMES 64

static i2s_chan_handle_t dac;

void setup() {
  Serial.begin(115200);
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  cc.dma_frame_num = FRAMES;
  ESP_ERROR_CHECK(i2s_new_channel(&cc, &dac, nullptr));
  i2s_std_config_t sc = {};
  sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(DAC_RATE);
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  sc.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  sc.gpio_cfg.bclk = (gpio_num_t)DAC_BCK;
  sc.gpio_cfg.ws = (gpio_num_t)DAC_WS;
  sc.gpio_cfg.dout = (gpio_num_t)DAC_DOUT;
  sc.gpio_cfg.din = I2S_GPIO_UNUSED;
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(dac, &sc));
  ESP_ERROR_CHECK(i2s_channel_enable(dac));
  Serial.printf("dac_test: %d Hz, BCK %d WS %d DOUT %d\n", DAC_RATE, DAC_BCK, DAC_WS, DAC_DOUT);
}

void loop() {
  static int32_t buf[FRAMES * 2];
  static double phase = 0;
  for (int i = 0; i < FRAMES; i++) {
    int32_t s = (int32_t)(0.25 * 2147483647.0 * sin(phase));
    buf[2 * i] = buf[2 * i + 1] = s;
    phase += 2 * PI * 440.0 / DAC_RATE;
    if (phase > 2 * PI) phase -= 2 * PI;
  }
  size_t written;
  i2s_channel_write(dac, buf, sizeof(buf), &written, portMAX_DELAY);
}
