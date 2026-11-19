// Samples the bass at 16 kHz, runs the effects, plays the result on the
// PCM5102A DAC over I2S and streams it to the PC (tools/listen.py).
// ADC1 runs in continuous mode with DMA on GPIO33 at 16 x 16 kHz, and each
// output sample is the mean of 16 conversions. Of the ADC1 pins, GPIO33 had
// the least hiss, and the mean of 16 brought it below analogRead (E18).
// Core 1 reads the DMA frames, runs the effects and writes the DAC; core 0
// sends to the PC.
//
// The DAC uses I2S1: on the original ESP32, ADC continuous mode takes I2S0.
// Same sample on L and R. GPIO12 (BCK) is a strapping pin; the PCM5102A input
// doesn't pull it up, so the board still boots.
//
// Effects run per sample, in this order: noise gate, auto-wah, distortion,
// chorus. Commands from the PC: 'g' toggles the gate, '[' and ']' lower and
// raise its threshold; 'w' toggles the auto-wah, 's'/'S' lower/raise its
// sensitivity; 'o' toggles the distortion, 'k'/'K' lower/raise its drive;
// 'c' toggles the chorus, m/M, d/D and r/R lower/raise its mix, depth and rate.
//
// Sample: 10xxxxxx 0xxxxxxx (12 bits). Once per second: STAT_HEADER, field
// count and 14-bit fields as two 0xxxxxxx bytes (see build_stats). Fields 0-4
// and 6-9 were analogRead timing and are now always 0.
#include <Arduino.h>
#include <driver/i2s_std.h>
#include <esp_adc/adc_continuous.h>

#include "autowah.h"
#include "chorus.h"
#include "distortion.h"
#include "noise_gate.h"

#define INPUT_PIN 33
#define SAMPLE_RATE 16000
#define OS 16
#define BAUD 921600
#define BLOCK 64
#define NBLK 32
#define FRAME_SAMPLES 32
#define FRAME_BYTES (FRAME_SAMPLES * OS * SOC_ADC_DIGI_RESULT_BYTES)
#ifndef DAC_BCK
#define DAC_BCK 12
#endif
#ifndef DAC_WS
#define DAC_WS 13
#endif
#ifndef DAC_DOUT
#define DAC_DOUT 14
#endif
#define DAC_DMA_BUFS 4
#define STAT_HEADER 0xC1
#define STAT_FIELDS 22
#define GATE_THRESHOLD 32
#define CHORUS_MIX 0.6f
#define CHORUS_DEPTH_MS 4
#define CHORUS_RATE_HZ 0.8f
#define FX_GATE 1
#define FX_CHORUS 2
#define FX_WAH 4
#define FX_DIST 8
#define WAH_SENSITIVITY 0.7f
#define DIST_DRIVE_DB 20

struct Stats {
  uint32_t dropped, fx_cycles_max, produced, foreign, dac_short;
};

static Stats st;
static volatile uint32_t pool_ovf = 0;
static uint8_t stat_pkt[2 + 2 * STAT_FIELDS];
static volatile bool stat_ready = false;

static uint8_t ring[NBLK][BLOCK * 2];
static uint8_t scratch[BLOCK * 2];
static volatile uint32_t wr = 0, rd = 0;
static volatile bool tx_ready = false;

static adc_continuous_handle_t adc;
static adc_channel_t channel;

static NoiseGate gate;
static Chorus chorus;
static AutoWah wah;
static Distortion dist;
static float dc = -1;
static volatile uint32_t fx = 0;

static i2s_chan_handle_t dac;
static int32_t dac_buf[FRAME_SAMPLES * 2];

static bool IRAM_ATTR on_pool_ovf(adc_continuous_handle_t, const adc_continuous_evt_data_t *, void *) {
  pool_ovf = pool_ovf + 1;
  return false;
}

static void build_stats() {
  const uint32_t f[STAT_FIELDS] = {0, 0, 0, 0, 0, st.dropped, 0, 0, 0, 0, fx,
                                   (uint32_t)gate.get_threshold(), st.fx_cycles_max,
                                   (uint32_t)(chorus.mix * 100 + 0.5f),
                                   (uint32_t)(chorus.get_depth_ms() * 10 + 0.5f),
                                   (uint32_t)(chorus.get_rate_hz() * 100 + 0.5f),
                                   pool_ovf, st.produced, st.foreign, st.dac_short,
                                   (uint32_t)(dist.get_drive_db() + 0.5f),
                                   (uint32_t)(wah.get_sensitivity() * 100 + 0.5f)};
  stat_pkt[0] = STAT_HEADER;
  stat_pkt[1] = STAT_FIELDS;
  for (int i = 0; i < STAT_FIELDS; i++) {
    uint32_t v = f[i] > 16383 ? 16383 : f[i];
    stat_pkt[2 + 2 * i] = v >> 7;
    stat_pkt[3 + 2 * i] = v & 0x7F;
  }
  st = {};
  pool_ovf = 0;
}

static IRAM_ATTR uint16_t process(uint16_t v) {
  if (dc < 0) dc = v;
  dc += (v - dc) * 0.0005f;
  // All always run, so the cost doesn't depend on which ones are on.
  float x = v - dc;
  float y = gate.process(x);
  if (!(fx & FX_GATE)) y = x;
  float a = wah.process(y);
  if (!(fx & FX_WAH)) a = y;
  float b = dist.process(a);
  if (!(fx & FX_DIST)) b = a;
  float z = chorus.process(b);
  if (!(fx & FX_CHORUS)) z = b;
  if (!fx) return v;
  int out = (int)(dc + z + 0.5f);
  return out < 0 ? 0 : out > 4095 ? 4095 : out;
}

static void tx_task(void *) {
  // Called here so the UART interrupt is installed on core 0.
  Serial.setTxBufferSize(4096);
  Serial.begin(BAUD);
  tx_ready = true;
  for (;;) {
    while (Serial.available()) {
      int c = Serial.read();
      if (c == 'g') fx = fx ^ FX_GATE;
      if (c == 'c') fx = fx ^ FX_CHORUS;
      if (c == 'w') fx = fx ^ FX_WAH;
      if (c == 'o') fx = fx ^ FX_DIST;
      if (c == 's') wah.set_sensitivity(wah.get_sensitivity() * 0.8f);
      if (c == 'S') wah.set_sensitivity(wah.get_sensitivity() * 1.25f);
      if (c == 'k') dist.set_drive_db(dist.get_drive_db() - 3);
      if (c == 'K') dist.set_drive_db(dist.get_drive_db() + 3);
      float t = gate.get_threshold();
      if (c == '[' && t > 2) gate.set_threshold(t - 2);
      if (c == ']' && t < 60) gate.set_threshold(t + 2);
      float mix = chorus.mix;
      if (c == 'm') chorus.mix = mix > 0.05f ? mix - 0.1f : 0;
      if (c == 'M') chorus.mix = mix < 1.45f ? mix + 0.1f : 1.5f;
      if (c == 'd') chorus.set_depth_ms(chorus.get_depth_ms() - 0.5f);
      if (c == 'D') chorus.set_depth_ms(chorus.get_depth_ms() + 0.5f);
      if (c == 'r') chorus.set_rate_hz(chorus.get_rate_hz() - 0.1f);
      if (c == 'R') chorus.set_rate_hz(chorus.get_rate_hz() + 0.1f);
    }
    while (rd != wr) {
      __sync_synchronize();
      Serial.write(ring[rd % NBLK], BLOCK * 2);
      __sync_synchronize();
      rd = rd + 1;
    }
    if (stat_ready) {
      Serial.write(stat_pkt, sizeof(stat_pkt));
      stat_ready = false;
    }
    vTaskDelay(1);
  }
}

static void start_adc() {
  adc_unit_t unit;
  ESP_ERROR_CHECK(adc_continuous_io_to_channel(INPUT_PIN, &unit, &channel));
  adc_continuous_handle_cfg_t hcfg = {};
  hcfg.max_store_buf_size = FRAME_BYTES * 8;
  hcfg.conv_frame_size = FRAME_BYTES;
  ESP_ERROR_CHECK(adc_continuous_new_handle(&hcfg, &adc));

  adc_digi_pattern_config_t pat = {};
  pat.atten = ADC_ATTEN_DB_12;
  pat.channel = channel;
  pat.unit = ADC_UNIT_1;
  pat.bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
  adc_continuous_config_t cfg = {};
  cfg.pattern_num = 1;
  cfg.adc_pattern = &pat;
  cfg.sample_freq_hz = SAMPLE_RATE * OS;
  cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE1;
  ESP_ERROR_CHECK(adc_continuous_config(adc, &cfg));

  adc_continuous_evt_cbs_t cbs = {};
  cbs.on_pool_ovf = on_pool_ovf;
  ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc, &cbs, nullptr));
  ESP_ERROR_CHECK(adc_continuous_start(adc));
}

static void start_dac() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  cc.dma_desc_num = DAC_DMA_BUFS;
  cc.dma_frame_num = FRAME_SAMPLES;
  cc.auto_clear = true;
  ESP_ERROR_CHECK(i2s_new_channel(&cc, &dac, nullptr));

  // 32-bit slots: BCK = 64 x fs, a ratio the PCM5102A PLL accepts without SCK.
  i2s_std_config_t sc = {};
  sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  sc.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  sc.gpio_cfg.bclk = (gpio_num_t)DAC_BCK;
  sc.gpio_cfg.ws = (gpio_num_t)DAC_WS;
  sc.gpio_cfg.dout = (gpio_num_t)DAC_DOUT;
  sc.gpio_cfg.din = I2S_GPIO_UNUSED;
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(dac, &sc));
  ESP_ERROR_CHECK(i2s_channel_enable(dac));
}

static void write_dac(uint16_t v) {
  static uint32_t n = 0;
  if (!dac) return;
  // One ADC LSB = 2^20 in the 32-bit slot, so ADC full scale is DAC full scale.
  float s = (v - dc) * 1048576.0f;
  s = s > 2147483520.0f ? 2147483520.0f : s < -2147483520.0f ? -2147483520.0f : s;
  dac_buf[2 * n] = dac_buf[2 * n + 1] = (int32_t)s;
  if (++n < FRAME_SAMPLES) return;
  n = 0;
  size_t written = 0;
  i2s_channel_write(dac, dac_buf, sizeof(dac_buf), &written, 20);
  if (written != sizeof(dac_buf)) st.dac_short++;
}

void setup() {
  vTaskPrioritySet(nullptr, configMAX_PRIORITIES - 2);
#ifndef NO_DAC
  start_dac();
#endif
  gate.init(SAMPLE_RATE, GATE_THRESHOLD);
  chorus.init(SAMPLE_RATE, CHORUS_MIX, CHORUS_DEPTH_MS, CHORUS_RATE_HZ);
  wah.init(SAMPLE_RATE, WAH_SENSITIVITY);
  dist.init(SAMPLE_RATE, DIST_DRIVE_DB);
  xTaskCreatePinnedToCore(tx_task, "tx", 4096, nullptr, 5, nullptr, 0);
  while (!tx_ready) delay(1);
  start_adc();
}

void loop() {
  static uint8_t buf[FRAME_BYTES];
  static uint32_t sum = 0, in_group = 0, out_i = 0;
  static int64_t t_stat = esp_timer_get_time();

  // Never returns: between loop() calls the Arduino core polls the serial port.
  for (;;) {
    uint32_t got = 0;
    if (adc_continuous_read(adc, buf, FRAME_BYTES, &got, 1000) != ESP_OK) continue;
    for (uint32_t p = 0; p + SOC_ADC_DIGI_RESULT_BYTES <= got; p += SOC_ADC_DIGI_RESULT_BYTES) {
      adc_digi_output_data_t *d = (adc_digi_output_data_t *)&buf[p];
      if (d->type1.channel != channel) {
        st.foreign++;
        continue;
      }
      sum += d->type1.data;
      if (++in_group < OS) continue;
      uint16_t v = (sum + OS / 2) / OS;
      sum = in_group = 0;

      uint32_t c0 = esp_cpu_get_cycle_count();
      v = process(v);
      st.fx_cycles_max = max(st.fx_cycles_max, esp_cpu_get_cycle_count() - c0);
      write_dac(v);

      bool full = wr - rd >= NBLK;
      uint8_t *out = full ? scratch : ring[wr % NBLK];
      out[2 * out_i] = 0x80 | (v >> 6);
      out[2 * out_i + 1] = v & 0x3F;
      st.produced++;
      if (++out_i == BLOCK) {
        out_i = 0;
        if (full) {
          st.dropped++;
        } else {
          __sync_synchronize();
          wr = wr + 1;
        }
      }
    }
    int64_t now = esp_timer_get_time();
    if (now - t_stat >= 1000000 && !stat_ready) {
      t_stat += 1000000;
      build_stats();
      __sync_synchronize();
      stat_ready = true;
    }
  }
}
