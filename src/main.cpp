// Lê a tensão de um pino pelo ADC e manda pela serial.
// Formato ">nome:valor" é o que o Teleplot (VS Code) entende para plotar;
// no monitor serial comum aparece como texto.
//
// raw: valor bruto do ADC, 0 a 4095 (12 bits)
// mV:  mesma leitura convertida para milivolts, usando a calibração do chip
#include <Arduino.h>

#define INPUT_PIN 4
#define PRINT_INTERVAL_MS 10

#if CONFIG_IDF_TARGET_ESP32S3 && (INPUT_PIN >= 26 && INPUT_PIN <= 32)
#error "No ESP32-S3 os GPIO 26 a 32 sao da memoria flash. Use outro pino (ex.: 4, 15, 16)."
#endif

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);                      // 0 a 4095
  analogSetPinAttenuation(INPUT_PIN, ADC_11db);  // faixa de ~0 a ~3,1 V
  delay(200);
  Serial.printf("Lendo ADC no GPIO%d\n", INPUT_PIN);
}

void loop() {
  int raw = analogRead(INPUT_PIN);
  int mv = analogReadMilliVolts(INPUT_PIN);

  Serial.printf(">raw:%d\n", raw);
  Serial.printf(">mV:%d\n", mv);

  delay(PRINT_INTERVAL_MS);
}
