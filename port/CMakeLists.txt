# Native build of Seven Kingdoms: Ancient Adversaries 2.15.7 for Android (arm64-v8a).
#
# Driven by Gradle (android/app/build.gradle externalNativeBuild). Game sources are NOT read from
# third_party/7kaa directly: scripts/apply-patches.sh copies them to build/7kaa-src and applies
# patches/7kaa/*.patch (CONTRACT 3.1).
cmake_minimum_required(VERSION 3.22)
project(SevenKingdomsAndroid C CXX)

get_filename_component(SK_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(SK_TP  "${SK_ROOT}/third_party")
set(SK_SRC "${SK_ROOT}/build/7kaa-src")

if(NOT EXISTS "${SK_SRC}/src/AM.cpp")
    message(FATAL_ERROR "build/7kaa-src is missing. Run scripts/apply-patches.sh (scripts/build.sh does it).")
endif()
foreach(_d SDL2 openal-soft enet)
    if(NOT EXISTS "${SK_TP}/${_d}/CMakeLists.txt")
        message(FATAL_ERROR "third_party/${_d} is missing. Run scripts/fetch-deps.sh.")
    endif()
endforeach()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# The game is a software renderer and the audio mixer runs in the main loop: even the debug APK
# must be optimised, so Debug builds use -O2 -g instead of CMake's default -O0.
set(CMAKE_C_FLAGS_DEBUG   "-O2 -g")
set(CMAKE_CXX_FLAGS_DEBUG "-O2 -g")

# ---------------------------------------------------------------------------
# SDL2: shared library (libSDL2.so), no tests, no static variant.
# ---------------------------------------------------------------------------
set(SDL_SHARED ON  CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
add_subdirectory("${SK_TP}/SDL2" SDL2)

# ---------------------------------------------------------------------------
# openal-soft: shared library (libopenal.so), OpenSL ES backend only.
# ---------------------------------------------------------------------------
set(LIBTYPE SHARED CACHE STRING "" FORCE)
set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_NO_CONFIG_UTIL ON CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_CONFIG OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_HRTF_DATA OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_AMBDEC_PRESETS OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_UPDATE_BUILD_VERSION OFF CACHE BOOL "" FORCE)
set(ALSOFT_ENABLE_MODULES OFF CACHE BOOL "" FORCE)
set(ALSOFT_RTKIT OFF CACHE BOOL "" FORCE)
set(ALSOFT_BACKEND_OPENSL ON CACHE BOOL "" FORCE)
set(ALSOFT_REQUIRE_OPENSL ON CACHE BOOL "" FORCE)
foreach(_b OBOE PIPEWIRE PULSEAUDIO ALSA OSS SOLARIS SNDIO JACK PORTAUDIO COREAUDIO WAVE SDL3 SDL2)
    set(ALSOFT_BACKEND_${_b} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${SK_TP}/openal-soft" openal-soft)

# ---------------------------------------------------------------------------
# enet: static library. The game compiles its multiplayer wrapper against stubs
# (USE_ENET undefined, multiplayer is out of scope), so nothing links it yet.
# ---------------------------------------------------------------------------
add_subdirectory("${SK_TP}/enet" enet)

# ---------------------------------------------------------------------------
# The game. Output must be libmain.so: SDLActivity loads it and calls SDL_main.
# ---------------------------------------------------------------------------
include("${CMAKE_CURRENT_LIST_DIR}/sources.cmake")

set(_game_sources)
foreach(_s ${SK_GAME_SOURCES} ${SK_IMGFUN_SOURCES} ${SK_OPENAL_SOURCES} ${SK_MULTIPLAYER_SOURCES})
    list(APPEND _game_sources "${SK_SRC}/src/${_s}")
endforeach()

set(PORT_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/android/sk_android_data.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/android/sk_android_video.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/android/sk_glob.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/android/sk_touch.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/android/sk_lifecycle.cpp"
)

add_library(main SHARED ${_game_sources} ${PORT_SOURCES})

target_include_directories(main PRIVATE
    "${SK_SRC}/include"
    "${CMAKE_CURRENT_LIST_DIR}/android"
)

# Defines replicate the Linux autotools build (docs/AUDIT.md section I3) explicitly.
# Deliberately NOT defined: USE_ASM, HAVE_KNOWN_BUILD, USE_ENET, HAVE_LIBCURL, ENABLE_NLS,
# USE_WINDOWS, USE_FHS, ENABLE_INTRO_VIDEO.
target_compile_definitions(main PRIVATE
    USE_SDL
    USE_POSIX
    USE_OPENAL
    NO_MEM_CLASS
    DISABLE_MULTI_PLAYER
    REGISTER=
    HAVE_SETENV
    HAVE_STRINGS_STRCASECMP
    "OPENAL_AL_H=<AL/al.h>"
    "OPENAL_ALC_H=<AL/alc.h>"
    "PACKAGE=\"7kaa\""
    "PACKAGE_NAME=\"7 Kingdoms: Ancient Adversaries\""
    "PACKAGE_VERSION=\"2.15.7\""
    "VERSION=\"2.15.7\""
)

# -fsigned-char is mandatory (ARM char is unsigned; the game assumes signed).
# Upstream's CXX standard is whatever the compiler defaults to for C++11 (AX_CXX_COMPILE_STDCXX_11).
target_compile_options(main PRIVATE
    -fsigned-char
)
set_target_properties(main PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON
)
target_link_libraries(main PRIVATE SDL2 OpenAL log android)
add_dependencies(main enet)  # build the (currently unlinked) static enet too

# Upstream sources are 1997-era code and very noisy: silence them. Warnings stay on for port/.
set_source_files_properties(${_game_sources} PROPERTIES COMPILE_OPTIONS "-w")
set_source_files_properties(${PORT_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
