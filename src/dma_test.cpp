// ADC1 continuous mode (DMA) test. The hardware samples one ADC1 pin at
// 16 x 16 kHz; each 16 kHz output sample is the mean of the first 1, 2, 4, 8
// or 16 conversions of its group, so every setting runs on the same hardware
// clock. The ESP32 can't run continuous mode below 20 kHz, so "1x" means one
// conversion out of 16 at 256 kHz, not 16 kHz.
//
// Pin: ADC_PIN build flag (GPIO32-39). Keys '0'..'6' pick the mean of 2^k, up to OS.
//
// Same sample format as main.cpp. Once per second: DMA_STAT_HEADER, field
// count and 14-bit fields (pin, mean count, output samples in the last second,
// dropped blocks, driver pool overflows, conversions from another channel).
#include <Arduino.h>
#include <esp_adc/adc_continuous.h>

#ifndef ADC_PIN
#define ADC_PIN 34
#endif
#define SAMPLE_RATE 16000
#ifndef OS
#define OS 16
#endif
#define BAUD 921600
#define BLOCK 64
#define NBLK 32
#define DMA_STAT_HEADER 0xC2
#define STAT_FIELDS 6
#define FRAME_BYTES (BLOCK * OS * SOC_ADC_DIGI_RESULT_BYTES)

static uint8_t ring[NBLK][BLOCK * 2];
static volatile uint32_t wr = 0, rd = 0;
static volatile bool tx_ready = false;
static uint8_t stat_pkt[2 + 2 * STAT_FIELDS];
static volatile bool stat_ready = false;

static adc_continuous_handle_t adc;
static adc_channel_t channel;
static volatile uint32_t mean_n = 1;
static volatile uint32_t pool_ovf = 0;
static uint32_t produced = 0, dropped = 0, foreign = 0;

static bool IRAM_ATTR on_pool_ovf(adc_continuous_handle_t, const adc_continuous_evt_data_t *, void *) {
  pool_ovf = pool_ovf + 1;
  return false;
}

static void build_stats() {
  const uint32_t f[STAT_FIELDS] = {ADC_PIN, mean_n, produced, dropped, pool_ovf, foreign};
  stat_pkt[0] = DMA_STAT_HEADER;
  stat_pkt[1] = STAT_FIELDS;
  for (int i = 0; i < STAT_FIELDS; i++) {
    uint32_t v = f[i] > 16383 ? 16383 : f[i];
    stat_pkt[2 + 2 * i] = v >> 7;
    stat_pkt[3 + 2 * i] = v & 0x7F;
  }
  produced = dropped = foreign = 0;
  pool_ovf = 0;
}

static void tx_task(void *) {
  Serial.setTxBufferSize(4096);
  Serial.begin(BAUD);
  tx_ready = true;
  for (;;) {
    while (Serial.available()) {
      int c = Serial.read();
      if (c >= '0' && c <= '6' && (1u << (c - '0')) <= OS) mean_n = 1u << (c - '0');
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
  ESP_ERROR_CHECK(adc_continuous_io_to_channel(ADC_PIN, &unit, &channel));
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
  xTaskCreatePinnedToCore(tx_task, "tx", 4096, nullptr, 5, nullptr, 0);
  while (!tx_ready) delay(1);
  start_adc();
}

void loop() {
  static uint8_t buf[FRAME_BYTES];
  static uint8_t scratch[BLOCK * 2];
  static uint32_t group[OS];
  static uint32_t in_group = 0, out_i = 0;
  static int64_t t_stat = esp_timer_get_time();

  for (;;) {
    uint32_t got = 0;
    if (adc_continuous_read(adc, buf, FRAME_BYTES, &got, 1000) != ESP_OK) continue;
    for (uint32_t p = 0; p + SOC_ADC_DIGI_RESULT_BYTES <= got; p += SOC_ADC_DIGI_RESULT_BYTES) {
      adc_digi_output_data_t *d = (adc_digi_output_data_t *)&buf[p];
      if (d->type1.channel != channel) {
        foreign++;
        continue;
      }
      group[in_group++] = d->type1.data;
      if (in_group < OS) continue;
      in_group = 0;

      uint32_t n = mean_n, sum = 0;
      for (uint32_t k = 0; k < n; k++) sum += group[k];
      uint16_t v = (sum + n / 2) / n;

      bool full = wr - rd >= NBLK;
      uint8_t *out = full ? scratch : ring[wr % NBLK];
      out[2 * out_i] = 0x80 | (v >> 6);
      out[2 * out_i + 1] = v & 0x3F;
      produced++;
      if (++out_i == BLOCK) {
        out_i = 0;
        if (full) {
          dropped++;
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
