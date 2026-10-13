# SPDX-FileCopyrightText: (C) 2026 FlowPlus contributors
# SPDX-License-Identifier: MIT

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.24)
  project(flowplus-startup-tests LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_AUTOMOC ON)
  add_compile_definitions(QT_NO_KEYWORDS)
  find_package(Qt6 6.7 REQUIRED COMPONENTS Widgets Test)
  enable_testing()
endif()

add_executable(AccessibilitySetupDialogTests
  AccessibilitySetupDialogTests.cpp
  ../../apps/deskflow-gui/AccessibilitySetupDialog.cpp
  ../../apps/deskflow-gui/AccessibilitySetupDialog.h
)
target_include_directories(AccessibilitySetupDialogTests PRIVATE ../../apps/deskflow-gui)
target_link_libraries(AccessibilitySetupDialogTests PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME AccessibilitySetupDialogTests COMMAND AccessibilitySetupDialogTests -o -,txt)
set_tests_properties(AccessibilitySetupDialogTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  if(WIN32)
    set_target_properties(AccessibilitySetupDialogTests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/AccessibilitySetupDialogTests"
    )
  endif()
  set_property(GLOBAL APPEND PROPERTY ${CMAKE_PROJECT_NAME}_tests AccessibilitySetupDialogTests)
endif()
