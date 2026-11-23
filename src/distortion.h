#pragma once

#include <math.h>

// Bass distortion. Below ~150 Hz the signal passes clean; above it, it is
// driven into a cubic soft clipper (no division in the sampling loop) and
// low-passed at ~3 kHz to tame the fizz, then added back to the clean lows,
// so the low end stays solid. Input and output in ADC LSB around the rest
// level.
class Distortion {
 public:
  static constexpr float LEVEL = 700.0f;

  void init(float rate, float drive_db) {
    split = 1.0f - expf(-2.0f * (float)M_PI * 150.0f / rate);
    tone = 1.0f - expf(-2.0f * (float)M_PI * 3000.0f / rate);
    set_drive_db(drive_db);
  }

  void set_drive_db(float db) {
    drive_db = db < 0 ? 0 : db > 40 ? 40 : db;
    drive = powf(10.0f, drive_db / 20.0f) / 2048.0f;
  }

  float get_drive_db() const { return drive_db; }

  __attribute__((always_inline)) float process(float x) {
    low += split * (x - low);
    float h = (x - low) * drive;
    // Slope 1 at 0, flat at +-1.5, where it reaches +-1.
    h = h > 1.5f ? 1.5f : h < -1.5f ? -1.5f : h;
    float s = h - (4.0f / 27.0f) * h * h * h;
    post += tone * (s - post);
    return low + post * LEVEL;
  }

 private:
  float split = 0, tone = 0, drive = 0, drive_db = 0;
  float low = 0, post = 0;
};
