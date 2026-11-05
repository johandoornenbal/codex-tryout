cmake_minimum_required(VERSION 3.22)
project(ButterCompStudio VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)
add_library(butter_engine Source/ButterEngine.cpp)
set_target_properties(butter_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(butter_engine PUBLIC Source)
if(BUILD_TESTING)
  add_executable(dsp_tests tests/DspTests.cpp)
  target_link_libraries(dsp_tests PRIVATE butter_engine)
  add_test(NAME butter_dsp COMMAND dsp_tests)
  add_executable(reference_tests tests/ReferenceTests.cpp tests/ReferenceEngine.cpp)
  target_link_libraries(reference_tests PRIVATE butter_engine)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(tests/ReferenceEngine.cpp PROPERTIES COMPILE_OPTIONS "-fwrapv")
  endif()
  add_test(NAME upstream_reference COMMAND reference_tests)
  add_executable(meter_tests tests/MeterTests.cpp)
  target_link_libraries(meter_tests PRIVATE butter_engine)
  add_test(NAME gain_reduction COMMAND meter_tests)
  add_library(ebur128_reference STATIC third_party/libebur128/ebur128.c)
  target_include_directories(ebur128_reference PUBLIC third_party/libebur128 PRIVATE third_party/libebur128/queue)
  if(UNIX)
    target_link_libraries(ebur128_reference PUBLIC m)
  endif()
  add_executable(loudness_tests tests/LoudnessTests.cpp)
  target_link_libraries(loudness_tests PRIVATE butter_engine ebur128_reference)
  add_test(NAME loudness_match COMMAND loudness_tests)
endif()
option(BUTTER_DSP_ONLY "Build DSP tests without GUI dependencies" OFF)
if(BUTTER_DSP_ONLY)
  return()
endif()
include(FetchContent)
set(JUCE_PATH "" CACHE PATH "Optional existing JUCE 8.0.12 checkout")
if(JUCE_PATH)
  add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
else()
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850 GIT_SHALLOW FALSE)
  FetchContent_MakeAvailable(JUCE)
endif()
juce_add_plugin(ButterCompStudio
  COMPANY_NAME "Doornenbal Audio"
  BUNDLE_ID "nl.doornenbal.buttercompstudio"
  PLUGIN_MANUFACTURER_CODE Jdau
  PLUGIN_CODE Bcs2
  FORMATS VST3
  PRODUCT_NAME "ButterComp Studio"
  IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE VST3_CATEGORIES Fx Dynamics)
juce_generate_juce_header(ButterCompStudio)
target_sources(ButterCompStudio PRIVATE Source/Plugin.cpp)
target_compile_definitions(ButterCompStudio PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(ButterCompStudio PRIVATE butter_engine juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

if(BUILD_TESTING)
  add_executable(plugin_checks tests/PluginChecks.cpp)
  target_include_directories(plugin_checks PRIVATE "${JUCE_MODULES_DIR}")
  target_compile_definitions(plugin_checks PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
  target_link_libraries(plugin_checks PRIVATE ButterCompStudio)
  add_test(NAME plugin_wrapper COMMAND plugin_checks)
endif()
