cmake_minimum_required(VERSION 3.24)
project(testdrive3_enhanced VERSION 0.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Test Drive III Enhanced: the faithful port of TDIII.EXE (VGA mode 13h, AdLib sound; ENGINE.md) with the
# enhanced renderer in src/enhanced (ENHANCED.md).

find_package(SDL3 REQUIRED CONFIG)

# Nuked-OPL3 (LGPL-2.1), built as its own library and linked in; see third_party/nuked-opl3/README.md.
add_library(nuked_opl3 STATIC third_party/nuked-opl3/opl3.c)
target_include_directories(nuked_opl3 PUBLIC third_party/nuked-opl3)

file(GLOB_RECURSE TD_SOURCES CONFIGURE_DEPENDS src/*.c)
file(GLOB_RECURSE TD_ENHANCED_SOURCES CONFIGURE_DEPENDS src/enhanced/*.c)

add_executable(testdrive3-enhanced ${TD_SOURCES})
target_include_directories(testdrive3-enhanced PRIVATE src)
target_link_libraries(testdrive3-enhanced PRIVATE SDL3::SDL3 nuked_opl3)

if(UNIX)
    target_link_libraries(testdrive3-enhanced PRIVATE m)
endif()
if(MSVC)
    target_compile_options(testdrive3-enhanced PRIVATE /W3)
else()
    # The memory model reads multi-byte values through byte pointers (see src/mem.h).
    target_compile_options(testdrive3-enhanced PRIVATE -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing)
    # Game strings are copied out of mem[] into fixed buffers with snprintf; truncation is intended.
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(testdrive3-enhanced PRIVATE -Wno-format-truncation)
    endif()
    # The enhanced renderer does per-pixel work every frame; keep it optimised in Debug builds.
    if(TD_ENHANCED_SOURCES)
        set_source_files_properties(${TD_ENHANCED_SOURCES} PROPERTIES COMPILE_OPTIONS -O2)
    endif()
endif()
if(WIN32)
    add_custom_command(TARGET testdrive3-enhanced POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:testdrive3-enhanced> $<TARGET_FILE_DIR:testdrive3-enhanced>
        COMMAND_EXPAND_LISTS)
endif()

# The game's launcher ("Test Drive III Enhanced"): the game folder, the car, course and skill the game starts
# with, the game speed, the enhanced view's options, sound and display; built beside testdrive3-enhanced.exe.
# It needs wxWidgets 3.2 (MSYS2: mingw-w64-x86_64-wxwidgets3.2-msw) and a C++17 compiler.
option(TD3E_LAUNCHER "Also build the launcher, Test Drive III Enhanced.exe (wxWidgets)" ON)
if(TD3E_LAUNCHER)
    add_subdirectory(launcher)
endif()
