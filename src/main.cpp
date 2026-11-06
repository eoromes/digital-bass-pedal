// Samples the ADC at 16 kHz and streams it to the PC (tools/listen.py).
// Core 1 only samples; core 0 sends. Calling Serial.write from the sampling
// loop delayed a couple of samples every block, which sounded metallic.
//
// Effects run per sample inside the sampling loop: block-level processing
// would delay the first samples of the next block. The sampling loop and the
// effects stay in IRAM: when code around analogRead runs from flash, it fights
// analogRead for the cache, the read gets 10-20 us slower and the sample rate
// collapses. Which builds do that depends on the code layout. Commands from the PC:
// 'g' toggles the noise gate, '[' and ']' lower and raise its threshold,
// 'c' toggles the chorus; m/M, d/D and r/R lower/raise its mix, depth and rate.
//
// Sample: 10xxxxxx 0xxxxxxx (12 bits). Once per second: STAT_HEADER, field
// count and 14-bit fields as two 0xxxxxxx bytes (see build_stats).
#include <Arduino.h>

#include "chorus.h"
#include "noise_gate.h"

#define INPUT_PIN 4
#define SAMPLE_RATE 16000
#define BAUD 921600
#define BLOCK 64
#define NBLK 32
#define STAT_HEADER 0xC1
#define STAT_FIELDS 16
#define GATE_THRESHOLD 32
#define CHORUS_MIX 0.6f
#define CHORUS_DEPTH_MS 4
#define CHORUS_RATE_HZ 0.8f
#define FX_GATE 1
#define FX_CHORUS 2

struct Stats {
  uint32_t max_late, late2, late10, late50, late10_start, dropped;
  uint32_t read_min, read_max, read_sum, slack_min, fx_cycles_max, count;
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

static NoiseGate gate;
static Chorus chorus;
static float dc = -1;
static volatile uint32_t fx = 0;

static void reset_stats() {
  st = {};
  st.read_min = st.slack_min = UINT32_MAX;
}

static void build_stats() {
  uint32_t read_mean10 = st.count ? (st.read_sum * 10 + st.count / 2) / st.count : 0;
  const uint32_t f[STAT_FIELDS] = {st.max_late,     st.late2,   st.late10,   st.late50,
                                   st.late10_start, st.dropped, st.read_min, read_mean10,
                                   st.read_max,     st.slack_min, fx,
                                   (uint32_t)gate.get_threshold(), st.fx_cycles_max,
                                   (uint32_t)(chorus.mix * 100 + 0.5f),
                                   (uint32_t)(chorus.get_depth_ms() * 10 + 0.5f),
                                   (uint32_t)(chorus.get_rate_hz() * 100 + 0.5f)};
  stat_pkt[0] = STAT_HEADER;
  stat_pkt[1] = STAT_FIELDS;
  for (int i = 0; i < STAT_FIELDS; i++) {
    uint32_t v = f[i] > 16383 ? 16383 : f[i];
    stat_pkt[2 + 2 * i] = v >> 7;
    stat_pkt[3 + 2 * i] = v & 0x7F;
  }
  reset_stats();
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

static IRAM_ATTR void sample_block(uint8_t *out) {
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

    uint32_t c0 = esp_cpu_get_cycle_count();
    v = process(v);
    st.fx_cycles_max = max(st.fx_cycles_max, esp_cpu_get_cycle_count() - c0);
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
    while (Serial.available()) {
      int c = Serial.read();
      if (c == 'g') fx = fx ^ FX_GATE;
      if (c == 'c') fx = fx ^ FX_CHORUS;
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

void setup() {
  analogReadResolution(12);
  analogSetPinAttenuation(INPUT_PIN, ADC_11db);
  vTaskPrioritySet(nullptr, configMAX_PRIORITIES - 2);
  reset_stats();
  gate.init(SAMPLE_RATE, GATE_THRESHOLD);
  chorus.init(SAMPLE_RATE, CHORUS_MIX, CHORUS_DEPTH_MS, CHORUS_RATE_HZ);
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
