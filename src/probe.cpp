// Uses the ESP32 as a DC voltmeter: prints, twice per second, the calibrated
// voltage on a probe pin (a loose wire touched to the point to measure) and
// on the audio input pin, each averaged over 64 reads. Good for low-impedance
// points such as op-amp outputs and supplies; a high-impedance point (VBIAS,
// the op-amp input) reads low because the ADC loads it.
#include <Arduino.h>

#ifndef PROBE_PIN
#define PROBE_PIN 32
#endif
#ifndef INPUT_PIN
#define INPUT_PIN 33
#endif

static uint32_t read_mv(int pin) {
  uint32_t sum = 0;
  for (int i = 0; i < 64; i++) sum += analogReadMilliVolts(pin);
  return sum / 64;
}

void setup() {
  Serial.begin(115200);
  analogSetAttenuation(ADC_11db);
}

void loop() {
  Serial.printf("probe GPIO%d: %4lu mV   input GPIO%d: %4lu mV\n", PROBE_PIN,
                (unsigned long)read_mv(PROBE_PIN), INPUT_PIN, (unsigned long)read_mv(INPUT_PIN));
  delay(500);
}
