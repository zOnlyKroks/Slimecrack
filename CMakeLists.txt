cmake_minimum_required(VERSION 3.16)
project(slimecrack C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# slimecrack: `seed` recovers the world seed, `salt` recovers the scrambler (spigot.yml seed-slime)
# -march=native is fastest locally but makes binaries that crash on other CPUs; CI turns it off.
option(SLIMECRACK_NATIVE "Tune for this machine's CPU" ON)
find_package(OpenMP)
add_executable(slimecrack
    src/slimecrack.c src/slime.c src/chunks.c src/crack.c src/render.c)
if(OpenMP_C_FOUND)
    target_link_libraries(slimecrack PRIVATE OpenMP::OpenMP_C)
endif()
if(MSVC)
    target_compile_options(slimecrack PRIVATE /O2)
else()
    target_compile_options(slimecrack PRIVATE -O3)
    if(SLIMECRACK_NATIVE)
        target_compile_options(slimecrack PRIVATE -march=native)
    endif()
endif()

# slimefinder: desktop slime-chunk map (world seed + scrambler). Off by default so CI needs no GUI library.
# Uses a system raylib if found; set -DSLIMECRACK_FETCH_RAYLIB=ON to download and build it instead.
option(SLIMECRACK_GUI "Build the slimefinder desktop app" OFF)
option(SLIMECRACK_FETCH_RAYLIB "Download raylib if it is not installed" OFF)
if(SLIMECRACK_GUI)
    find_package(raylib QUIET)
    if(NOT raylib_FOUND AND SLIMECRACK_FETCH_RAYLIB)
        include(FetchContent)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(raylib
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG 5.5
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(raylib)
    elseif(NOT raylib_FOUND)
        message(FATAL_ERROR "raylib not found: install it (brew install raylib) or pass -DSLIMECRACK_FETCH_RAYLIB=ON")
    endif()
    # render.c is left out on purpose: finder.c supplies its own progress callbacks.
    find_package(Threads REQUIRED)
    add_executable(slimefinder gui/finder.c src/slime.c src/chunks.c src/crack.c)
    target_include_directories(slimefinder PRIVATE src)
    target_link_libraries(slimefinder PRIVATE raylib Threads::Threads)
    if(OpenMP_C_FOUND)
        target_link_libraries(slimefinder PRIVATE OpenMP::OpenMP_C)
    endif()
    if(NOT MSVC)
        target_compile_options(slimefinder PRIVATE -O3)
    endif()
endif()
