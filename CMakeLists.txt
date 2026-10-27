cmake_minimum_required(VERSION 3.16)
project(slimecrack C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Always optimize: the default CLion "Debug" profile would be many times slower.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

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
