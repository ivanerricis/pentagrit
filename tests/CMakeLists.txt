# SPDX-License-Identifier: AGPL-3.0-or-later
include(Catch)

add_executable(PentagritTests
  TestMain.cpp
  CrossoverTests.cpp
  TransientShaperTests.cpp
  SaturatorTests.cpp
  AutoGainTests.cpp
  EngineTests.cpp
  ParameterTests.cpp
  StateTests.cpp
  AllocationTests.cpp
  EditorTests.cpp
  PresetTests.cpp
  SettingsTests.cpp
  PerfTests.cpp)

target_link_libraries(PentagritTests PRIVATE PentagritCore Catch2::Catch2)
target_include_directories(PentagritTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

catch_discover_tests(PentagritTests DISCOVERY_MODE PRE_TEST)
