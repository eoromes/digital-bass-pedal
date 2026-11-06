#!/usr/bin/env python3
"""Receives the ESP32 audio stream (src/main.cpp) over serial and plays it.

    .venv/bin/python tools/listen.py [--save file.wav]

With --save, writes the values sent by the ESP32 to a WAV file
(sample = (value - 2048) * 16) and the firmware stats to file.timing.csv. With
an effect on, those are the processed values; the effects column says which.

Keys: g toggles the noise gate, [ and ] change its threshold, q quits.
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
STAT_NAMES = ("max_late_us", "late_over_2us", "late_over_10us", "late_over_50us",
              "late_over_10us_block_start", "dropped_blocks", "read_min_us",
              "read_mean_us_x10", "read_max_us", "min_slack_us", "effects",
              "gate_threshold_lsb", "fx_max_cycles")
FX_GATE = 1
KEYS = b"g[]"


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
        self.file = None
        self.t0 = time.monotonic()

    def add(self, vals):
        n = len(vals)
        if self.file is None:
            names = [STAT_NAMES[i] if i < len(STAT_NAMES) else f"field_{i}"
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

    tty_attrs = None
    if sys.stdin.isatty():
        tty_attrs = termios.tcgetattr(sys.stdin)
        tty.setcbreak(sys.stdin)
        threading.Thread(target=forward_keys, args=(fd,), daemon=True).start()

    high = stat = dc = fx = threshold = None
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
                    if vals:
                        fx = vals[10] if len(vals) > 10 else None
                        threshold = vals[11] if len(vals) > 11 else None
                        if timing:
                            timing.add(vals)
                    stat = [] if b == STAT_HEADER else None
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
                gate = "" if fx is None else f"  gate {'on' if fx & FX_GATE else 'off'} {threshold}"
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
