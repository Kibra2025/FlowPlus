# SPDX-FileCopyrightText: (C) 2026 FlowPlus contributors
# SPDX-License-Identifier: MIT

# This directory also configures independently, so protocol/HTTP regressions can
# run without building the operating-system input hooks or installing OpenSSL.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.24)
  project(flowplus-transfer-tests LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_AUTOMOC ON)
  add_compile_definitions(QT_NO_KEYWORDS)
  find_package(Qt6 6.7 REQUIRED COMPONENTS Core Network Widgets Test)
  enable_testing()
  get_filename_component(FLOWPLUS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
  add_library(flowplus-transfer STATIC
    "${FLOWPLUS_SOURCE_DIR}/src/lib/gui/transfer/LocalSendService.cpp"
    "${FLOWPLUS_SOURCE_DIR}/src/lib/gui/transfer/LocalSendService.h"
  )
  target_include_directories(flowplus-transfer PUBLIC "${FLOWPLUS_SOURCE_DIR}/src/lib")
  target_link_libraries(flowplus-transfer PUBLIC Qt6::Core Qt6::Network)
  set(transfer_dependency flowplus-transfer)
  add_library(flowplus-drop STATIC
    "${FLOWPLUS_SOURCE_DIR}/src/lib/gui/transfer/NativeDropTarget.cpp"
  )
  target_include_directories(flowplus-drop PUBLIC "${FLOWPLUS_SOURCE_DIR}/src/lib")
  target_link_libraries(flowplus-drop PUBLIC Qt6::Core)
  if(WIN32)
    target_sources(flowplus-drop PRIVATE "${FLOWPLUS_SOURCE_DIR}/src/lib/gui/transfer/NativeDropTargetWindows.cpp")
    target_link_libraries(flowplus-drop PUBLIC ole32 oleaut32 uuid oleacc shell32)
  elseif(APPLE)
    enable_language(OBJCXX)
    set_target_properties(flowplus-drop PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON)
    target_sources(flowplus-drop PRIVATE
      "${FLOWPLUS_SOURCE_DIR}/src/lib/gui/transfer/NativeDropTargetMac.mm"
      "${FLOWPLUS_SOURCE_DIR}/src/lib/gui/transfer/NativeDragSource.mm"
    )
    target_link_libraries(flowplus-drop PUBLIC Qt6::Gui)
    target_link_libraries(flowplus-drop PUBLIC "-framework Cocoa" "-framework ApplicationServices")
  endif()
  set(drop_dependency flowplus-drop)
  add_library(flowplus-protocol STATIC
    "${FLOWPLUS_SOURCE_DIR}/src/lib/deskflow/ProtocolUtil.cpp"
    "${FLOWPLUS_SOURCE_DIR}/src/lib/deskflow/DeskflowException.cpp"
    "${FLOWPLUS_SOURCE_DIR}/src/lib/base/BaseException.cpp"
    "${FLOWPLUS_SOURCE_DIR}/src/lib/base/String.cpp"
    "${FLOWPLUS_SOURCE_DIR}/src/lib/io/IOException.cpp"
  )
  target_compile_definitions(flowplus-protocol PRIVATE NOLOGGING)
  target_include_directories(flowplus-protocol PUBLIC "${FLOWPLUS_SOURCE_DIR}/src/lib")
  target_link_libraries(flowplus-protocol PUBLIC Qt6::Core)
  set(protocol_dependency flowplus-protocol)
else()
  set(transfer_dependency gui)
  set(drop_dependency gui)
  set(protocol_dependency app base io arch)
  if(WIN32)
    list(APPEND protocol_dependency platform version)
  endif()
endif()

add_executable(TransferProtocolTests TransferProtocolTests.cpp)
target_link_libraries(TransferProtocolTests PRIVATE ${transfer_dependency} Qt6::Test)
add_test(NAME TransferProtocolTests COMMAND TransferProtocolTests -o -,txt)

add_executable(ProtocolUtilLimitsTests ProtocolUtilLimitsTests.cpp)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  target_compile_definitions(ProtocolUtilLimitsTests PRIVATE NOLOGGING)
endif()
target_link_libraries(ProtocolUtilLimitsTests PRIVATE ${protocol_dependency} Qt6::Test)
add_test(NAME ProtocolUtilLimitsTests COMMAND ProtocolUtilLimitsTests -o -,txt)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_executable(TransferInteractionTests
    ../gui/DragDropManagerTests.cpp
    ../../lib/gui/transfer/DragOverlayWidget.cpp
    ../../lib/gui/transfer/DragOverlayWidget.h
  )
  target_link_libraries(TransferInteractionTests PRIVATE ${transfer_dependency} Qt6::Widgets Qt6::Test)
  add_test(NAME TransferInteractionTests COMMAND TransferInteractionTests -o -,txt)
endif()

add_executable(LocalSendServiceTests ../gui/transfer/LocalSendServiceTests.cpp)
target_link_libraries(LocalSendServiceTests PRIVATE ${transfer_dependency} Qt6::Test)
add_test(NAME LocalSendServiceTests COMMAND LocalSendServiceTests -o -,txt)

add_executable(NativeDropTargetTests NativeDropTargetTests.cpp)
target_link_libraries(NativeDropTargetTests PRIVATE ${drop_dependency} Qt6::Test)
add_test(NAME NativeDropTargetTests COMMAND NativeDropTargetTests -o -,txt)

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  if(WIN32)
    # vcpkg's post-link deployment and windeployqt must not read/write DLLs
    # in the same output directory while these targets link in parallel.
    foreach(transfer_test TransferProtocolTests ProtocolUtilLimitsTests LocalSendServiceTests NativeDropTargetTests)
      set_target_properties(${transfer_test} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${transfer_test}"
      )
    endforeach()
  endif()
  set_property(GLOBAL APPEND PROPERTY ${CMAKE_PROJECT_NAME}_tests TransferProtocolTests ProtocolUtilLimitsTests LocalSendServiceTests NativeDropTargetTests)
endif()
