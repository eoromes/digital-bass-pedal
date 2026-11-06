// Test firmware: do the periodic noise bursts come from serial traffic?
//
// Repeats: record CAPTURE_S seconds to RAM with the serial port idle, send it;
// record again while core 0 sends dummy traffic at the normal rate (32 kB/s),
// send it. Each recording starts with MARK_LEN samples at 0 then MARK_LEN at
// 4095 (idle) or the opposite (with traffic). Dummy blocks start with
// DUMMY_HEADER, which tools/listen.py ignores.
//
// Upload: pio run -e capture -t upload
#include <Arduino.h>

#define INPUT_PIN 4
#define SAMPLE_RATE 16000
#define BAUD 921600
#define CAPTURE_S 3
#define NSAMP (SAMPLE_RATE * CAPTURE_S)
#define MARK_LEN 32
#define DUMMY_HEADER 0xC3
#define DUMMY_BLOCK 128

static uint16_t *samples;
static volatile bool dummy_on = false;
static volatile bool dummy_busy = false;
static volatile bool tx_ready = false;

static void tx_task(void *) {
  Serial.setTxBufferSize(4096);
  Serial.begin(BAUD);
  tx_ready = true;
  uint8_t block[DUMMY_BLOCK];
  block[0] = DUMMY_HEADER;
  memset(block + 1, 0x55, sizeof(block) - 1);
  for (;;) {
    if (dummy_on) {
      dummy_busy = true;
      Serial.write(block, sizeof(block));
      vTaskDelay(pdMS_TO_TICKS(4));
    } else {
      dummy_busy = false;
      vTaskDelay(1);
    }
  }
}

static void capture() {
  int64_t start = esp_timer_get_time();
  for (uint32_t i = 0; i < NSAMP; i++) {
    int64_t target = start + ((uint64_t)i * 1000000ULL) / SAMPLE_RATE;
    while (esp_timer_get_time() < target) {
    }
    samples[i] = analogRead(INPUT_PIN);
  }
}

static void send_sample(uint16_t v) {
  uint8_t b[2] = {(uint8_t)(0x80 | (v >> 6)), (uint8_t)(v & 0x3F)};
  Serial.write(b, 2);
}

static void send_capture(bool with_traffic) {
  for (int i = 0; i < MARK_LEN; i++) send_sample(with_traffic ? 4095 : 0);
  for (int i = 0; i < MARK_LEN; i++) send_sample(with_traffic ? 0 : 4095);
  uint8_t chunk[128];
  size_t k = 0;
  for (uint32_t i = 0; i < NSAMP; i++) {
    chunk[k++] = 0x80 | (samples[i] >> 6);
    chunk[k++] = samples[i] & 0x3F;
    if (k == sizeof(chunk)) {
      Serial.write(chunk, k);
      k = 0;
    }
  }
  if (k) Serial.write(chunk, k);
  // Wait until it is out, so the next idle recording is really idle.
  Serial.flush();
}

void setup() {
  analogReadResolution(12);
  analogSetPinAttenuation(INPUT_PIN, ADC_11db);
  samples = (uint16_t *)malloc(NSAMP * sizeof(uint16_t));
  xTaskCreatePinnedToCore(tx_task, "tx", 4096, nullptr, 5, nullptr, 0);
  while (!tx_ready) delay(1);
}

void loop() {
  if (!samples) {
    Serial.println("ram_capture: not enough memory for the buffer");
    delay(1000);
    return;
  }
  for (;;) {
    capture();
    send_capture(false);

    dummy_on = true;
    while (!dummy_busy) delay(1);
    capture();
    dummy_on = false;
    while (dummy_busy) delay(1);
    Serial.flush();
    send_capture(true);
  }
}
