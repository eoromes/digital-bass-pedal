#pragma once

// Downward expander. Input and output in ADC LSB around the rest level.
// Below the threshold (RMS) the gain falls as (level / threshold)^4, so a
// decaying note fades smoothly instead of switching off.
//
// Runs in the sampling loop: no library calls (sqrtf etc. live in flash), and
// the target gain is updated every CONTROL samples.
class NoiseGate {
 public:
  static constexpr unsigned CONTROL = 16;

  void init(float rate, float threshold_lsb) {
    rms_coef = 1.0f / (0.040f * rate);
    gain_up = 1.0f / (0.002f * rate);
    gain_down = 1.0f / (0.100f * rate);
    set_threshold(threshold_lsb);
  }

  void set_threshold(float lsb) {
    threshold = lsb;
    inv_t2 = 1.0f / (lsb * lsb);
  }

  float get_threshold() const { return threshold; }

  __attribute__((always_inline)) float process(float x) {
    ms += rms_coef * (x * x - ms);
    if (++count == CONTROL) {
      count = 0;
      float r = ms * inv_t2;
      target = r >= 1.0f ? 1.0f : r * r;
    }
    gain += (target > gain ? gain_up : gain_down) * (target - gain);
    return x * gain;
  }

 private:
  float rms_coef = 0, gain_up = 0, gain_down = 0, threshold = 0, inv_t2 = 0;
  float ms = 0, gain = 0, target = 0;
  unsigned count = 0;
};
