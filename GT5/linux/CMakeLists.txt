# Gran Turismo 5 recomp: the Linux (x86-64) build.
#
# A separate project from GT5/CMakeLists.txt (the Windows build), so nothing here can change the
# Windows exe. It builds the same sources -- the lifted PPU code, the lifted SPU images, the boot
# harness and the ps3recomp runtime -- into one program, GT5/build-linux/gt5, which play.sh starts.
#
# Rendering: the runtime's Vulkan backend and its register-file draw engine, the same renderer as
# the Android port (the Direct3D 12 renderer the Windows build uses is Windows-only). Window,
# keyboard, controllers and sound: SDL2. Movies and music: the system's FFmpeg, loaded at run time.
#
# setup.sh configures and builds this; by hand:
#   cmake -S GT5/linux -B GT5/build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
#   cmake --build GT5/build-linux
cmake_minimum_required(VERSION 3.20)
project(gt5_linux LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
    message(FATAL_ERROR "GT5/linux is the Linux build. Windows: GT5/CMakeLists.txt (setup.ps1). Android: GT5/android.")
endif()
if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "The game code needs Clang (the SPU code's tail calls are a Clang feature). "
        "Install clang and configure with -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ (setup.sh does).")
endif()
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    message(FATAL_ERROR "Only x86-64 Linux is supported so far (this is ${CMAKE_SYSTEM_PROCESSOR}).")
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(GT5_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(PS3RECOMP_DIR "${GT5_DIR}/../ps3recomp" CACHE PATH "ps3recomp checkout")
get_filename_component(PS3RECOMP_DIR "${PS3RECOMP_DIR}" ABSOLUTE)
set(RECOMP_DIR "${GT5_DIR}/generated")
set(SPU_DIR "${GT5_DIR}/spu")

if(NOT EXISTS "${RECOMP_DIR}/ppu_recomp.h")
    message(FATAL_ERROR "No ppu_recomp.h in ${RECOMP_DIR}. Run setup.sh first: it generates the game code from your EBOOT.elf.")
endif()

find_package(SDL2 REQUIRED)

# The renderer: Vulkan. libvulkan is loaded at run time, so only the headers are needed here
# (Debian/Ubuntu: libvulkan-dev). glslang turns the game's shaders into SPIR-V (glslang-dev).
set(PS3RECOMP_RSX_VULKAN ON CACHE BOOL "" FORCE)
find_package(glslang CONFIG QUIET)
if(NOT glslang_FOUND)
    message(FATAL_ERROR "glslang not found: it turns the game's shaders into Vulkan's. "
        "Debian/Ubuntu/Mint: sudo apt install glslang-dev glslang-tools. Fedora: glslang-devel. Arch: glslang.")
endif()

# FFmpeg (movies, and the ATRAC3plus music some loading screens wait for), loaded at run time from
# the system's libavcodec. Its headers decide which libavcodec.so.N the game opens. 6.1 or newer.
find_path(GT5_FFMPEG_INCLUDE_DIR libavcodec/avcodec.h PATH_SUFFIXES ffmpeg)
if(NOT GT5_FFMPEG_INCLUDE_DIR)
    message(FATAL_ERROR "FFmpeg's headers were not found. Debian/Ubuntu/Mint: sudo apt install libavcodec-dev. "
        "Fedora: ffmpeg-free-devel (or ffmpeg-devel from RPM Fusion). Arch: ffmpeg.")
endif()
include(CheckSymbolExists)
set(CMAKE_REQUIRED_INCLUDES "${GT5_FFMPEG_INCLUDE_DIR}")
check_symbol_exists(AV_FRAME_FLAG_KEY "libavutil/frame.h" GT5_FFMPEG_61)
unset(CMAKE_REQUIRED_INCLUDES)
if(NOT GT5_FFMPEG_61)
    message(FATAL_ERROR "FFmpeg in ${GT5_FFMPEG_INCLUDE_DIR} is older than 6.1. The game needs 6.1 or newer "
        "(Ubuntu 24.04 / Mint 22 / Debian 13 and later have it).")
endif()
set(PS3RECOMP_FFMPEG_RUNTIME ON CACHE BOOL "" FORCE)
set(PS3RECOMP_FFMPEG_INCLUDE_DIR "${GT5_FFMPEG_INCLUDE_DIR}" CACHE PATH "" FORCE)

add_subdirectory("${PS3RECOMP_DIR}" "${CMAKE_BINARY_DIR}/ps3recomp_sdk" EXCLUDE_FROM_ALL)

file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS "${RECOMP_DIR}/*.cpp")
list(LENGTH RECOMP_SOURCES RECOMP_SOURCE_COUNT)
message(STATUS "Lifted sources:  ${RECOMP_SOURCE_COUNT} file(s)")

set(HLE_NID_TABLE "${GT5_DIR}/src/gen/ppu_hle_nids.cpp")
set(PS3RECOMP_SCAFFOLD
    "${PS3RECOMP_DIR}/runtime/ppu/tests/boot_main.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_loader.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_hle.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_fs.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_sysprx.cpp"
)

set(SPU_SOURCES "")
if(EXISTS "${SPU_DIR}/sources.cmake")
    include("${SPU_DIR}/sources.cmake")
    list(APPEND SPU_SOURCES ${SPU_GENERATED_SOURCES} "${PS3RECOMP_DIR}/runtime/spu/spu_dispatch_mt.c")
endif()
file(GLOB SPU_JOB_SOURCES CONFIGURE_DEPENDS "${SPU_DIR}/jobs/*/spu_recomp.c")
if(SPU_JOB_SOURCES)
    list(APPEND SPU_SOURCES ${SPU_JOB_SOURCES} "${GT5_DIR}/src/gen/spu_jobs.c")
endif()

add_executable(gt5
    "${GT5_DIR}/src/gt5_longjmp.cpp"
    "${GT5_DIR}/src/gt5_debug.cpp"
    "${GT5_DIR}/src/gt5_widescreen.cpp"
    ${PS3RECOMP_SCAFFOLD}
    ${HLE_NID_TABLE}
    ${RECOMP_SOURCES}
    ${SPU_SOURCES}
)
file(GLOB GT5_MOD_SOURCES CONFIGURE_DEPENDS "${GT5_DIR}/src/gen/mods.cpp")
if(GT5_MOD_SOURCES)
    target_sources(gt5 PRIVATE ${GT5_MOD_SOURCES})
endif()

target_include_directories(gt5 PRIVATE
    "${RECOMP_DIR}"
    "${PS3RECOMP_DIR}/include"
    "${PS3RECOMP_DIR}/runtime/ppu"
    "${PS3RECOMP_DIR}/runtime/spu"
    "${PS3RECOMP_DIR}/runtime/memory"
    "${PS3RECOMP_DIR}/runtime/syscalls"
    "${PS3RECOMP_DIR}/runtime/platform"
    "${PS3RECOMP_DIR}/libs/video"
    "${GT5_DIR}"
)
target_compile_options(gt5 PRIVATE -fno-strict-aliasing -fwrapv -Wno-everything)

# The same code generation as the Windows build, so both run the game the same way: the lifted PPU
# code at -O2 with the instructions every AVX2 CPU has (MOVBE folds the byte swap of each big-endian
# guest access into the access), SPU code with AVX2/FMA and the PS3's round-toward-zero floats. No
# FMA contraction anywhere: floating-point results match the Windows build's.
set_source_files_properties(${RECOMP_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-O2;-w;-mavx2;-mbmi;-mbmi2;-mmovbe;-mlzcnt;-mpopcnt;-ffp-contract=off")
if(SPU_SOURCES)
    set_source_files_properties(${SPU_SOURCES} PROPERTIES
        INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu"
        COMPILE_DEFINITIONS SPU_USE_DISPATCH_MT
        COMPILE_OPTIONS "-O2;-w;-mavx2;-mfma;-ffp-contract=off;-DSPU_X86_RZ_ALWAYS=1")
endif()
# GT5's sound job: its numbers are samples, and it needs every bit of speed to keep up. jD58063CD is
# the disc's; game update 2.17 replaces it with jCB9BD024 (spu_jobs_from_eboot.py, UPDATED).
foreach(_sound_job jD58063CD jCB9BD024)
    if(EXISTS "${SPU_DIR}/jobs/${_sound_job}/spu_recomp.c")
        set_source_files_properties("${SPU_DIR}/jobs/${_sound_job}/spu_recomp.c" PROPERTIES
            COMPILE_OPTIONS "-O2;-w;-mavx2;-mfma;-ffp-contract=off;-DSPU_X86_RZ_ALWAYS=1;-DSPU_X86_FAST_FLOAT=1")
    endif()
endforeach()

target_link_libraries(gt5 PRIVATE ps3recomp_runtime SDL2::SDL2 ${CMAKE_DL_LIBS} m pthread)
set_target_properties(gt5 PROPERTIES LINKER_LANGUAGE CXX)
