// Samples the ADC at 16 kHz and streams it to the PC (tools/listen.py).
// Core 1 only samples; core 0 sends. Calling Serial.write from the sampling
// loop delayed a couple of samples every block, which sounded metallic.
//
// Sample: 10xxxxxx 0xxxxxxx (12 bits). Once per second: STAT_HEADER, field
// count and 14-bit fields as two 0xxxxxxx bytes (see build_stats).
#include <Arduino.h>

#define INPUT_PIN 4
#define SAMPLE_RATE 16000
#define BAUD 921600
#define BLOCK 64
#define NBLK 32
#define STAT_HEADER 0xC1
#define STAT_FIELDS 10

struct Stats {
  uint32_t max_late, late2, late10, late50, late10_start, dropped;
  uint32_t read_min, read_max, read_sum, slack_min, count;
};

static Stats st;
static uint8_t stat_pkt[2 + 2 * STAT_FIELDS];
static volatile bool stat_ready = false;

static uint8_t ring[NBLK][BLOCK * 2];
static uint8_t scratch[BLOCK * 2];
static volatile uint32_t wr = 0, rd = 0;
static volatile bool tx_ready = false;

static uint64_t start_us;
static uint64_t n = 0;

static void reset_stats() {
  st = {};
  st.read_min = st.slack_min = UINT32_MAX;
}

static void build_stats() {
  uint32_t read_mean10 = st.count ? (st.read_sum * 10 + st.count / 2) / st.count : 0;
  const uint32_t f[STAT_FIELDS] = {st.max_late,     st.late2,   st.late10,   st.late50,
                                   st.late10_start, st.dropped, st.read_min, read_mean10,
                                   st.read_max,     st.slack_min};
  stat_pkt[0] = STAT_HEADER;
  stat_pkt[1] = STAT_FIELDS;
  for (int i = 0; i < STAT_FIELDS; i++) {
    uint32_t v = f[i] > 16383 ? 16383 : f[i];
    stat_pkt[2 + 2 * i] = v >> 7;
    stat_pkt[3 + 2 * i] = v & 0x7F;
  }
  reset_stats();
}

static void sample_block(uint8_t *out) {
  for (int i = 0; i < BLOCK; i++) {
    int64_t target = start_us + (n++ * 1000000ULL) / SAMPLE_RATE;
    int64_t wait_start = esp_timer_get_time();
    int64_t now;
    do {
      now = esp_timer_get_time();
    } while (now < target);

    uint16_t v = analogRead(INPUT_PIN);
    uint32_t read_us = esp_timer_get_time() - now;
    uint32_t late = now - target;
    uint32_t slack = now - wait_start;

    st.max_late = max(st.max_late, late);
    st.late2 += late > 2;
    st.late10 += late > 10;
    st.late50 += late > 50;
    st.late10_start += late > 10 && i < 4;
    st.read_min = min(st.read_min, read_us);
    st.read_max = max(st.read_max, read_us);
    st.read_sum += read_us;
    st.slack_min = min(st.slack_min, slack);
    st.count++;

    out[2 * i] = 0x80 | (v >> 6);
    out[2 * i + 1] = v & 0x3F;
  }
}

static void tx_task(void *) {
  // Called here so the UART interrupt is installed on core 0.
  Serial.setTxBufferSize(4096);
  Serial.begin(BAUD);
  tx_ready = true;
  for (;;) {
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

void setup() {
  analogReadResolution(12);
  analogSetPinAttenuation(INPUT_PIN, ADC_11db);
  vTaskPrioritySet(nullptr, configMAX_PRIORITIES - 2);
  reset_stats();
  xTaskCreatePinnedToCore(tx_task, "tx", 4096, nullptr, 5, nullptr, 0);
  while (!tx_ready) delay(1);
  start_us = esp_timer_get_time();
}

void loop() {
  // Never returns: between loop() calls the Arduino core polls the serial port.
  for (;;) {
    bool full = wr - rd >= NBLK;
    sample_block(full ? scratch : ring[wr % NBLK]);
    if (full) {
      st.dropped++;
    } else {
      __sync_synchronize();
      wr = wr + 1;
    }
    if (st.count >= SAMPLE_RATE && !stat_ready) {
      build_stats();
      __sync_synchronize();
      stat_ready = true;
    }
  }
}
