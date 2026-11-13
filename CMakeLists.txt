cmake_minimum_required(VERSION 3.18)

# The radio is the one of a CPU family: the driver (dmespwifi) is common, the port (dmespwifi_port, src/port/<family>/) is the radio. The
# module is built and released per family, not per architecture: esp32s3. x86_64 has a port without a radio for the tests on the host -
# never released. The family selects the tools (src/port/<family>/config.cmake)
set(DMOD_CPU_FAMILY "esp32s3" CACHE STRING "Target CPU family: esp32s3, or x86_64 (no radio: the tests)")
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/port/${DMOD_CPU_FAMILY}/config.cmake)
    message(FATAL_ERROR "dmespwifi: no port for the CPU family '${DMOD_CPU_FAMILY}' (esp32s3; x86_64 for the tests)")
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/src/port/${DMOD_CPU_FAMILY}/config.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED DMOD_MODULE_VERSION)
    set(DMOD_MODULE_VERSION "0.1" CACHE STRING "DMOD module version")
endif()

set(DMOD_MODE "DMOD_MODULE" CACHE STRING "DMOD build mode")
set(DMOD_BUILD_TESTS OFF CACHE BOOL "Build tests")
set(DMOD_BUILD_EXAMPLES OFF CACHE BOOL "Build examples")
set(DMOD_BUILD_TOOLS OFF CACHE BOOL "Build tools")
set(DMOD_BUILD_TEMPLATES OFF CACHE BOOL "Build templates")
option(DMOD_USE_INSTALLED_SDK "Use an installed dmod-dev SDK when available" ON)

# Explicit source paths take precedence over an installed SDK. Resolve DMOD
# before project() so DMOD_TOOLS_NAME can select the compiler in either flow.
set(_dmod_installed_sdk FALSE)
if(DMOD_USE_INSTALLED_SDK AND DMOD_MODE STREQUAL "DMOD_MODULE"
        AND NOT DMOD_DIR AND NOT FETCHCONTENT_SOURCE_DIR_DMOD)
    find_package(dmod CONFIG QUIET)
    if(dmod_FOUND)
        set(_dmod_installed_sdk TRUE)
    endif()
endif()

if(NOT _dmod_installed_sdk)
    if(DEFINED DMOD_DIR AND NOT "${DMOD_DIR}" STREQUAL "")
        set(FETCHCONTENT_SOURCE_DIR_DMOD "${DMOD_DIR}" CACHE PATH "" FORCE)
    endif()
    include(FetchContent)
    FetchContent_Declare(
        dmod
        GIT_REPOSITORY https://github.com/choco-technologies/dmod.git
        GIT_TAG        develop
    )
    FetchContent_MakeAvailable(dmod)
endif()

project(dmespwifi VERSION ${DMOD_MODULE_VERSION}
    DESCRIPTION "dmespwifi DMOD library module"
    LANGUAGES C CXX)

if(NOT _dmod_installed_sdk)
    set(DMOD_DIR "${dmod_SOURCE_DIR}" CACHE PATH "DMOD source directory")
    set(DMOD_SCRIPTS_DIR "${DMOD_DIR}/scripts" CACHE PATH "DMOD scripts directory")
    include("${DMOD_DIR}/paths.cmake")
    dmod_setup_external_module()
endif()

# Name of the module
set(DMOD_MODULE_NAME        dmespwifi)

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         2048)

# Path to the .dmr file - enables release package creation in build/packages/
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/dmespwifi.dmr)

# List of MAL interfaces implemented by this module (leave empty if none)
set(DMOD_MAL_IMPLS          )

# List of DIF interfaces implemented by this module (leave empty if none)
set(DMOD_DIF_IMPLS          )

#
#   dmod_add_library - create a library module
#   it has the same signature as add_library
#   and can be used in the same way after the creation
#   (for example, to link libraries)
#
dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    # List of source files - can include C and C++ files
    src/dmespwifi.c
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(${DMOD_MODULE_NAME}_if INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# To depend on another module's headers, use:
#   dmod_link_modules(${DMOD_MODULE_NAME}
#       other_module@1.0
#   )
# dmdrvi: the driver interface, dmini: the configuration, dmnetif: the interface of the network, dmosi: threads, locks, queues, timers
dmod_link_modules(${DMOD_MODULE_NAME}
    dmdrvi@>=2.15
    dmini
    dmnetif
    dmroute
    dmosi
)

# The radio: the port of the family
add_subdirectory(src/port)
target_link_libraries(${DMOD_MODULE_NAME} ${DMOD_MODULE_NAME}_port_if)

enable_testing()
add_subdirectory(tests)
