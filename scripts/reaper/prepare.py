#!/usr/bin/env python3
"""Generate the deterministic stereo input used by the REAPER host smoke test."""
from pathlib import Path
import math
import struct
import wave

root = Path(__file__).resolve().parents[2]
out = root / 'build' / 'reaper-test'
out.mkdir(parents=True, exist_ok=True)
rate = 48000
with wave.open(str(out / 'input.wav'), 'wb') as wav:
    wav.setparams((2, 2, rate, 0, 'NONE', 'not compressed'))
    frames = bytearray()
    for i in range(rate * 8):
        t = i / rate
        # Quiet opening, strong stereo compression, quiet ending for needle release.
        envelope = 0 if t < .5 or t >= 6.5 else min(1, (t - .5) / .05, (6.5 - t) / .05)
        left = envelope * .9 * math.sin(2 * math.pi * 220 * t)
        right = envelope * .22 * math.sin(2 * math.pi * 173 * t)
        frames.extend(struct.pack('<hh', round(left * 32767), round(right * 32767)))
    wav.writeframes(frames)
print(out / 'input.wav')
