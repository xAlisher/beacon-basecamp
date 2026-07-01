cmake_minimum_required(VERSION 3.14)
project(BeaconUi LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found")
endif()

# Universal ui_qml module: REP_FILE runs repc on beacon_ui.rep; the generated
# *Plugin glue + logos_sdk.h (from metadata.json#dependencies) compile in.
logos_module(
    NAME beacon_ui
    REP_FILE src/beacon_ui.rep
    SOURCES
        src/beacon_ui_backend.h
        src/beacon_ui_backend.cpp
    INCLUDE_DIRS
        src
)
