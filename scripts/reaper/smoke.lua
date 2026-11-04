-- Run from REAPER's Actions > Show action list > ReaScript > Load.
-- This creates a new project tab, never modifies an existing project, and writes
-- only into this repository's generated build/reaper-test directory.
local script = debug.getinfo(1, 'S').source:sub(2)
local folder = script:match('^(.*[/\\])')
local output = folder .. '../../build/reaper-test/'
local project = output .. 'butter-reaper-smoke.rpp'
local log
local function check(ok, message)
  if not ok then error(message, 0) end
  log:write('PASS: ' .. message .. '\n'); log:flush()
end
local function parameter(track, fx, name)
  for i = 0, reaper.TrackFX_GetNumParams(track, fx)-1 do
    local _, text = reaper.TrackFX_GetParamName(track, fx, i)
    if text == name then return i end
  end
  error('Missing parameter: ' .. name)
end
local function run()
  reaper.RecursiveCreateDirectory(output, 0)
  log = assert(io.open(output .. 'host-checks.txt', 'w'))
  log:write('REAPER ' .. reaper.GetAppVersion() .. '\n')
  local source = io.open(output .. 'input.wav', 'rb')
  check(source ~= nil, 'Generated stereo test input exists')
  source:close()
  reaper.Main_OnCommand(40859, 0) -- New project tab, preserve other project tabs.
  reaper.InsertTrackAtIndex(0, true)
  local track = reaper.GetTrack(0, 0)
  reaper.GetSetMediaTrackInfo_String(track, 'P_NAME', 'ButterComp integration test', true)
  reaper.SetOnlyTrackSelected(track)
  reaper.SetEditCurPos(0, false, false)
  reaper.InsertMedia(output .. 'input.wav', 0)
  check(reaper.CountTrackMediaItems(track) == 1, 'Stereo WAV imported into test track')
  local fx = reaper.TrackFX_AddByName(track, 'VST3: ButterComp Studio', false, -1)
  check(fx >= 0, 'REAPER discovers and instantiates ButterComp Studio VST3')
  local compress = parameter(track, fx, 'Compress')
  local gain = parameter(track, fx, 'Output')
  local mix = parameter(track, fx, 'Mix')
  reaper.TrackFX_SetParamNormalized(track, fx, compress, .8)
  reaper.TrackFX_SetParamNormalized(track, fx, gain, .5) -- 1x linear output.
  reaper.TrackFX_SetParamNormalized(track, fx, mix, .75)
  reaper.Main_SaveProjectEx(0, project, 0)
  -- Deliberately change the live value, then reopen the saved project. Query the
  -- newly instantiated plugin; do not mistake a same-instance get/set for recall.
  reaper.TrackFX_SetParamNormalized(track, fx, compress, .1)
  reaper.MarkProjectDirty(0)
  reaper.Main_OnCommand(40859, 0) -- Reopen saved fixture in a separate new tab.
  reaper.Main_openProject(project)
  track = reaper.GetTrack(0, 0)
  check(track ~= nil, 'Saved REAPER project reopened')
  fx = 0
  local _, name = reaper.TrackFX_GetFXName(track, fx)
  check(name:find('ButterComp', 1, true) ~= nil, 'VST3 restored from project')
  compress = parameter(track, fx, 'Compress')
  gain = parameter(track, fx, 'Output')
  mix = parameter(track, fx, 'Mix')
  check(math.abs(reaper.TrackFX_GetParamNormalized(track, fx, compress)-.8) < 1e-5, 'Compress restored to 80%')
  check(math.abs(reaper.TrackFX_GetParamNormalized(track, fx, gain)-.5) < 1e-5, 'Output restored to 1x')
  check(math.abs(reaper.TrackFX_GetParamNormalized(track, fx, mix)-.75) < 1e-5, 'Mix restored to 75%')
  reaper.TrackFX_Show(track, fx, 3) -- Floating native editor.
  check(reaper.TrackFX_GetFloatingWindow(track, fx) ~= nil, 'Native VST3 editor opened in REAPER')
  -- Render through REAPER's actual master bus, using an isolated test project.
  reaper.GetSetProjectInfo(0, 'RENDER_SETTINGS', 0, true)
  reaper.GetSetProjectInfo(0, 'RENDER_BOUNDSFLAG', 0, true)
  reaper.GetSetProjectInfo(0, 'RENDER_STARTPOS', 0, true)
  reaper.GetSetProjectInfo(0, 'RENDER_ENDPOS', 8, true)
  reaper.GetSetProjectInfo(0, 'RENDER_SRATE', 48000, true)
  reaper.GetSetProjectInfo(0, 'RENDER_CHANNELS', 2, true)
  reaper.GetSetProjectInfo(0, 'RENDER_TAILFLAG', 0, true)
  reaper.GetSetProjectInfo_String(0, 'RENDER_FILE', output, true)
  reaper.GetSetProjectInfo_String(0, 'RENDER_PATTERN', 'processed', true)
  reaper.GetSetProjectInfo_String(0, 'RENDER_FORMAT', 'ZXZhdxgAAA==', true) -- PCM WAV 24-bit.
  -- Reject stale output as evidence: remove only this harness's known output.
  if reaper.file_exists(output .. 'processed.wav') then
    assert(os.remove(output .. 'processed.wav'))
  end
  reaper.Main_OnCommand(41824, 0) -- Render using current settings, auto-close.
  local rendered = io.open(output .. 'processed.wav', 'rb')
  check(rendered ~= nil, 'REAPER rendered processed.wav')
  rendered:close()
  -- Playback allows inspection of live needle movement and return to rest.
  reaper.SetEditCurPos(0, false, false)
  reaper.OnPlayButton()
  log:write('MANUAL CHECK PENDING: Observe unequal L/R needles during loud audio and release during final silence.\n')
  log:write('Run python3 scripts/reaper/analyse.py for render signal checks.\n')
  log:close(); log = nil
end
local ok, message = xpcall(run, debug.traceback)
if not ok then
  if log then log:write('FAIL: ' .. message .. '\n'); log:close() end
  reaper.ShowMessageBox(message, 'ButterComp REAPER test failed', 0)
end
