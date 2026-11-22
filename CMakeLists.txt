cmake_minimum_required(VERSION 3.16)
project(ZPstudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)

# The emulator runs the console's master clock cycle by cycle: an unoptimised build is several times slower than real time.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---- the emulator, built into Studio so the game plays inside the window ----------------------------------------
# Compiled from a ZeroPoint checkout (its source list is read from that checkout's CMakeLists, so new emulator files
# are picked up without touching this file). Without the checkout, or without SDL2 (audio), Studio still works: Run
# then opens the game in the external zeropoint_sdl window instead.
set(ZPSTUDIO_EMULATOR_DIR "${CMAKE_SOURCE_DIR}/../ZeroPoint" CACHE PATH "ZeroPoint emulator checkout to embed")
option(ZPSTUDIO_EMBED_EMULATOR "Build the emulator into Studio" ON)
set(ZPSTUDIO_HAVE_EMULATOR OFF)

if(ZPSTUDIO_EMBED_EMULATOR)
    set(_emu "${ZPSTUDIO_EMULATOR_DIR}")
    set(_why "")
    if(NOT EXISTS "${_emu}/qt/emulatorwidget.cpp" OR NOT EXISTS "${_emu}/CMakeLists.txt")
        set(_why "no ZeroPoint checkout at ${_emu} (set -DZPSTUDIO_EMULATOR_DIR=...)")
    endif()

    if(NOT _why)
        file(READ "${_emu}/CMakeLists.txt" _emu_cmake)
        string(REGEX MATCH "set\\(CORE_SOURCES([^)]*)\\)" _found "${_emu_cmake}")
        if(NOT _found)
            set(_why "could not read CORE_SOURCES from ${_emu}/CMakeLists.txt")
        else()
            string(STRIP "${CMAKE_MATCH_1}" _list)
            string(REGEX REPLACE "[ \t\r\n]+" ";" _list "${_list}")
            set(_emu_sources "")
            foreach(f IN LISTS _list)
                if(NOT EXISTS "${_emu}/${f}")
                    set(_why "the emulator source list names ${f}, which does not exist")
                endif()
                list(APPEND _emu_sources "${_emu}/${f}")
            endforeach()
        endif()
    endif()

    if(NOT _why)
        find_package(SDL2 CONFIG QUIET)
        if(TARGET SDL2::SDL2)
            set(_sdl SDL2::SDL2)
        else()
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(SDL2PC QUIET IMPORTED_TARGET sdl2)
            endif()
            if(TARGET PkgConfig::SDL2PC)
                set(_sdl PkgConfig::SDL2PC)
            else()
                set(_why "SDL2 (used for the audio) was not found")
            endif()
        endif()
    endif()

    if(_why)
        message(STATUS "Emulator NOT embedded: ${_why}. Run will use the external zeropoint_sdl window.")
    else()
        find_package(Threads REQUIRED)
        add_library(zpstudio_emu STATIC
            ${_emu_sources}
            "${_emu}/src/serial_host.cpp"
            "${_emu}/qt/emulatorwidget.cpp"
            "${_emu}/qt/audiooutput.cpp")
        target_include_directories(zpstudio_emu PUBLIC "${_emu}/include" "${_emu}/qt")
        target_link_libraries(zpstudio_emu PUBLIC Qt6::Widgets ${_sdl} Threads::Threads)
        if(WIN32)
            target_link_libraries(zpstudio_emu PUBLIC ws2_32)
        endif()
        set_target_properties(zpstudio_emu PROPERTIES CXX_EXTENSIONS ON)    # the emulator is written against gnu++17
        # The master clock crosses translation units every cycle; LTO lets those calls inline (the emulator does the same).
        if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
            include(CheckIPOSupported)
            check_ipo_supported(RESULT _ipo OUTPUT _ipo_err)
            if(_ipo)
                set_property(TARGET zpstudio_emu PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
            endif()
        endif()
        if(MSVC)
            target_compile_options(zpstudio_emu PRIVATE /w)      # someone else's code: do not drown our warnings
        else()
            target_compile_options(zpstudio_emu PRIVATE -w)
        endif()
        set(ZPSTUDIO_HAVE_EMULATOR ON)
        message(STATUS "Emulator embedded from ${_emu}")
    endif()
endif()

# ---- core: everything the Studio knows how to do, with no window (also what the CLI uses) ----
add_library(zpstudio_core STATIC
    src/core/toolchain.cpp
    src/core/projectconfig.cpp
    src/core/diagnostics.cpp
    src/core/linemap.cpp
    src/core/buildplan.cpp
    src/core/buildrunner.cpp
    src/core/assetmanifest.cpp
    src/core/assetanalysis.cpp
    src/core/projectcreator.cpp
    src/core/emulatorlauncher.cpp
)
target_include_directories(zpstudio_core PUBLIC src/core)
target_link_libraries(zpstudio_core PUBLIC Qt6::Core Qt6::Gui)
if(MSVC)
    target_compile_options(zpstudio_core PRIVATE /W4)
else()
    target_compile_options(zpstudio_core PRIVATE -Wall -Wextra)
endif()

# ---- headless command line ----
add_executable(zpstudio-cli src/cli/main.cpp)
target_link_libraries(zpstudio-cli PRIVATE zpstudio_core)

# ---- the window ----
file(GLOB APP_SOURCES CONFIGURE_DEPENDS src/app/*.cpp src/app/*.h)
list(FILTER APP_SOURCES EXCLUDE REGEX "src/app/main\\.cpp$")
add_library(zpstudio_app STATIC ${APP_SOURCES})     # everything but main(), so tests can drive the real window
target_link_libraries(zpstudio_app PUBLIC zpstudio_core Qt6::Widgets)
target_include_directories(zpstudio_app PUBLIC src/app)
if(ZPSTUDIO_HAVE_EMULATOR)
    target_link_libraries(zpstudio_app PUBLIC zpstudio_emu)
    target_compile_definitions(zpstudio_app PUBLIC ZPSTUDIO_HAVE_EMULATOR=1)
endif()
add_executable(zpstudio WIN32 MACOSX_BUNDLE src/app/main.cpp)
target_link_libraries(zpstudio PRIVATE zpstudio_app)

# ---- tests ----
enable_testing()
foreach(t diagnostics linemap buildplan assets project)
    add_executable(tst_${t} tests/tst_${t}.cpp)
    target_link_libraries(tst_${t} PRIVATE zpstudio_core Qt6::Test)
    add_test(NAME ${t} COMMAND tst_${t})
    set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# Drives the real window end to end; skips itself when ZPdevtools is not checked out next to this repo.
add_executable(tst_gui tests/tst_gui.cpp)
target_link_libraries(tst_gui PRIVATE zpstudio_app Qt6::Test)
target_compile_definitions(tst_gui PRIVATE ZPSTUDIO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME gui COMMAND tst_gui)
set_tests_properties(gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
