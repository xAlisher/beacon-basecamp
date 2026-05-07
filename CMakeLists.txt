cmake_minimum_required(VERSION 3.14)
project(BeaconPlugin LANGUAGES CXX)

# ── Nix builder path ──────────────────────────────────────────────────────────
# When built via logos-module-builder (nix build .#packages.x86_64-linux.lgx-portable),
# LOGOS_MODULE_BUILDER_ROOT is set. logos_module() registers the target so the
# builder can locate beacon_plugin.so.
if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)

    logos_module(
        NAME logos_beacon
        SOURCES
            src/plugin/BeaconPlugin.h
            src/plugin/BeaconPlugin.cpp
        INCLUDE_DIRS
            src
    )

else()
# ── Local dev path ────────────────────────────────────────────────────────────
# Used by: cmake -B build && cmake --build build && cmake --install build

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# ── Qt ────────────────────────────────────────────────────────────────────────
list(PREPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.9.3/gcc_64")
find_package(Qt6 REQUIRED COMPONENTS Core Qml)
qt_standard_project_setup()

# ── Nix store paths ───────────────────────────────────────────────────────────
if(DEFINED ENV{LOGOS_LIBLOGOS_HEADERS})
    set(LOGOS_LIBLOGOS_HEADERS "$ENV{LOGOS_LIBLOGOS_HEADERS}")
else()
    set(LOGOS_LIBLOGOS_HEADERS
        "/nix/store/8cgbzy0jczrpizyga4i4pfjba77m3k7f-logos-liblogos-headers-0.1.0/include")
endif()

if(DEFINED ENV{LOGOS_CPP_SDK_ROOT})
    set(LOGOS_CPP_SDK "$ENV{LOGOS_CPP_SDK_ROOT}")
else()
    set(LOGOS_CPP_SDK
        "/nix/store/rbsasyd55ijnxliw30j2r4ph7v5x6ljs-logos-cpp-sdk")
endif()

# ── Plugin ────────────────────────────────────────────────────────────────────
add_library(logos_beacon_plugin MODULE
    src/plugin/BeaconPlugin.h
    src/plugin/BeaconPlugin.cpp
)

set_target_properties(logos_beacon_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "logos_beacon_plugin"
)

target_include_directories(logos_beacon_plugin PRIVATE
    src
    "${LOGOS_LIBLOGOS_HEADERS}"
    "${LOGOS_CPP_SDK}/include"
    "${LOGOS_CPP_SDK}/include/cpp"
    "${LOGOS_CPP_SDK}/include/core"
)

# Note: Do NOT add --whole-archive around liblogos_sdk.a — it pulls in
# QRemoteObjectRegistryHost(QUrl,QObject*) which was removed in Qt 6.9.3,
# causing load failure outside the AppImage.
target_link_libraries(logos_beacon_plugin PRIVATE
    Qt6::Core
    Qt6::Qml
    "${LOGOS_CPP_SDK}/lib/liblogos_sdk.a"
    dl pthread
)

configure_file(
    src/plugin/plugin_metadata.json
    ${CMAKE_CURRENT_BINARY_DIR}/src/plugin/plugin_metadata.json
    COPYONLY
)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test_beacon_plugin
    tests/test_beacon_plugin.cpp
    tests/logos_api_stub.cpp
    src/plugin/BeaconPlugin.h
    src/plugin/BeaconPlugin.cpp
    "${LOGOS_CPP_SDK}/include/cpp/logos_api.h"
)

target_include_directories(test_beacon_plugin PRIVATE
    src
    "${LOGOS_LIBLOGOS_HEADERS}"
    "${LOGOS_CPP_SDK}/include"
    "${LOGOS_CPP_SDK}/include/cpp"
    "${LOGOS_CPP_SDK}/include/core"
)

target_link_libraries(test_beacon_plugin PRIVATE
    Qt6::Core
    Qt6::Test
    dl pthread
)

add_test(NAME test_beacon_plugin COMMAND test_beacon_plugin)

# ── Install paths ─────────────────────────────────────────────────────────────
set(LOGOS_BASECAMP_DATA "$ENV{HOME}/.local/share/Logos/LogosBasecamp")
set(BEACON_MODULE_DIR "${LOGOS_BASECAMP_DATA}/modules/logos_beacon")
set(BEACON_UI_DIR     "${LOGOS_BASECAMP_DATA}/plugins/beacon_ui")

# Clean old module dir on reinstall
install(CODE "
    set(_modules_dir \"\\$ENV{DESTDIR}${LOGOS_BASECAMP_DATA}/modules\")
    file(GLOB _old \"\${_modules_dir}/logos_beacon*\")
    foreach(_dir \${_old})
        file(REMOVE_RECURSE \"\${_dir}\")
    endforeach()
")

install(TARGETS logos_beacon_plugin LIBRARY DESTINATION "${BEACON_MODULE_DIR}")
install(FILES
    modules/logos_beacon/manifest.json
    modules/logos_beacon/metadata.json
    modules/logos_beacon/plugin_metadata.json
    modules/logos_beacon/variant
    DESTINATION "${BEACON_MODULE_DIR}"
)
install(FILES
    plugins/beacon_ui/manifest.json
    plugins/beacon_ui/metadata.json
    plugins/beacon_ui/Main.qml
    plugins/beacon_ui/variant
    DESTINATION "${BEACON_UI_DIR}"
)
install(FILES
    plugins/beacon_ui/beacon.png
    DESTINATION "${BEACON_UI_DIR}"
)
install(FILES
    plugins/beacon_ui/icons/Copy.svg
    DESTINATION "${BEACON_UI_DIR}/icons"
)

# Patch RUNPATH so Qt libs resolve correctly when loaded from Basecamp AppImage
find_program(PATCHELF_EXECUTABLE patchelf)
if(PATCHELF_EXECUTABLE)
    install(CODE "
        set(_plugin \"${BEACON_MODULE_DIR}/logos_beacon_plugin.so\")
        set(_build_so \"${CMAKE_BINARY_DIR}/logos_beacon_plugin.so\")
        if(EXISTS \"\${_plugin}\")
            execute_process(
                COMMAND bash -c \"ldd '\${_build_so}' | grep nix/store | awk '{print \\\$3}' | xargs -I{} dirname {} | sort -u | tr '\\n' ':' | sed 's/:$//'\"
                OUTPUT_VARIABLE _nix_paths
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            set(_qt_lib \"\$ENV{HOME}/Qt/6.9.3/gcc_64/lib\")
            set(_rpath \"\\\$ORIGIN:\${_nix_paths}:\${_qt_lib}:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu\")
            execute_process(
                COMMAND ${PATCHELF_EXECUTABLE} --set-rpath \"\${_rpath}\" \"\${_plugin}\"
            )
            message(STATUS \"Patched RUNPATH on logos_beacon_plugin.so\")
        endif()
    ")
endif()

endif() # LOGOS_MODULE_BUILDER_ROOT
