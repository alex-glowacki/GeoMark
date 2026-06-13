cmake_minimum_required(VERSION 3.20)
project(GeoMark
    VERSION 0.1.0
    DESCRIPTION "Self-contained two-unit RTK GNSS surveying system"
    LANGUAGES C
)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
# Export compile_commands.json for clangd / VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Compiler warnings
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
)
# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)
# Collect all source files
set(GEOMARK_SOURCES
    src/main.c
    src/stream/serial.c
    src/stream/radio.c
    src/stream/file.c
    src/gnss/um980.c
    src/gnss/nmea.c
    src/gnss/rtcm3.c
    src/base/station.c
    src/rover/station.c
    src/collector/collector.c
    src/collector/points.c
    src/collector/coords.c
    src/collector/export.c
    src/net/stream_server.c
    src/net/stream_client.c
    src/ui/tft/display.c
    src/ui/tft/touch.c
    src/ui/tft/screen.c
    src/ui/client.c
    src/util/config.c
    src/util/log.c
    src/util/thread.c
)
add_executable(geomark ${GEOMARK_SOURCES})
# Link pthreads (required for util/thread.c, stream_server.c, stream_client.c)
find_package(Threads REQUIRED)
target_link_libraries(geomark PRIVATE Threads::Threads)
# Tests
enable_testing()
add_subdirectory(tests)