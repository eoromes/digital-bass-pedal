// Teste inicial: pisca o LED onboard (GPIO2 na maioria das DevKits)
// e imprime informações do chip na serial.
#include <Arduino.h>

#define LED_PIN 2

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  delay(500);
  Serial.println();
  Serial.println("=== Digital Guitar Pedal - teste ESP32 ===");
  Serial.printf("Chip: %s rev %d, %d cores @ %d MHz\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), ESP.getCpuFreqMHz());
  Serial.printf("Flash: %u bytes | PSRAM: %u bytes | Heap livre: %u bytes\n",
                ESP.getFlashChipSize(), ESP.getPsramSize(), ESP.getFreeHeap());
}

void loop() {
  static uint32_t n = 0;
  digitalWrite(LED_PIN, n % 2);
  Serial.printf("tick %lu\n", (unsigned long)n++);
  delay(500);
}
