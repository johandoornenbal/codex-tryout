# Source provenance

Airwindows ButterComp2: https://github.com/airwindows/airwindows

Retrieved 2026-11-04, upstream HEAD `7fd5f5cae262a41e6dd7d9a83d60c46e84d3092a`.
ButterComp2 is the newest numbered ButterComp variant found in that tree.
Original files and MIT license are retained in `third_party/airwindows`.

`Source/ButterEngine.cpp` retains the upstream float/double processing body.
Changes: replace VST2 wrapper with a standalone engine; make residue noise counters
per instance; use unsigned arithmetic for defined overflow; use fixed nonzero
initial dither seeds; simplify pointer increments. Add read-only gain-element observation for the stereo reduction meters; this
observation does not feed back into audio processing. Compression equations, control
ranges, defaults, sample-rate scaling and wet/dry ordering are retained.
This is an independent adaptation, not an official Airwindows release.

JUCE 8.0.12 is pinned to `29396c22c93392d6738e021b83196283d6e4d850`.
JUCE is dual-licensed under AGPLv3 and commercial terms. The wrapper's MIT license
does not relicense JUCE. For distribution, comply with JUCE's AGPLv3 terms
(including corresponding source) or obtain a suitable JUCE commercial license.
See https://github.com/juce-framework/JUCE/blob/8.0.12/LICENSE.md.
The VST3 SDK bundled with JUCE has its own license notices, which must be retained.

Level matching is an independent post-engine stage. Its stereo BS.1770
K-weighting filter design follows libebur128 by Jan Kokemueller (MIT), pinned at
`67b33abe1558160ed76ada1322329b0e9e058b02` from
https://github.com/jiixyj/libebur128. The upstream C implementation and license
are retained in `third_party/libebur128` as an independent test oracle; they are
not linked into the plugin. Queue compatibility code retains its embedded BSD
license notices. `Source/LoudnessMatch.h` uses bounded 400 ms windows, 100 ms hops,
a -70 LUFS absolute gate and -10 LU relative gate for learning.
