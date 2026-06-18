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

# test_screens: Sleep + Main Menu + Placeholder screens, end to end through
# the screen stack. Links ui/tft/display.c because the screens' on_render
# functions take display_* function addresses when building their UiScreen
# vtables, so the symbols must resolve at link time even though the test
# never calls display_open() and never invokes on_render. Matches the
# existing test_collector convention of linking hardware-touching modules
# without exercising their actual I/O.
add_executable(test_screens
    ${PROJECT_SOURCE_DIR}/tests/test_screens.c
    ${PROJECT_SOURCE_DIR}/src/ui/core/widget.c
    ${PROJECT_SOURCE_DIR}/src/ui/core/widget_draw.c
    ${PROJECT_SOURCE_DIR}/src/ui/core/screen_stack.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/sleep_screen.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/sleep_screen_draw.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/main_menu_screen.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/main_menu_screen_draw.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/placeholder_screen.c
    ${PROJECT_SOURCE_DIR}/src/ui/screens/placeholder_screen_draw.c
    ${PROJECT_SOURCE_DIR}/src/ui/tft/display.c
    ${PROJECT_SOURCE_DIR}/src/util/log.c
)
target_include_directories(test_screens PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)
add_test(NAME test_screens COMMAND test_screens)