# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)

# CMake 4 dropped compatibility with projects asking for < 3.5 (some dependencies still do).
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Pentagrit VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(PENTAGRIT_BUILD_TESTS "Build the Catch2 test suite" ON)

include(cmake/Dependencies.cmake)
include(cmake/CompilerSettings.cmake)

configure_file(PluginInfo.h.in "${CMAKE_BINARY_DIR}/generated/PluginInfo.h" @ONLY)

# ---- Core library: everything in src/ except PluginEntry.cpp (shared by plugin and tests) ----
file(GLOB_RECURSE PENTAGRIT_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER PENTAGRIT_CORE_SOURCES EXCLUDE REGEX "PluginEntry\\.cpp$")

# Embedded UI fonts (IBM Plex plus the per-style families; all SIL OFL 1.1, licences in resources/ui/fonts/licenses).
file(GLOB PENTAGRIT_FONT_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/ui/fonts/*.ttf")
juce_add_binary_data(PentagritUiAssets
  HEADER_NAME PentagritUiData.h
  NAMESPACE PentagritUiData
  SOURCES ${PENTAGRIT_FONT_FILES})

# ---- Factory presets embedded as binary data (resources/presets/*.xml) ----
file(GLOB PENTAGRIT_PRESET_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/presets/*.xml")
juce_add_binary_data(PentagritPresetData
  HEADER_NAME PentagritPresetData.h
  NAMESPACE PentagritPresetData
  SOURCES ${PENTAGRIT_PRESET_FILES})
set_target_properties(PentagritPresetData PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(PentagritCore INTERFACE)
target_sources(PentagritCore INTERFACE ${PENTAGRIT_CORE_SOURCES})
target_include_directories(PentagritCore INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(PentagritCore INTERFACE
  PentagritCompilerSettings
  PentagritPresetData
  juce::juce_audio_utils
  juce::juce_dsp
  PentagritUiAssets)

# ---- Plugin ----
set(PENTAGRIT_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND PENTAGRIT_FORMATS AU)
endif()

juce_add_plugin(Pentagrit
  COMPANY_NAME "Pentagrit"
  PLUGIN_MANUFACTURER_CODE Ivnr
  PLUGIN_CODE Pgrt
  FORMATS ${PENTAGRIT_FORMATS}
  PRODUCT_NAME "Pentagrit"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  VST3_CATEGORIES Fx Distortion
  AU_MAIN_TYPE kAudioUnitType_Effect
  COPY_PLUGIN_AFTER_BUILD FALSE
  BUNDLE_ID com.ivanerricis.pentagrit)

target_sources(Pentagrit PRIVATE src/PluginEntry.cpp)
target_link_libraries(Pentagrit PRIVATE PentagritCore)

clap_juce_extensions_plugin(
  TARGET Pentagrit
  CLAP_ID "com.ivanerricis.pentagrit"
  CLAP_FEATURES audio-effect distortion)

# ---- Tests ----
if(PENTAGRIT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
