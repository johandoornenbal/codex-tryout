# REAPER VST3 integration test

Status: **prepared, not run in REAPER**. REAPER is not installed in this cloud
instance, and the official download was denied by the network proxy (HTTP 403).
The saved network draft adds `reaper.fm` and `www.reaper.fm`; it does not install
REAPER or establish that the host test passed. The existing four CTest suites
passed independently. `smoke.lua` passed Lua 5.4 syntax validation. No plugin
application code was changed for this test preparation.

## Run

Use an isolated/portable REAPER configuration for testing. For Linux, scan the
absolute `build/ButterCompStudio_artefacts/Release/VST3` directory in REAPER's
Preferences > Plug-ins > VST. On macOS, first build and install the arm64 plugin
as described in the repository README, then rescan in REAPER.

1. From the repository root, run `python3 scripts/reaper/prepare.py`.
2. In REAPER, open Actions > Show action list, load `scripts/reaper/smoke.lua`
   with ReaScript > Load, and run it.
3. It creates a new project tab with the generated stereo WAV, loads the VST3,
   sets Compress to 80%, Output to 1x, and Mix to 75%, saves a project, and reopens
   it in another tab to check actual host state recall. Existing project tabs
   are preserved. All generated output stays in `build/reaper-test`.
4. It opens the native editor, renders `processed.wav`, then starts playback.
   Watch the left/right needles move during loud audio and release during the
   ending silence. Capture a screenshot of the plugin inside REAPER. Visual
   needle behavior remains a separate manual check, not a Lua pass result.
5. Run `python3 scripts/reaper/analyse.py` to check the actual REAPER render for
   correct stereo format, duration, non-silent finite audio and measurable
   processing relative to the dry fixture.

Results are in `build/reaper-test/host-checks.txt`, alongside the project, input
WAV and rendered WAV. The log distinguishes automatic passes from the pending
visual check. Existing `processed.wav` is removed before rendering to prevent
stale files being counted as evidence. Stop playback and close the generated test
project tabs when finished.

These scripts have not yet been exercised against REAPER. Their runtime behavior
and API/action assumptions must be verified in the host before treating this as
an automated regression test. Linux success would not establish Apple Silicon
compatibility or macOS signing/DAW behavior.
