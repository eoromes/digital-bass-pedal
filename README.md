# digital-bass-pedal

Digital bass effects pedal built around an original ESP32.

The bass signal goes through an analog preamp into an ADC, the effects run on the ESP32 itself, and the audio goes out to the amp through an I2S DAC. Everything is on a breadboard for now, powered from the board's USB port.

```
bass -> preamp (MCP6002) -> ADC -> ESP32 effects -> PCM5102A DAC -> volume -> amp
```

Write-up of the project so far: [docs/article.md](docs/article.md) (draft).

## Hardware

- ESP32 DevKit (original ESP32, dual-core 240 MHz, with a CP2102 USB-serial bridge)
- MCP6002 as a high-impedance buffer, adjustable gain (1x to 11x) and ~11 kHz anti-aliasing filter
- Input: the ESP32's built-in ADC
- Output: PCM5102A I2S DAC (not connected yet)
- Planned: one button and one LED per effect

### Pins

| Function | GPIO |
| --- | --- |
| Audio input (built-in ADC1, continuous mode) | 33 |

The pins for the PCM5102A, the buttons and the LEDs are not decided yet. GPIO12 is a boot strapping pin on the original ESP32.

The PCM5102A needs SCK tied to GND and the XSMT jumper set to H, otherwise it stays muted.

## Effects

- Noise gate (RMS expander), working
- Bass chorus, working: the lows below an adjustable crossover (100 Hz by default) stay dry, and only the band above goes through the modulated delay
- Planned: overdrive, compressor, delay

The effects run sample by sample inside the sampling loop, in IRAM. Gate and chorus together cost about 700 CPU cycles per sample.

## Building

Uses [PlatformIO](https://platformio.org/):

```sh
pio run -t upload               # main firmware (src/main.cpp)
pio run -e capture -t upload    # RAM capture test firmware (src/ram_capture.cpp)
PLATFORMIO_BUILD_FLAGS=-DADC_PIN=33 pio run -e dma -t upload   # ADC1 DMA test firmware (src/dma_test.cpp)
```

## Listening and analysis tools

The current firmware samples GPIO33 with DMA at 256 kHz, averages each 16 conversions into one 16 kHz sample, and streams the raw samples to the PC over USB. The scripts in `tools/` play them back and analyze recordings:

```sh
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt
.venv/bin/python tools/listen.py --save take.wav   # play and record (keys: g gate, [ ] threshold, c chorus, m/M d/D r/R f/F its mix, depth, rate, low filter)
.venv/bin/python tools/analyze.py take.wav         # noise analysis
.venv/bin/python tools/view.py take.wav            # waveform, clipping, attacks
```

`docs/make_figures.py` regenerates the article figures from the raw recordings (not in the repository yet; pass their folder with `--data`).

## Status

The analog input is built and characterized. With its input tied to a clean DC level, the built-in ADC alone reaches only about 55-60 dB of dynamic range, and each `analogRead` takes about 41 µs, which caps software sampling at about 20 kHz. Continuous mode with DMA on GPIO33, averaging 16 conversions per sample, lowers the ADC hiss by about 4.5 dB and frees the CPU. Next step for this version: the PCM5102A output.

A future version may replace the built-in ADC with an external audio ADC (such as the PCM1808) for a much better signal-to-noise ratio.
