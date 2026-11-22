cmake_minimum_required(VERSION 3.16)
project(ZPstudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)

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
