#!/usr/bin/env python3
"""Check REAPER's rendered WAV; requires an actual completed host render."""
from pathlib import Path
import math
import struct
import wave

root = Path(__file__).resolve().parents[2] / 'build' / 'reaper-test'

def read(path):
    with wave.open(str(path), 'rb') as wav:
        if wav.getnchannels() != 2 or wav.getframerate() != 48000:
            raise SystemExit(f'FAIL: wrong format: {path}')
        width, count = wav.getsampwidth(), wav.getnframes()
        raw = wav.readframes(count)
    if width not in (2, 3, 4):
        raise SystemExit(f'FAIL: unsupported PCM width {width}')
    values = [int.from_bytes(raw[i:i+width], 'little', signed=True) / 2**(width*8-1)
              for i in range(0, len(raw), width)]
    return values[::2], values[1::2]

try:
    before, after = read(root / 'input.wav'), read(root / 'processed.wav')
except (FileNotFoundError, wave.Error) as exc:
    raise SystemExit(f'NOT RUN: a valid REAPER render is required: {exc}')
for channel in range(2):
    a, b = before[channel], after[channel]
    if abs(len(a) - len(b)) > 1:
        raise SystemExit('FAIL: rendered duration differs from the eight-second fixture')
    # Test sustained audio, excluding silence and compressor startup.
    start, end = 2*48000, 6*48000
    signal = b[start:end]
    rms = math.sqrt(sum(x*x for x in signal) / len(signal))
    error = math.sqrt(sum((x-y)**2 for x, y in zip(a[start:end], signal)) / len(signal))
    if not all(math.isfinite(x) for x in b) or not (.0001 < rms < 1):
        raise SystemExit('FAIL: invalid or silent output')
    if error < 1e-5:
        raise SystemExit('FAIL: rendered audio matches dry input; processing may be bypassed')
    print(f'PASS channel {channel+1}: RMS {20*math.log10(rms):.2f} dBFS; difference from dry {error:.6f}')
print('PASS: valid eight-second stereo host render with measurable processing on both channels')
