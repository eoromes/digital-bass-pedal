// Lê o ADC no INPUT_PIN e manda pela serial, em um de dois modos:
//
// MODE_PLOT  : texto ">raw:..." e ">mV:..." a cada 10 ms, para ver no Teleplot.
// MODE_AUDIO : amostras binárias a SAMPLE_RATE Hz, para ouvir no PC com
//              tools/listen.py. Nesse modo o monitor serial mostra lixo.
#include <Arduino.h>

#define MODE_PLOT 0
#define MODE_AUDIO 1
#define MODE MODE_AUDIO

#define INPUT_PIN 4

#if CONFIG_IDF_TARGET_ESP32S3 && (INPUT_PIN >= 26 && INPUT_PIN <= 32)
#error "No ESP32-S3 os GPIO 26 a 32 sao da memoria flash. Use outro pino (ex.: 4, 15, 16)."
#endif

#if MODE == MODE_PLOT

#define PRINT_INTERVAL_MS 10

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);                      // 0 a 4095
  analogSetPinAttenuation(INPUT_PIN, ADC_11db);  // faixa de ~0 a ~3,1 V
  delay(200);
  Serial.printf("Lendo ADC no GPIO%d\n", INPUT_PIN);
}

void loop() {
  Serial.printf(">raw:%d\n", analogRead(INPUT_PIN));
  Serial.printf(">mV:%d\n", analogReadMilliVolts(INPUT_PIN));
  delay(PRINT_INTERVAL_MS);
}

#else  // MODE_AUDIO

// 16 kHz x 2 bytes = 32 kB/s, folgado para 921600 baud (~92 kB/s).
// Para baixo é suficiente: a fundamental mais aguda fica abaixo de 1 kHz.
#define SAMPLE_RATE 16000
#define AUDIO_BAUD 921600
#define BLOCK 64

// Cada amostra de 12 bits vira 2 bytes: 1xxxxxxx (6 bits altos) e 0xxxxxxx
// (6 bits baixos). O bit 7 marca o primeiro byte, então o PC se ressincroniza
// sozinho se algum byte se perder.
static uint8_t buf[BLOCK * 2];
static uint64_t start_us;
static uint64_t n = 0;

void setup() {
  Serial.setTxBufferSize(4096);
  Serial.begin(AUDIO_BAUD);
  analogReadResolution(12);
  analogSetPinAttenuation(INPUT_PIN, ADC_11db);
  start_us = esp_timer_get_time();
}

void loop() {
  for (int i = 0; i < BLOCK; i++) {
    // espera o instante da próxima amostra (relógio absoluto, sem acumular erro)
    uint64_t target = start_us + (n * 1000000ULL) / SAMPLE_RATE;
    while ((int64_t)(esp_timer_get_time() - target) < 0) {
    }
    n++;

    uint16_t v = analogRead(INPUT_PIN);
    buf[2 * i] = 0x80 | (v >> 6);
    buf[2 * i + 1] = v & 0x3F;
  }
  Serial.write(buf, sizeof(buf));
}

#endif
