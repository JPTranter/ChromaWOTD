cmake_minimum_required(VERSION 3.16)
project(CHROMAWOTD_layout_tests CXX)

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
)
target_include_directories(chroma_canvas PUBLIC
    ${CC_HARN_DIR}
    ${CC_STB_DIR}
    ${CC_SRC_DIR}
)
target_compile_definitions(chroma_canvas PUBLIC CHROMAWOTD_HOST=1)

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
    target_compile_definitions(${NAME} PRIVATE CHROMAWOTD_HOST=1)
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

# Preview renderer: render an arbitrary fixture through the real layout engine.
# Driven by tools/render_preview.py.
add_executable(layout_render
    ${CC_SRC_DIR}/verse_display.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/layout_render.cpp
)
target_compile_definitions(layout_render PRIVATE CHROMAWOTD_HOST=1)
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

