# Test executables — each is a standalone binary linked against the
# modules it exercises directly (no dependency on main.c).

set(TEST_SOURCES_COMMON
    ${PROJECT_SOURCE_DIR}/src/gnss/nmea.c
    ${PROJECT_SOURCE_DIR}/src/gnss/rtcm3.c
    ${PROJECT_SOURCE_DIR}/src/util/config.c
    ${PROJECT_SOURCE_DIR}/src/util/log.c
    ${PROJECT_SOURCE_DIR}/src/stream/serial.c
    ${PROJECT_SOURCE_DIR}/src/stream/radio.c
    ${PROJECT_SOURCE_DIR}/src/stream/file.c
    ${PROJECT_SOURCE_DIR}/src/collector/collector.c
)

function(add_geomark_test target source)
    add_executable(${target} ${source} ${TEST_SOURCES_COMMON})
    target_include_directories(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
    )
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_geomark_test(test_nmea   test_nmea.c)
add_geomark_test(test_rtcm3  test_rtcm3.c)
add_geomark_test(test_config test_config.c)

function(add_geomark_test_threaded target source extra_sources)
    add_executable(${target} ${source} ${TEST_SOURCES_COMMON} ${extra_sources})
    target_include_directories(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(${target} PRIVATE pthread)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_geomark_test_threaded(test_collector
    ${PROJECT_SOURCE_DIR}/tests/test_collector.c
    ${PROJECT_SOURCE_DIR}/src/collector/collector.c
)

# test_widget: widget grid + screen stack logic only. Deliberately does NOT
# link ui/tft/display.c, ui/gpio_button.c, or anything SPI/GPIO-related —
# the whole point of the widget.c / widget_draw.c split is that the focus,
# hit-testing, and event-handling logic has no display dependency and runs
# on host with no Pi attached.
add_executable(test_widget
    ${PROJECT_SOURCE_DIR}/tests/test_widget.c
    ${PROJECT_SOURCE_DIR}/src/ui/core/widget.c
    ${PROJECT_SOURCE_DIR}/src/ui/core/screen_stack.c
    ${PROJECT_SOURCE_DIR}/src/util/log.c
)
target_include_directories(test_widget PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)
add_test(NAME test_widget COMMAND test_widget)

# test_keyboard: on-screen keyboard logic only (key activation mutating a
# UiKeyboardTarget). No ui/tft/display.h dependency, same convention as
# test_widget -- keys are plain WIDGET_BUTTON entries, so this needs only
# widget.c itself, not widget_draw.c or display.c.
add_executable(test_keyboard
    ${PROJECT_SOURCE_DIR}/tests/test_keyboard.c
    ${PROJECT_SOURCE_DIR}/src/ui/core/widget.c
    ${PROJECT_SOURCE_DIR}/src/ui/core/keyboard.c
    ${PROJECT_SOURCE_DIR}/src/util/log.c
)
target_include_directories(test_keyboard PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)
# Needs libm explicitly for fabs() -- test_decimal_key_produces_parseable_float()
# checks the keyboard's typed "-6.5" string against atof()'s parsed value,
# same convention test_coords.c/test_measure_points_export.c already
# established for their own libm dependency.
target_link_libraries(test_keyboard PRIVATE m)
add_test(NAME test_keyboard COMMAND test_keyboard)

# test_screens: Sleep + Main Menu + New Project + Continue Project +
# Job Setup + Job Create + Open Existing Job + Measure Points +
# Placeholder screens, end to end through the screen stack. Links
# ui/tft/display.c because the screens' on_render functions take
# display_* function addresses when building their UiScreen vtables, so
# the symbols must resolve at link time even though the test never calls
# display_open() and never invokes on_render. Matches the existing
# test_collector convention of linking hardware-touching modules without
# exercising their actual I/O. Also links collector/coords.c and libm
# (target_link_libraries below) since measure_points.c calls
# coords_wgs84_to_nd_north() for ND North job projection and cos() for
# the local-fallback projection.
add_executable(test_screens
    ${PROJECT_SOURCE_DIR}/tests/test_screens.c
    ${PROJECT_SOURCE_DIR}/src/ui/core/widget.c
    ${PROJECT_SOURCE_DIR}/src/ui/core/widget_draw.c
    ${PROJECT_SOURCE_DIR}/src/ui/core/keyboard.c
    ${PROJECT_SOURCE_DIR}/src/ui/core/screen_stack.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/sleep_screen.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/sleep_screen_draw.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/main_menu_screen.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/main_menu_screen_draw.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/new_project_screen.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/new_project_screen_draw.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/continue_project_screen.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/continue_project_screen_draw.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/job_setup_screen.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/job_setup_screen_draw.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/job_create_screen.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/job_create_screen_draw.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/open_job_screen.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/open_job_screen_draw.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/measure_points_screen.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/measure_points_screen_draw.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/export_screen.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/export_screen_draw.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/placeholder_screen.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/placeholder_screen_draw.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/project_context.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/job_context.c
    ${PROJECT_SOURCE_DIR}/src/collector/job_metadata.c
    ${PROJECT_SOURCE_DIR}/src/collector/measure_points.c
    ${PROJECT_SOURCE_DIR}/src/collector/measure_points_export.c
    ${PROJECT_SOURCE_DIR}/src/collector/coords.c
    ${PROJECT_SOURCE_DIR}/src/survey/codelist.c
    ${PROJECT_SOURCE_DIR}/src/ui/tft/display.c
    ${PROJECT_SOURCE_DIR}/src/util/log.c
)
target_include_directories(test_screens PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(test_screens PRIVATE m)
add_test(NAME test_screens COMMAND test_screens)

# test_coords: WGS84 -> NAD83 North Dakota State Plane North Zone (Lambert
# Conformal Conic) transform. Needs libm explicitly (sin/cos/tan/log/pow/
# sqrt/isnan) -- this is the first test target with that dependency.
add_executable(test_coords
    ${PROJECT_SOURCE_DIR}/tests/test_coords.c
    ${PROJECT_SOURCE_DIR}/src/collector/coords.c
)
target_include_directories(test_coords PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(test_coords PRIVATE m)
add_test(NAME test_coords COMMAND test_coords)

# test_measure_points_export: LandXML/CSV export of a job's captured
# points. Pure host-testable (no networking, no hardware) -- unlike
# net/rtk_feed_client.c, this gets a real automated suite. Needs
# coords.c (measure_points_project()'s ND North branch) and libm
# (coords.c's own trig functions), same as test_coords above.
add_executable(test_measure_points_export
    ${PROJECT_SOURCE_DIR}/tests/test_measure_points_export.c
    ${PROJECT_SOURCE_DIR}/src/collector/measure_points_export.c
    ${PROJECT_SOURCE_DIR}/src/collector/measure_points.c
    ${PROJECT_SOURCE_DIR}/src/collector/coords.c
    ${PROJECT_SOURCE_DIR}/src/collector/job_metadata.c
    ${PROJECT_SOURCE_DIR}/src/util/log.c
)
target_include_directories(test_measure_points_export PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(test_measure_points_export PRIVATE m)
add_test(NAME test_measure_points_export COMMAND test_measure_points_export)

# test_job_metadata: defaults, unit coercion enforcement, and INI
# save/load round-trip for job-level metadata.
add_executable(test_job_metadata
    ${PROJECT_SOURCE_DIR}/tests/test_job_metadata.c
    ${PROJECT_SOURCE_DIR}/src/collector/job_metadata.c
    ${PROJECT_SOURCE_DIR}/src/util/log.c
)
target_include_directories(test_job_metadata PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)
add_test(NAME test_job_metadata COMMAND test_job_metadata)

# test_units: util/units.h's conversion and DMS/bearing-formatting
# helpers. Header-only module (every function is `static inline` in the
# header itself) -- no separate .c source to link, just the test file
# against the header, same as how every other consumer of units.h
# already pulls it in (#include only, no units.c exists). Needs libm
# explicitly for fabs() (used both inside units.h itself and in this
# test file's own ASSERT comparisons), same convention test_coords.c/
# test_measure_points_export.c already established.
add_executable(test_units
    ${PROJECT_SOURCE_DIR}/tests/test_units.c
)
target_include_directories(test_units PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(test_units PRIVATE m)
add_test(NAME test_units COMMAND test_units)