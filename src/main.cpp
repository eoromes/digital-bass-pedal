// Samples the bass at 16 kHz and streams it to the PC (tools/listen.py).
// ADC1 runs in continuous mode with DMA on GPIO33 at 16 x 16 kHz, and each
// output sample is the mean of 16 conversions. Of the ADC1 pins, GPIO33 had
// the least hiss, and the mean of 16 brought it below analogRead (E18).
// Core 1 reads the DMA frames and runs the effects; core 0 sends.
//
// Effects run per sample. Commands from the PC: 'g' toggles the noise gate,
// '[' and ']' lower and raise its threshold, 'c' toggles the chorus; m/M, d/D,
// r/R and f/F lower/raise its mix, depth, rate and low filter (the frequency
// below which the bass stays dry).
//
// Sample: 10xxxxxx 0xxxxxxx (12 bits). Once per second: STAT_HEADER, field
// count and 14-bit fields as two 0xxxxxxx bytes (see build_stats). Fields 0-4
// and 6-9 were analogRead timing and are now always 0.
#include <Arduino.h>
#include <esp_adc/adc_continuous.h>

#include "chorus.h"
#include "noise_gate.h"

#define INPUT_PIN 33
#define SAMPLE_RATE 16000
#define OS 16
#define BAUD 921600
#define BLOCK 64
#define NBLK 32
#define FRAME_SAMPLES 32
#define FRAME_BYTES (FRAME_SAMPLES * OS * SOC_ADC_DIGI_RESULT_BYTES)
#define STAT_HEADER 0xC1
#define STAT_FIELDS 20
#define GATE_THRESHOLD 32
#define CHORUS_MIX 0.5f
#define CHORUS_DEPTH_MS 2
#define CHORUS_RATE_HZ 0.5f
#define CHORUS_SPLIT_HZ 100
#define FX_GATE 1
#define FX_CHORUS 2

struct Stats {
  uint32_t dropped, fx_cycles_max, produced, foreign;
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
static float dc = -1;
static volatile uint32_t fx = 0;

static bool IRAM_ATTR on_pool_ovf(adc_continuous_handle_t, const adc_continuous_evt_data_t *, void *) {
  pool_ovf = pool_ovf + 1;
  return false;
}

static void build_stats() {
  const uint32_t f[STAT_FIELDS] = {0, 0, 0, 0, 0, st.dropped, 0, 0, 0, 0, fx,
                                   (uint32_t)gate.get_threshold(), st.fx_cycles_max,
                                   (uint32_t)(chorus.get_mix() * 100 + 0.5f),
                                   (uint32_t)(chorus.get_depth_ms() * 10 + 0.5f),
                                   (uint32_t)(chorus.get_rate_hz() * 100 + 0.5f),
                                   pool_ovf, st.produced, st.foreign,
                                   (uint32_t)(chorus.get_split_hz() + 0.5f)};
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
  // Both always run, so the cost doesn't depend on which ones are on.
  float x = v - dc;
  float y = gate.process(x);
  if (!(fx & FX_GATE)) y = x;
  float z = chorus.process(y);
  if (!(fx & FX_CHORUS)) z = y;
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
      float t = gate.get_threshold();
      if (c == '[' && t > 2) gate.set_threshold(t - 2);
      if (c == ']' && t < 60) gate.set_threshold(t + 2);
      if (c == 'm') chorus.set_mix(chorus.get_mix() - 0.1f);
      if (c == 'M') chorus.set_mix(chorus.get_mix() + 0.1f);
      if (c == 'd') chorus.set_depth_ms(chorus.get_depth_ms() - 0.5f);
      if (c == 'D') chorus.set_depth_ms(chorus.get_depth_ms() + 0.5f);
      if (c == 'r') chorus.set_rate_hz(chorus.get_rate_hz() - 0.1f);
      if (c == 'R') chorus.set_rate_hz(chorus.get_rate_hz() + 0.1f);
      if (c == 'f') chorus.set_split_hz(chorus.get_split_hz() * 0.8f);
      if (c == 'F') chorus.set_split_hz(chorus.get_split_hz() * 1.25f);
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

void setup() {
  vTaskPrioritySet(nullptr, configMAX_PRIORITIES - 2);
  gate.init(SAMPLE_RATE, GATE_THRESHOLD);
  chorus.init(SAMPLE_RATE, CHORUS_MIX, CHORUS_DEPTH_MS, CHORUS_RATE_HZ, CHORUS_SPLIT_HZ);
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
