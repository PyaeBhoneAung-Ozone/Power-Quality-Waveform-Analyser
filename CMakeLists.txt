cmake_minimum_required(VERSION 3.20)
project(power_quality_analyser C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(power_quality_analyser
    main.c
    waveform.c
    io.c
)

target_include_directories(power_quality_analyser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Link math library (required for sqrt, fabs, etc.)
target_link_libraries(power_quality_analyser m)

# Enable warnings
if(MSVC)
    target_compile_options(power_quality_analyser PRIVATE /W4)
else()
    target_compile_options(power_quality_analyser PRIVATE -Wall -Wextra -Wpedantic)
endif()
