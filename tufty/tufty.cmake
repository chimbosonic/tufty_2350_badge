# Included at the end of pico-openpgp's CMakeLists.txt via PICO_OPENPGP_EXTRA_CMAKE
# (see patches/pico-openpgp.patch). Adds the Tufty 2350 display UI to pico_openpgp.

set(TUFTY_DIR ${CMAKE_CURRENT_LIST_DIR})

target_sources(pico_openpgp PRIVATE
    ${TUFTY_DIR}/board_tufty.c
    ${TUFTY_DIR}/gfx.c
    ${TUFTY_DIR}/st7789.c
    ${TUFTY_DIR}/ui.c
    ${TUFTY_DIR}/third_party/qrcodegen/qrcodegen.c
)

target_include_directories(pico_openpgp PRIVATE
    ${TUFTY_DIR}
    ${TUFTY_DIR}/third_party/qrcodegen
)

pico_generate_pio_header(pico_openpgp ${TUFTY_DIR}/st7789_parallel.pio)

# ENABLE_LVGL_UI makes pico-keys-sdk's main loop call platform_ui_task() (tufty/ui.c).
# No LVGL is involved; it is just the SDK's UI hook.
target_compile_definitions(pico_openpgp PRIVATE ENABLE_LVGL_UI=1)

target_link_libraries(pico_openpgp PRIVATE
    hardware_dma
    hardware_pio
    hardware_pwm
)

# Build ID shown on the badge (boot screen, and the info overlay on button A) and
# printed by build.sh, so you can tell which firmware the device is running.
# Only ui.c gets the define, so a new ID does not rebuild the whole target.
execute_process(
    COMMAND git -C ${TUFTY_DIR}/.. describe --always --dirty
    OUTPUT_VARIABLE TUFTY_GIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT TUFTY_GIT)
    set(TUFTY_GIT "nogit")
endif()
string(TIMESTAMP TUFTY_BUILD_TIME "%Y%m%d-%H%M%S")
set(TUFTY_BUILD_ID "${TUFTY_GIT}-${TUFTY_BUILD_TIME}")
# Non-default USB identity (pico-keys-sdk VIDPID preset, e.g. Gnuk) is part of the ID.
if(VIDPID AND NOT VIDPID STREQUAL "None")
    string(TOLOWER "${VIDPID}" _tufty_vidpid)
    string(APPEND TUFTY_BUILD_ID "-${_tufty_vidpid}")
endif()
set_source_files_properties(${TUFTY_DIR}/ui.c PROPERTIES
    COMPILE_DEFINITIONS "TUFTY_BUILD_ID=\"${TUFTY_BUILD_ID}\"")
file(WRITE ${CMAKE_BINARY_DIR}/tufty_build_id.txt "${TUFTY_BUILD_ID}\n")
file(WRITE ${CMAKE_BINARY_DIR}/tufty_usb_id.txt "${USB_VID}:${USB_PID}\n")
message(STATUS "Tufty build ID: ${TUFTY_BUILD_ID}")
