#!/usr/bin/env python3
"""Receives the ESP32 audio stream (src/main.cpp) over serial and plays it.
With the DMA firmware, the analogRead timing columns are always 0.

    .venv/bin/python tools/listen.py [--save file.wav]

With --save, writes the values sent by the ESP32 to a WAV file
(sample = (value - 2048) * 16) and the firmware stats to file.timing.csv. With
an effect on, those are the processed values; the effects column says which.

Keys: g toggles the noise gate, [ and ] change its threshold; c toggles the
chorus, m/M d/D r/R lower/raise its mix, depth and rate; t toggles the DAC
test tone, v/V lower/raise the DAC volume; q quits. With the ADC1 DMA test
firmware (src/dma_test.cpp), 0..6 average 1 to 64 conversions per sample;
--send 2 sends a key at start.
"""

import argparse
import array
import os
import sys
import termios
import threading
import time
import tty
import wave

import sounddevice as sd

RATE = 16000
CALLBACK_FRAMES = 128
STAT_HEADER = 0xC1
DMA_STAT_HEADER = 0xC2
STAT_NAMES = ("max_late_us", "late_over_2us", "late_over_10us", "late_over_50us",
              "late_over_10us_block_start", "dropped_blocks", "read_min_us",
              "read_mean_us_x10", "read_max_us", "min_slack_us", "effects",
              "gate_threshold_lsb", "fx_max_cycles",
              "chorus_mix_x100", "chorus_depth_ms_x10", "chorus_rate_hz_x100",
              "pool_overflows", "samples_per_s", "foreign_channel",
              "dac_short_writes", "dac_gain_db_plus_24", "dac_tone")
DMA_STAT_NAMES = ("pin", "mean_of", "samples_per_s", "dropped_blocks",
                  "pool_overflows", "foreign_channel")
FX_GATE = 1
FX_CHORUS = 2
KEYS = b"g[]cmMdDrRtvV0123456"


def open_serial(port, baud):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[4] = attrs[5] = getattr(termios, f"B{baud}")
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIFLUSH)
    return fd


class Player:
    """Keeps only latency_ms of audio queued: drops the excess (the ESP32
    clock runs slightly faster than the sound card) and plays silence when
    the queue runs dry."""

    def __init__(self, latency_ms):
        self.target = max(int(RATE * latency_ms / 1000), 2 * CALLBACK_FRAMES) * 2
        self.limit = self.target * 5 // 2
        self.buf = bytearray()
        self.lock = threading.Lock()
        self.playing = False
        # latency="low" (8 ms) played only ~40% of the samples on this PC.
        self.stream = sd.RawOutputStream(samplerate=RATE, channels=1, dtype="int16",
                                         blocksize=CALLBACK_FRAMES, latency=0.02,
                                         callback=self._callback)
        self.stream.start()

    def _callback(self, out, frames, _time, _status):
        n = frames * 2
        with self.lock:
            have = min(len(self.buf), n) if self.playing else 0
            out[:have] = self.buf[:have]
            out[have:] = bytes(n - have)
            del self.buf[:have]
            if have < n:
                self.playing = False

    def write(self, data):
        with self.lock:
            self.buf += data
            if len(self.buf) > self.limit:
                del self.buf[:len(self.buf) - self.target]
            if len(self.buf) >= self.target:
                self.playing = True

    def close(self):
        self.stream.stop()
        self.stream.close()


def forward_keys(fd):
    """Sends effect keys to the ESP32; q ends the session."""
    while True:
        key = os.read(sys.stdin.fileno(), 1)
        if not key or key == b"q":
            os._exit(0)
        if key in KEYS:
            os.write(fd, key)


def parse_stats(pkt):
    if not pkt or len(pkt) != 1 + 2 * pkt[0]:
        return None
    return [(pkt[1 + 2 * i] << 7) | pkt[2 + 2 * i] for i in range(pkt[0])]


class TimingCSV:
    """Created on the first stats packet, so firmware without stats leaves no
    empty file; flushed every row so a killed session keeps what it got."""

    def __init__(self, path):
        self.path = path
        self.names = STAT_NAMES
        self.file = None
        self.t0 = time.monotonic()

    def add(self, vals):
        n = len(vals)
        if self.file is None:
            names = [self.names[i] if i < len(self.names) else f"field_{i}"
                     for i in range(n)]
            self.file = open(self.path, "w")
            self.file.write("t_s," + ",".join(names) + "\n")
        self.file.write(f"{time.monotonic() - self.t0:.1f},"
                        + ",".join(map(str, vals)) + "\n")
        self.file.flush()

    def close(self):
        if self.file:
            self.file.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--gain", type=float, default=1.0)
    ap.add_argument("--latency", type=float, default=30, help="ms")
    ap.add_argument("--save", metavar="FILE.wav")
    ap.add_argument("--send", default="", help="keys to send at start")
    args = ap.parse_args()

    fd = open_serial(args.port, args.baud)
    player = Player(args.latency)
    wav = timing = None
    if args.save:
        wav = wave.open(args.save, "wb")
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        timing = TimingCSV(args.save.removesuffix(".wav") + ".timing.csv")

    if args.send:
        os.write(fd, args.send.encode())

    tty_attrs = None
    if sys.stdin.isatty():
        tty_attrs = termios.tcgetattr(sys.stdin)
        tty.setcbreak(sys.stdin)
        threading.Thread(target=forward_keys, args=(fd,), daemon=True).start()

    high = stat = dc = fx = threshold = chorus = dma = dac = None
    header = None
    lo, hi, sq, count = 4095, 0, 0.0, 0
    raw = array.array("h")
    t0 = time.monotonic()
    try:
        while True:
            try:
                data = os.read(fd, 4096)
            except OSError:
                data = b""
            if not data:
                raise SystemExit("serial port closed")
            out = array.array("h")
            for b in data:
                if b & 0x80:
                    vals = parse_stats(stat)
                    if vals and header == DMA_STAT_HEADER:
                        dma = vals
                        if timing:
                            timing.names = DMA_STAT_NAMES
                            timing.add(vals)
                    elif vals:
                        fx = vals[10] if len(vals) > 10 else None
                        threshold = vals[11] if len(vals) > 11 else None
                        chorus = vals[13:16] if len(vals) > 15 else None
                        dac = vals[19:22] if len(vals) > 21 else None
                        if timing:
                            timing.add(vals)
                    stat = [] if b in (STAT_HEADER, DMA_STAT_HEADER) else None
                    header = b
                    high = b & 0x3F if b & 0xC0 == 0x80 else None
                    continue
                if stat is not None:
                    stat.append(b)
                    continue
                if high is None:
                    continue
                v = (high << 6) | b
                high = None

                lo, hi = min(lo, v), max(hi, v)
                dc = v if dc is None else dc + (v - dc) * 0.0005
                sq += (v - dc) ** 2
                count += 1
                raw.append((v - 2048) * 16)
                out.append(int(max(-32768, min(32767, (v - dc) * 16 * args.gain))))

            player.write(out.tobytes())

            if time.monotonic() - t0 >= 1.0 and count:
                gate = "" if fx is None else (f"  gate {'on' if fx & FX_GATE else 'off'} {threshold}"
                                         f"  chorus {'on' if fx & FX_CHORUS else 'off'}")
                if dma:
                    gate = (f"  GPIO{dma[0]} mean of {dma[1]}  {dma[2]} samples/s"
                            f"  dropped {dma[3]} overflow {dma[4]} foreign {dma[5]}")
                if chorus:
                    gate += (f" mix {chorus[0] / 100:.1f} depth {chorus[1] / 10:.1f} ms"
                             f" rate {chorus[2] / 100:.2f} Hz")
                if dac:
                    gate += (f"  dac {dac[1] - 24:+d} dB{' tone' if dac[2] else ''}"
                             f"{f' short {dac[0]}' if dac[0] else ''}")
                print(f"raw {lo:4d}..{hi:4d}  rms {(sq / count) ** 0.5:5.1f}{gate}", flush=True)
                lo, hi, sq, count, t0 = 4095, 0, 0.0, 0, time.monotonic()
                if wav:
                    wav.writeframes(raw.tobytes())
                raw = array.array("h")
    finally:
        if tty_attrs:
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, tty_attrs)
        player.close()
        if wav:
            wav.writeframes(raw.tobytes())
            wav.close()
        if timing:
            timing.close()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
