cmake_minimum_required(VERSION 3.20)

# Classic flag-based MSVC runtime selection (CMP0091 OLD) so the static CRT can
# be forced reliably under direct clang-cl + Ninja. Must be set before project().
if (WIN32)
    cmake_policy(SET CMP0091 OLD)
endif()

project(PocketMonstersStadiumRecomp C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-everything /W4")
endif()
if (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR")
endif()
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy(SET CMP0135 NEW)
endif()

# Statically link the MSVC CRT so the shipped exe needs no VC++ redistributable.
# CMAKE_MSVC_RUNTIME_LIBRARY (CMP0091 NEW) is not reliably honored for direct
# clang-cl + Ninja, so rewrite the dynamic-CRT flag (-MD) to static (/MT) in
# every flag variable (deps inherit these, so every target links the static CRT).
if (WIN32)
    foreach(flag_var
            CMAKE_C_FLAGS   CMAKE_C_FLAGS_DEBUG   CMAKE_C_FLAGS_RELEASE
            CMAKE_C_FLAGS_RELWITHDEBINFO   CMAKE_C_FLAGS_MINSIZEREL
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
        string(REGEX REPLACE "[-/]MDd" "/MTd" ${flag_var} "${${flag_var}}")
        string(REGEX REPLACE "[-/]MD"  "/MT"  ${flag_var} "${${flag_var}}")
    endforeach()
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Release/windowed build: no console window. Off by default (console is handy
# for dev — the boot log goes to stderr). Configure -DPMS_NO_CONSOLE=ON to ship.
option(PMS_NO_CONSOLE "Build a windowed release with no console window" OFF)

# ---- ROM hash check (advisory) ---------------------------------------------
set(EXPECTED_BASEROM_MD5 "c46e087d966a35095df96799b0b4ffae")
if(EXISTS "${CMAKE_SOURCE_DIR}/baserom.z64")
    file(MD5 "${CMAKE_SOURCE_DIR}/baserom.z64" ACTUAL_BASEROM_MD5)
    if(NOT ACTUAL_BASEROM_MD5 STREQUAL EXPECTED_BASEROM_MD5)
        message(WARNING "baserom.z64 MD5 mismatch.\n  expected: ${EXPECTED_BASEROM_MD5}\n  actual:   ${ACTUAL_BASEROM_MD5}")
    else()
        message(STATUS "baserom.z64 MD5 OK (Pocket Monsters Stadium J)")
    endif()
endif()

# ---- RT64 (graphics renderer) ----------------------------------------------
set(RT64_STATIC TRUE)
set(RT64_SDL_WINDOW_VULKAN TRUE)
add_compile_definitions(HLSL_CPU)
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/rt64 ${CMAKE_BINARY_DIR}/rt64)

# ---- N64ModernRuntime (junctioned at lib/N64ModernRuntime) -----------------
# Transitively adds N64Recomp (+ ares-bridge placeholder) and ultramodern.
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime)

target_include_directories(rt64 PRIVATE ${CMAKE_BINARY_DIR}/rt64/src)

# ---- RecompiledFuncs library (the recompiled game C) -----------------------
add_library(RecompiledFuncs STATIC)
target_compile_options(RecompiledFuncs PRIVATE
    -fno-strict-aliasing
    -Wno-unused-variable
    -Wno-implicit-function-declaration
)
target_include_directories(RecompiledFuncs PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
)
file(GLOB FUNC_C_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/generated/*.c)
target_sources(RecompiledFuncs PRIVATE ${FUNC_C_SOURCES})

# ---- Runner executable ------------------------------------------------------
add_executable(PocketMonstersStadiumRecomp
    src/main/main.cpp
    src/main/rt64_render_context.cpp
    src/main/register_overlays.cpp
    src/main/diagnostics.cpp
    src/main/rsp_aspmain_hook.cpp
    src/main/debug_server.cpp
    # Generated lookup table (provides get_entrypoint_address etc).
    generated/lookup.cpp
    # Pocket Monsters Stadium's aspMain, recompiled from the Japan ROM.
    ${CMAKE_SOURCE_DIR}/rsp/aspMain.cpp
    # Standard libultra JPEG ucode, shared with Pokemon Stadium US.
    ${CMAKE_SOURCE_DIR}/rsp/njpgdspMain.cpp
)

target_include_directories(PocketMonstersStadiumRecomp PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/main
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/ares-bridge/include
    ${CMAKE_SOURCE_DIR}/lib/concurrentqueue
    ${CMAKE_SOURCE_DIR}/lib/SlotMap
    ${CMAKE_SOURCE_DIR}/lib/rt64/src
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/hlslpp/include
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/rhi
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/render
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_options(PocketMonstersStadiumRecomp PRIVATE
    -fno-strict-aliasing
    -fms-extensions
    -march=nehalem  # Required by RT64's RSP simulator (SSSE3, SSE4.1, SSE4.2)
)
target_compile_definitions(PocketMonstersStadiumRecomp PRIVATE NOMINMAX)

# Emit PDBs (Release too) so the SEH crash filter's stack walk resolves real
# symbol names. clang-cl needs /Z7 + /DEBUG; /OPT:REF strips dead code while
# /OPT:NOICF stops COMDAT folding from corrupting the stack-walk symbol map.
if (MSVC)
    target_compile_options(PocketMonstersStadiumRecomp PRIVATE /Z7)
    target_compile_options(RecompiledFuncs             PRIVATE /Z7)
    # Generated one-function C files can be enormous after raw fragment
    # discovery. Keep runtime/renderer Release, but avoid /O2 pathologies in
    # the recompiled MIPS translation units during bring-up.
    target_compile_options(RecompiledFuncs             PRIVATE /Od /Ob0)
    target_link_options(PocketMonstersStadiumRecomp PRIVATE /DEBUG /OPT:REF /OPT:NOICF)
    if (TARGET librecomp)
        target_compile_options(librecomp PRIVATE /Z7)
    endif()
    if (TARGET ultramodern)
        target_compile_options(ultramodern PRIVATE /Z7)
    endif()
endif()

# ---- SDL2 (fetched on Windows) ---------------------------------------------
if (WIN32)
    include(FetchContent)
    if (DEFINED ENV{SDL2_VERSION})
        set(SDL2_VERSION $ENV{SDL2_VERSION})
    else()
        set(SDL2_VERSION "2.30.3")
    endif()
    FetchContent_Declare(
        sdl2
        URL https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-VC.zip
    )
    FetchContent_MakeAvailable(sdl2)
    target_include_directories(PocketMonstersStadiumRecomp PRIVATE ${sdl2_SOURCE_DIR}/include)
    target_link_directories(PocketMonstersStadiumRecomp PRIVATE ${sdl2_SOURCE_DIR}/lib/x64)

    add_custom_command(TARGET PocketMonstersStadiumRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${sdl2_SOURCE_DIR}/lib/x64/SDL2.dll"
            "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxil.dll"
            "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxcompiler.dll"
            $<TARGET_FILE_DIR:PocketMonstersStadiumRecomp>
    )

    target_link_libraries(PocketMonstersStadiumRecomp PRIVATE SDL2 SDL2main Winmm.lib dbghelp.lib comdlg32.lib ws2_32.lib)
endif()

target_link_libraries(PocketMonstersStadiumRecomp PRIVATE
    RecompiledFuncs
    librecomp
    ultramodern
    rt64
    ares_bridge
)

# Windowed (no-console) release. SDL2main supplies WinMain for the WINDOWS
# subsystem, which calls our SDL_main, so no /ENTRY override is needed.
if (WIN32 AND PMS_NO_CONSOLE)
    target_link_options(PocketMonstersStadiumRecomp PRIVATE /SUBSYSTEM:WINDOWS)
endif()
