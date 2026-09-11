cmake_minimum_required(VERSION 3.16)
project(CHROMAWOTD_layout_tests CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# GoogleTest setup
include(FetchContent)
if(EXISTS "C:/Users/jptra/Projects/eClock/firmware/test/build/_deps/googletest-src")
    FetchContent_Declare(
        googletest
        SOURCE_DIR "C:/Users/jptra/Projects/eClock/firmware/test/build/_deps/googletest-src"
    )
else()
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

# Mock canvas library
add_library(chroma_canvas STATIC
    ${CC_HARN_DIR}/canvas.cpp
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

cc_test(test_layout_portrait  tests/test_layout_portrait.cpp)
cc_test(test_layout_landscape tests/test_layout_landscape.cpp)
cc_test(test_layout_alert     tests/test_layout_alert.cpp)

# Ensure output directory exists
add_custom_command(TARGET test_layout_portrait POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/output)

