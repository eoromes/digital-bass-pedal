# digital-guitar-pedal

Digital bass effects pedal built around an ESP32-S3.

The idea is simple: the bass signal goes through an analog preamp into the ESP's ADC, the effects run on the chip itself, and the audio goes out to the amp through an external I2S DAC. For now everything is on a breadboard, powered from the board's USB port.

```
bass -> preamp (MCP6002) -> ESP32-S3 ADC -> DSP -> PCM5102A -> volume -> amp
```

## Hardware

- ESP32-S3-DevKitC-1 (or an N16R8 clone)
- MCP6002 as a high-impedance buffer, adjustable gain (1x to 11x) and ~11 kHz anti-aliasing filter
- PCM5102A I2S DAC on the output (the S3 has no internal DAC)
- 4 buttons + 4 LEDs to switch the effects on and off
- Optional: rotary encoder, 0.96" OLED and a PCM1808 ADC to compare against the internal one

### Pins

| Function | GPIO |
| --- | --- |
| Audio input (ADC1) | 4 |
| I2S BCK / WS / DOUT | 12 / 13 / 14 |
| Buttons | 15, 16, 17, 18 |
| LEDs | 39, 40, 41, 42 |
| Encoder CLK / DT / SW | 5, 6, 7 |
| OLED SDA / SCL | 8 / 9 |

The PCM5102A needs SCK tied to GND and the XSMT jumper set to H, otherwise it stays muted.

## Planned effects

- Overdrive / distortion
- Chorus
- Delay
- Compressor

Audio is meant to run at 50 kHz, in 64-sample blocks.

## Building

Uses [PlatformIO](https://platformio.org/):

```sh
pio run -t upload
```

## Listening and analysis tools

The current firmware streams the raw ADC samples to the PC over USB. The scripts in `tools/` play them back and analyze recordings:

```sh
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt
.venv/bin/python tools/listen.py --save take.wav   # play and record
.venv/bin/python tools/analyze.py take.wav         # noise analysis
.venv/bin/python tools/view.py take.wav            # waveform, clipping, attacks
```

## Status

Early stage. Development is currently on an original ESP32 (`esp32dev`); the target is the ESP32-S3. The analog input is built and the input path has been characterized: with its input tied to a clean DC level, the internal ADC alone reaches only about 55-60 dB of dynamic range. Next steps: an external I2S ADC (PCM1808) and the PCM5102A output.
