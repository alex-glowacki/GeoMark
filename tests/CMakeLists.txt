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