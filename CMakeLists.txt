cmake_minimum_required(VERSION 3.28)
project(beacon-basecamp VERSION 1.0.0 LANGUAGES CXX)

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
add_library(beacon_plugin MODULE
    src/plugin/BeaconPlugin.h
    src/plugin/BeaconPlugin.cpp
)

set_target_properties(beacon_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "beacon_plugin"
)

target_include_directories(beacon_plugin PRIVATE
    src
    "${LOGOS_LIBLOGOS_HEADERS}"
    "${LOGOS_CPP_SDK}/include"
    "${LOGOS_CPP_SDK}/include/cpp"
    "${LOGOS_CPP_SDK}/include/core"
)

# whole-archive: keeps ModuleProxy::informModuleToken from being stripped
# (whole-archive-module-proxy-strip skill)
target_link_libraries(beacon_plugin PRIVATE
    Qt6::Core
    Qt6::Qml
    "${LOGOS_CPP_SDK}/lib/liblogos_sdk.a"
    dl pthread
)
# Note: Qt6RemoteObjects symbols (QRemoteObjectNode etc.) are pulled in by liblogos_sdk.a.
# They resolve at runtime from the AppImage's already-loaded libQt6RemoteObjects (RTLD_GLOBAL).
# Do NOT add --whole-archive: that pulls in QRemoteObjectRegistryHost(QUrl,QObject*) which
# was removed in Qt 6.9.3 (renamed to private API), causing load failure outside the AppImage.

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
set(LOGOS_APP_DATA "$ENV{HOME}/.local/share/Logos/LogosApp")
set(BEACON_MODULE_DIR "${LOGOS_APP_DATA}/modules/logos_beacon")
set(BEACON_UI_DIR     "${LOGOS_APP_DATA}/plugins/beacon_ui")

# Clean old module dir on reinstall
install(CODE "
    set(_modules_dir \"\\$ENV{DESTDIR}${LOGOS_APP_DATA}/modules\")
    file(GLOB _old \"\${_modules_dir}/logos_beacon*\")
    foreach(_dir \${_old})
        file(REMOVE_RECURSE \"\${_dir}\")
    endforeach()
")

install(TARGETS beacon_plugin LIBRARY DESTINATION "${BEACON_MODULE_DIR}")
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
    assets/icons/Beacon_sidebar.png
    DESTINATION "${BEACON_UI_DIR}/icons"
)

# Patch RUNPATH so Qt libs resolve correctly when loaded from AppImage
find_program(PATCHELF_EXECUTABLE patchelf)
if(PATCHELF_EXECUTABLE)
    install(CODE "
        set(_plugin \"${BEACON_MODULE_DIR}/beacon_plugin.so\")
        set(_build_so \"${CMAKE_BINARY_DIR}/beacon_plugin.so\")
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
            message(STATUS \"Patched RUNPATH on beacon_plugin.so\")
        endif()
    ")
endif()

# Mirror to LogosBasecamp
install(CODE "
    set(_mod_src \"${BEACON_MODULE_DIR}\")
    set(_mod_dst \"\$ENV{HOME}/.local/share/Logos/LogosBasecamp/modules/logos_beacon\")
    file(COPY \"\${_mod_src}/\" DESTINATION \"\${_mod_dst}\")

    set(_ui_src \"${BEACON_UI_DIR}\")
    set(_ui_dst \"\$ENV{HOME}/.local/share/Logos/LogosBasecamp/plugins/beacon_ui\")
    file(GLOB _ui_files \"\${_ui_src}/*\")
    foreach(_f \${_ui_files})
        file(COPY \"\${_f}\" DESTINATION \"\${_ui_dst}\")
    endforeach()
    message(STATUS \"Mirror to LogosBasecamp complete\")
")
