# C is required as well as C++: the vendored QR encoder
# (third_party/qrcodegen/qrcodegen.c) is C, and a CXX-only project has no
# CMAKE_C_COMPILE_OBJECT, so adding a .c source silently fails to configure.
cmake_minimum_required(VERSION 3.16)
project(CHROMAWOTD_layout_tests C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# GoogleTest setup. A pre-populated checkout can be reused to keep builds
# offline-friendly; override with -DCHROMAWOTD_GTEST_DIR=<path> (or set it to
# "auto" to always fetch). No hard dependency on any other project's tree.
include(FetchContent)
set(CHROMAWOTD_GTEST_DIR "$ENV{CHROMAWOTD_GTEST_DIR}" CACHE PATH
    "Local googletest source dir to reuse instead of fetching")
if(NOT CHROMAWOTD_GTEST_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/googletest")
    set(CHROMAWOTD_GTEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/googletest")
endif()

if(CHROMAWOTD_GTEST_DIR AND EXISTS "${CHROMAWOTD_GTEST_DIR}")
    message(STATUS "Using local googletest at ${CHROMAWOTD_GTEST_DIR}")
    FetchContent_Declare(
        googletest
        SOURCE_DIR "${CHROMAWOTD_GTEST_DIR}"
    )
else()
    message(STATUS "Fetching googletest v1.14.0 from GitHub")
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )
endif()
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Paths
set(CC_FW_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(CC_SRC_DIR   "${CC_FW_DIR}/src")
set(CC_INC_DIR   "${CC_FW_DIR}/include")
set(CC_HARN_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/harness")
set(CC_STB_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# Mock canvas library + host DisplayTarget backend + pure text modules
add_library(chroma_canvas STATIC
    ${CC_HARN_DIR}/canvas.cpp
    ${CC_SRC_DIR}/draw/target_canvas.cpp
    ${CC_SRC_DIR}/draw/weather_icon.cpp
    ${CC_SRC_DIR}/text/glyphs.cpp
    ${CC_SRC_DIR}/text/wrap.cpp
    ${CC_SRC_DIR}/text/date_format.cpp
    ${CC_SRC_DIR}/net/net.cpp
    ${CC_SRC_DIR}/net/net_host_curl.cpp
    ${CC_SRC_DIR}/net/portal.cpp
    ${CC_SRC_DIR}/net/wifi_qr.cpp
    # Vendored QR encoder (Nayuki, MIT) — C, no exceptions, no dynamic allocation
    # beyond the caller's buffers. Needed because the Wi-Fi QR payload contains a
    # per-boot random password, so the code cannot be pre-generated.
    ${CC_SRC_DIR}/third_party/qrcodegen/qrcodegen.c
    ${CC_SRC_DIR}/config/config.cpp
    ${CC_SRC_DIR}/config/config_active.cpp
    ${CC_SRC_DIR}/config/config_nvs_host.cpp
    ${CC_SRC_DIR}/config/tz_map.cpp
    ${CC_SRC_DIR}/sched/wake_schedule.cpp
    ${CC_SRC_DIR}/sched/content_policy.cpp
    ${CC_SRC_DIR}/sched/factory_reset.cpp
)
target_include_directories(chroma_canvas PUBLIC
    ${CC_HARN_DIR}
    ${CC_STB_DIR}
    ${CC_SRC_DIR}
)
target_compile_definitions(chroma_canvas PUBLIC CHROMAWOTD_HOST=1)

# Device font path. The firmware ships the proportional Roboto/FreeSans set
# (-DCHROMAWOTD_FONT_FREESANS=1 in platformio.ini), but the host targets all
# defined CHROMAWOTD_HOST only, so the auto-size ladder and the proportional
# glyph/degree/measurement path had NO local coverage — ctest, layout_render,
# regenerate_screenshots and the docs/images ledger all exercised the fallback
# 5x7 path. Turning this ON compiles the layout targets on the device font path.
# Default OFF keeps the classic 5x7 renders the docs/images ledger is built from;
# the layout invariants that encode 5x7 glyph geometry only hold in that mode
# (see LESSONS 38).
option(CHROMAWOTD_DEVICE_FONTS
       "Compile host layout targets on the device (FreeSans/Roboto) font path" OFF)
set(CC_FONT_DEFS "")
if(CHROMAWOTD_DEVICE_FONTS)
    set(CC_FONT_DEFS CHROMAWOTD_FONT_FREESANS=1)
    message(STATUS "Device font path ON: host layout targets get CHROMAWOTD_FONT_FREESANS=1")
endif()

# Helper function to register tests and copy MinGW runtime DLLs
file(TO_CMAKE_PATH "${CMAKE_CXX_COMPILER}" _compiler)
get_filename_component(_mingw_bin_dir "${_compiler}" DIRECTORY)
set(_rt_dlls
    "${_mingw_bin_dir}/libstdc++-6.dll"
    "${_mingw_bin_dir}/libgcc_s_seh-1.dll"
    "${_mingw_bin_dir}/libwinpthread-1.dll")

function(cc_test NAME SRC_TEST)
    add_executable(${NAME}
        ${CC_SRC_DIR}/verse_display.cpp
        ${SRC_TEST}
    )
    target_compile_definitions(${NAME} PRIVATE CHROMAWOTD_HOST=1 ${CC_FONT_DEFS})
    target_link_libraries(${NAME} PRIVATE chroma_canvas gtest_main)
    add_test(NAME ${NAME}
             COMMAND ${NAME}
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    if(WIN32 AND EXISTS "${_mingw_bin_dir}")
        add_custom_command(TARGET ${NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${_rt_dlls} $<TARGET_FILE_DIR:${NAME}>
            COMMENT "Copying MinGW runtime DLLs for ${NAME}")
    endif()
endfunction()

cc_test(test_layout_landscape tests/test_layout_landscape.cpp)
cc_test(test_layout_alert     tests/test_layout_alert.cpp)
cc_test(test_layout_overflow  tests/test_layout_overflow.cpp)

# Verse font auto-size ladder. The ladder itself is compiled only under
# CHROMAWOTD_FONT_FREESANS (the device build flag in platformio.ini), which the
# cc_test() targets above do NOT define — so this target adds the flag and is
# the only local coverage of the selection the panel actually performs.
add_executable(test_verse_autosize
    ${CC_SRC_DIR}/verse_display.cpp
    tests/test_verse_autosize.cpp
)
target_compile_definitions(test_verse_autosize PRIVATE
    CHROMAWOTD_HOST=1 CHROMAWOTD_FONT_FREESANS=1)
target_link_libraries(test_verse_autosize PRIVATE chroma_canvas gtest_main)
add_test(NAME test_verse_autosize
         COMMAND test_verse_autosize
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32 AND EXISTS "${_mingw_bin_dir}")
    add_custom_command(TARGET test_verse_autosize POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_rt_dlls} $<TARGET_FILE_DIR:test_verse_autosize>
        COMMENT "Copying MinGW runtime DLLs for test_verse_autosize")
endif()

# Pure text modules (text/glyphs, text/wrap) — no layout/backend dependency, so
# these are tested in isolation (REVIEW T1). chroma_canvas already compiles the
# two .cpp files.
add_executable(test_text tests/test_text.cpp)
target_compile_definitions(test_text PRIVATE CHROMAWOTD_HOST=1)
target_link_libraries(test_text PRIVATE chroma_canvas gtest_main)
add_test(NAME test_text COMMAND test_text WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32 AND EXISTS "${_mingw_bin_dir}")
    add_custom_command(TARGET test_text POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_rt_dlls} $<TARGET_FILE_DIR:test_text>
        COMMENT "Copying MinGW runtime DLLs for test_text")
endif()

# Network module (shared pure mapping + host curl fetch; live-fetch tests are
# skipped when offline).
cc_test(test_net tests/test_net.cpp)

# Device configuration: defaults, validation, coordinate parsing, the NVS
# round-trip and the portal's AP credential rule. These decide whether a device
# can connect at all, so they are asserted rather than eyeballed.
cc_test(test_config tests/test_config.cpp)

# Wi-Fi QR payload: the escaping rules, which decide whether a scan joins the right
# network with the right password.
cc_test(test_wifi_qr tests/test_wifi_qr.cpp)

# Wake schedule arithmetic (pure; no backend needed).
add_executable(test_sched tests/test_sched.cpp)
target_compile_definitions(test_sched PRIVATE CHROMAWOTD_HOST=1)
target_link_libraries(test_sched PRIVATE chroma_canvas gtest_main)
add_test(NAME test_sched COMMAND test_sched WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32 AND EXISTS "${_mingw_bin_dir}")
    add_custom_command(TARGET test_sched POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_rt_dlls} $<TARGET_FILE_DIR:test_sched>
        COMMENT "Copying MinGW runtime DLLs for test_sched")
endif()

# Preview renderer: render an arbitrary fixture through the real layout engine.
# Driven by tools/render_preview.py.
add_executable(layout_render
    ${CC_SRC_DIR}/verse_display.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/layout_render.cpp
)
target_compile_definitions(layout_render PRIVATE CHROMAWOTD_HOST=1 ${CC_FONT_DEFS})
target_link_libraries(layout_render PRIVATE chroma_canvas)
if(WIN32 AND EXISTS "${_mingw_bin_dir}")
    add_custom_command(TARGET layout_render POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_rt_dlls} $<TARGET_FILE_DIR:layout_render>
        COMMENT "Copying MinGW runtime DLLs for layout_render")
endif()

# Ensure output directory exists
add_custom_command(TARGET test_layout_landscape POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/output)
