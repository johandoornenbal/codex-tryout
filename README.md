# ButterComp Studio

An independent VST3 adaptation of **Airwindows ButterComp2**, with a native cream,
charcoal and brass editor. For Apple Silicon Macs, including M2 and later.

![Rendered plugin editor](docs/ui-preview.png)

Three automatable controls retain ButterComp2's original ranges and defaults:

| Control | Range | Default |
| --- | --- | --- |
| Compress | 0–100% | 0% |
| Output | 0–2× linear gain | 1× |
| Mix | 0–100% wet | 100% |

Includes two illuminated analogue gain-reduction meters, editable numeric values, double-click reset,
32/64-bit audio processing and host session state recall. Stereo input/output only.
Meters display the wet compressor gain element in dB, before output gain and
parallel mix, with independent left/right needles and a 0-30 dB scale.
The compressor is active internally even at 0% mix, so the meters still show wet-path
reduction. Input drive and automatic gain compensation are excluded from this
reading. A 12 ms attack and 280 ms release give the needles analogue-style motion;
needles return to rest during silence, host bypass and stopped processing.
Readings above 30 dB pin the needles at the end of the scale. The original DSP is not a limiter;
output can exceed 0 dBFS. No extra attack, release or threshold controls are invented.
This plugin has its own identity and does not replace existing Airwindows instances.

## Build on your Mac

Install Xcode Command Line Tools (`xcode-select --install`) and CMake 3.22 or newer
(for example `brew install cmake`). From the repository root:

```sh
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build-mac --config Release --parallel 3
ctest --test-dir build-mac -C Release --output-on-failure
```

The first configure downloads the pinned JUCE source. An existing JUCE 8.0.12
checkout can be supplied with `-DJUCE_PATH=/absolute/path/to/JUCE`.

The output is `build-mac/ButterCompStudio_artefacts/Release/VST3/ButterComp Studio.vst3`.
To install locally:

```sh
codesign --force --deep --sign - "build-mac/ButterCompStudio_artefacts/Release/VST3/ButterComp Studio.vst3"
mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3"
ditto "build-mac/ButterCompStudio_artefacts/Release/VST3/ButterComp Studio.vst3" \
  "$HOME/Library/Audio/Plug-Ins/VST3/ButterComp Studio.vst3"
```

Restart or rescan plugins in your VST3-compatible DAW. Logic Pro requires Audio Unit
and cannot load this VST3 target. Ad-hoc signing is for local use; distributing a
trusted macOS release requires Developer ID signing and notarization.

The GitHub Actions workflow builds, tests, ad-hoc signs and archives an arm64 VST3
on macOS. Once the code is pushed to GitHub, its downloadable artifact appears in
that workflow run. The workflow has not been run merely by creating this file.

## Validation

```sh
# DSP-only validation on Linux/macOS, no JUCE or audio device needed
cmake -S . -B build-dsp -DBUTTER_DSP_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-dsp --parallel 2
ctest --test-dir build-dsp --output-on-failure
```

DSP tests exercise multiple sample rates, block sizes, dry mix, output muting,
noise floor, float/double consistency and reset. A separate reference test compiles
the untouched upstream processing source and compares 48 scenarios. A gain-reduction suite checks calibration against rendered audio, channel
independence, output/mix isolation, release behavior and float/double consistency
at four sample rates. The wrapper
check tests session-state recall, stereo processing, both meter components and PNG render.
It writes `ui-preview.png` into its build working directory.

Native Apple Silicon compilation, macOS signing and DAW loading require macOS and
remain separate from Linux verification. Before a release, run the macOS workflow
and test automation, state recall, playback and editor behavior in your DAW.

## License and attribution

ButterComp2 DSP © Chris Johnson / Airwindows, MIT. Original source and license are
in `third_party/airwindows`; provenance and adaptation details are in
[THIRD_PARTY.md](THIRD_PARTY.md). This is not an official Airwindows release.

The wrapper and UI are MIT. JUCE has separate AGPLv3/commercial licensing; comply
with the selected JUCE license when distributing binaries. See THIRD_PARTY.md.

## Current verification

In this Linux cloud instance: VST3 compilation and all four CTest suites passed.
The suites cover 29 DSP scenarios, 48 upstream-reference comparisons (maximum
absolute error 3.55271e-15), gain-reduction calibration and ballistics at four sample rates, and wrapper
state/audio/editor checks. The image above
is rendered from the actual native editor, not a design mockup. The reusable cloud
setup script was also executed successfully. No Apple Silicon binary or DAW test
has been produced in this Linux environment.
