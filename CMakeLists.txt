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
    src/collector/job_metadata.c
    src/collector/measure_points.c
    src/collector/measure_points_export.c
    src/collector/usb_export.c
    src/collector/export.c
    src/net/stream_server.c
    src/net/stream_client.c
    src/net/rtk_feed_client.c
    src/survey/survey.c
    src/survey/codelist.c
    src/survey/export.c
    src/ui/tft/display.c
    src/ui/tft/screen.c
    src/ui/gpio_button.c
    src/ui/survey_screen.c
    src/ui/client.c
    src/ui/preview.c
    src/ui/core/widget.c
    src/ui/core/widget_draw.c
    src/ui/core/keyboard.c
    src/ui/core/screen_stack.c
    src/ui/core/touch_input.c
    src/ui/screens/sleep_screen.c
    src/ui/screens/sleep_screen_draw.c
    src/ui/screens/main_menu_screen.c
    src/ui/screens/main_menu_screen_draw.c
    src/ui/screens/new_project_screen.c
    src/ui/screens/new_project_screen_draw.c
    src/ui/screens/continue_project_screen.c
    src/ui/screens/continue_project_screen_draw.c
    src/ui/screens/job_setup_screen.c
    src/ui/screens/job_setup_screen_draw.c
    src/ui/screens/job_create_screen.c
    src/ui/screens/job_create_screen_draw.c
    src/ui/screens/open_job_screen.c
    src/ui/screens/open_job_screen_draw.c
    src/ui/screens/measure_points_screen.c
    src/ui/screens/measure_points_screen_draw.c
    src/ui/screens/export_screen.c
    src/ui/screens/export_screen_draw.c
    src/ui/screens/placeholder_screen.c
    src/ui/screens/placeholder_screen_draw.c
    src/ui/screens/project_context.c
    src/ui/screens/job_context.c
    src/util/config.c
    src/util/log.c
    src/util/thread.c
)
add_executable(geomark ${GEOMARK_SOURCES})
# Link pthreads (required for util/thread.c, stream_server.c, stream_client.c, rtk_feed_client.c)
find_package(Threads REQUIRED)
target_link_libraries(geomark PRIVATE Threads::Threads)
# Link libm (required by collector/coords.c's trig/log/pow functions)
target_link_libraries(geomark PRIVATE m)
# Tests
enable_testing()
add_subdirectory(tests)