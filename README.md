# digital-bass-pedal

Digital bass effects pedal built around an original ESP32.

https://github.com/user-attachments/assets/c89f3586-0b72-473c-83ae-9768305bff6b

*The pedal through a bass amp: clean, chorus and flanger on a held note, then distortion alone, with chorus and with flanger on a riff. Recorded with a phone in front of the amp.*

The bass signal goes through an analog preamp into the ESP32's ADC, the effects run on the ESP32 itself at 48 kHz, and the audio goes out to the amp through an I2S DAC. Everything is on a breadboard for now, powered from the board's USB port.

```
bass -> preamp (MCP6002) -> ADC -> ESP32 effects -> PCM5102A DAC -> volume -> amp
```

Write-up of the project so far: [docs/article.md](docs/article.md) (draft).

## Hardware

- ESP32 DevKit (original ESP32, dual-core 240 MHz, with a CP2102 USB-serial bridge)
- MCP6002 as a high-impedance buffer, adjustable gain (1x to 11x) and ~11 kHz anti-aliasing filter
- Input: the ESP32's built-in ADC
- Output: PCM5102A I2S DAC, then a level divider and a volume pot

### Pins

| Function | GPIO |
| --- | --- |
| Audio input (built-in ADC1, continuous mode) | 33 |
| PCM5102A BCK / LCK / DIN (I2S1) | 18 / 19 / 23 |

The PCM5102A needs SCK, FLT, DEMP and FMT tied to GND and XSMT to 3.3 V, otherwise it stays muted.

## Effects

In this order:

- Noise gate (RMS expander)
- Distortion: the lows below ~150 Hz stay clean, the rest is soft-clipped
- Flanger: 0.2 to 4 ms delay swept by an LFO, with feedback
- Two-voice chorus, at a third of the mix below 120 Hz so the low E keeps its fundamental
- Planned: compressor, delay

The effects run sample by sample in IRAM. With all of them on, the worst sample takes about 2,300 of the ~5,000 CPU cycles available at 48 kHz. A soft limiter before the DAC rounds off peaks above about three quarters of full scale. The effects are switched on and off, and adjusted, from the PC with `tools/listen.py`.

## Building

Uses [PlatformIO](https://platformio.org/):

```sh
pio run -t upload               # main firmware (src/main.cpp)
PLATFORMIO_BUILD_FLAGS=-DADC_PIN=33 pio run -e dma -t upload   # ADC1 DMA test firmware (src/dma_test.cpp)
pio run -e probe -t upload      # the ESP32 as a DC voltmeter on GPIO32/33 (src/probe.cpp)
```

## Listening and analysis tools

The firmware samples GPIO33 with DMA at 384 kHz and averages each 8 conversions into one 48 kHz sample. Besides playing on the DAC, it streams the signal to the PC over USB at 16 kHz (low-passed at 7 kHz, one sample in three), with a stats packet once per second. The scripts in `tools/` show, record and analyze it:

```sh
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt
.venv/bin/python tools/listen.py --save take.wav   # show levels and effects, record; --play also plays on the PC (keys listed at start)
.venv/bin/python tools/analyze.py take.wav         # noise analysis
.venv/bin/python tools/view.py take.wav            # waveform, clipping, attacks
```

`docs/make_figures.py` regenerates the article figures from the raw recordings (not in the repository yet; pass their folder with `--data`).

## Status

The input and the output both work. With its input tied to a clean DC level, the built-in ADC alone reaches only about 55-60 dB of dynamic range, and each `analogRead` took about 41 µs, which capped software sampling at about 20 kHz. Continuous mode with DMA on GPIO33, averaging conversions per sample, lowers the ADC hiss and frees the CPU. Running at 48 kHz moves the DAC's out-of-band noise above the audible range. Next steps: the noise on the output, latency, and the buttons.

A future version may replace the built-in ADC with an external audio ADC (such as the PCM1808) for a much better signal-to-noise ratio.
