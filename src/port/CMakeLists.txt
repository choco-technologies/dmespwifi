# ======================================================================
#               dmespwifi_port Module Configuration
# ======================================================================
# The radio of a CPU family (DMOD_CPU_FAMILY): src/port/<family>/. esp32s3 is the radio of the chip, run by the libraries of Espressif
# (esp32s3/radio/: libdmespwifi_port.a, which the package carries and dmod-boot links into the firmware); x86_64 has no radio (the tests).

# Name of the module
set(DMOD_MODULE_NAME        dmespwifi_port)

# Version is already set above and used in project()

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         1024)

# Path to the DMR file for this module
set(DMOD_DMR_PATH           ${CMAKE_SOURCE_DIR}/dmespwifi_port.dmr)

if(DMOD_CPU_FAMILY STREQUAL "esp32s3")
    set(PORT_SOURCES
        esp32s3/port.c
        esp32s3/wifi.c
        esp32s3/osi.c
        esp32s3/radio.c
        esp32s3/glue_host.c
    )
else()
    set(PORT_SOURCES ${DMOD_CPU_FAMILY}/port.c)
endif()

dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    ${PORT_SOURCES}
)

# dmosi: threads, locks, queues, timers; dmdrvi: the Wi-Fi types of the API (dmdrvi_ioctl.h)
dmod_link_modules(${DMOD_MODULE_NAME}
    dmosi
    dmdrvi@>=2.13
)

target_include_directories(${DMOD_MODULE_NAME} BEFORE PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/${DMOD_CPU_FAMILY}
)

if(DMOD_CPU_FAMILY STREQUAL "esp32s3")
    target_include_directories(${DMOD_MODULE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/esp32s3/radio)

    # The glue of the library of the radio (esp32s3/radio/build.sh) needs the headers of dmod - the ones of this build, wherever dmod came
    # from (the installed dmod-dev, a checkout, GitHub): their directories, one per line
    file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/dmespradio-includes.txt
        CONTENT "$<JOIN:$<TARGET_PROPERTY:${DMOD_MODULE_NAME},INCLUDE_DIRECTORIES>,\n>\n")
endif()

target_include_directories(${DMOD_MODULE_NAME}_if INTERFACE
    ${CMAKE_SOURCE_DIR}/include
)
