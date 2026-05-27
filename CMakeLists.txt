cmake_minimum_required(VERSION 3.13...3.27)

set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")

message(STATUS "PICO_SDK_PATH='${PICO_SDK_PATH}'")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(PICO_DEOPTIMIZED_DEBUG 1)
else()
  set(PICO_DEOPTIMIZED_DEBUG 0)
endif()

# Windows Pico builds need this file before project defined.
if(CMAKE_HOST_WIN32)
  include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
endif()

set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1 CACHE BOOL "" FORCE)

# Let caller override: -DPICO_BOARD=...
set(PICO_BOARD pico_w CACHE STRING "Pico board")
set(FREERTOS_KERNEL_PATH $ENV{FREERTOS_SRC_PATH})

include_directories(${CMAKE_CURRENT_LIST_DIR})
include(pico_sdk_import.cmake)

project(pico_rtos_https C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

message(STATUS "CMAKE_PROJECT_NAME = ${CMAKE_PROJECT_NAME}")

pico_sdk_init()

include(FetchContent)

# --- FreeRTOSConfig.h provider (MUST exist before importing FreeRTOS) ---
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/include    # FreeRTOSConfig.h is here
)

target_compile_definitions(freertos_config INTERFACE
  projCOVERAGE_TEST=0
)

set(FREERTOS_HEAP "4" CACHE STRING "" FORCE)

if(NOT DEFINED PICO_BOARD OR PICO_BOARD STREQUAL "")
  message(FATAL_ERROR "PICO_BOARD is not set. Configure with -DPICO_BOARD=pico_w (etc).")
endif()
message("PICO_BOARD = ${PICO_BOARD}")

if(PICO_BOARD MATCHES "^pico(_w)?$")
  # Import the RP2040 port (creates FreeRTOS-Kernel and FreeRTOS-Kernel-Heap4)
  include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)
elseif(PICO_BOARD MATCHES "^pico2(_w)?$")
  # Import the RP2350 ARM port (creates FreeRTOS-Kernel and FreeRTOS-Kernel-Heap4)
  include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2350_ARM_NTZ/FreeRTOS_Kernel_import.cmake)
else()
  message(FATAL_ERROR "Unsupported PICO_BOARD='${PICO_BOARD}'.")
endif()

# Set User Agent for WiFi boards
if(BOARD_TYPE STREQUAL "pico_w")
  set(USER_AGENT_VALUE "pico")
elseif(BOARD_TYPE STREQUAL "pico2_w")
  set(USER_AGENT_VALUE "pico2")
endif()

# Only define USER_AGENT if USER_AGENT_VALUE was set above
if(DEFINED USER_AGENT_VALUE AND NOT USER_AGENT_VALUE STREQUAL "")
  add_compile_definitions(USER_AGENT="${USER_AGENT_VALUE}")
endif()

add_executable(pico_rtos_https
  src/pico_rtos_https.c
  src/pico_tls.c
  src/rx_circular_buffer.c
  src/freertos_hooks.c
  src/freertos_assert.c
  src/ping.c
)

# Some things are better not shared, so we can put them
# in a private header file, which is ignored by git.
set(PRIVATE_HEADER ${CMAKE_SOURCE_DIR}/include/private/private_defs.h)
if(EXISTS "${PRIVATE_HEADER}")
  add_compile_definitions(USE_PRIVATE_INCLUDE)
endif()

# Wireless network SSID and password (from environment vars) are cached in header file.
#
# Create header file doesn't exist.
#
set(WIFI_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/wifi_config.h")
if(NOT EXISTS "${WIFI_HEADER}")
  if(NOT DEFINED ENV{PICO_WIFI_SSID} OR "$ENV{PICO_WIFI_SSID}" STREQUAL "")
    message(FATAL_ERROR "PICO_WIFI_SSID env var must be set to a non-empty string.")
  endif()
  if(NOT DEFINED ENV{PICO_WIFI_PASSWORD} OR "$ENV{PICO_WIFI_PASSWORD}" STREQUAL "")
    message(FATAL_ERROR "PICO_WIFI_PASSWORD env var must be set to a non-empty string.")
  endif()
endif()

if(CMAKE_HOST_WIN32)
  set(MKWIFIHEADER_COMMAND
      powershell.exe
      -NoProfile
      -ExecutionPolicy Bypass
      -File "${CMAKE_SOURCE_DIR}/scripts/mkssidheader.ps1"
  )
else()
  set(MKWIFIHEADER_COMMAND
    bash "${CMAKE_SOURCE_DIR}/scripts/mkssidheader.sh"
  )
endif()

add_custom_command(
  OUTPUT "${WIFI_HEADER}"
  COMMAND ${MKWIFIHEADER_COMMAND}
          "$ENV{PICO_WIFI_SSID}"
          "$ENV{PICO_WIFI_PASSWORD}"
          "${WIFI_HEADER}"
  DEPENDS
    "${CMAKE_SOURCE_DIR}/scripts/mkssidheader.sh"
    "${CMAKE_SOURCE_DIR}/scripts/mkssidheader.ps1"
  COMMENT "Generating Wi-Fi credentials header"
  VERBATIM
)

add_custom_target(generate_wifi_header
  DEPENDS "${WIFI_HEADER}"
)

add_dependencies(pico_rtos_https generate_wifi_header)

# CA certificate for HTTPS server (from environment var) is cached in header file.
#
# Create header file if it doesn't exist.
#
set(HTTPS_CA_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/https_ca_cert.h")

if(NOT EXISTS "${HTTPS_CA_HEADER}")
  if(NOT DEFINED ENV{HTTPS_HOST} OR "$ENV{HTTPS_HOST}" STREQUAL "")
    message(FATAL_ERROR "HTTPS_HOST env var must be set to a non-empty string.")
  endif()
endif()

if(CMAKE_HOST_WIN32)
  set(MKCAHEADER_COMMAND
      powershell.exe
      -NoProfile
      -ExecutionPolicy Bypass
      -File "${CMAKE_SOURCE_DIR}/scripts/mkcaheader.ps1"
  )
else()
  set(MKCAHEADER_COMMAND
    bash "${CMAKE_SOURCE_DIR}/scripts/mkcaheader.sh"
  )
endif()

add_custom_command(
  OUTPUT "${HTTPS_CA_HEADER}"
  COMMAND ${MKCAHEADER_COMMAND} "$ENV{HTTPS_HOST}" "${HTTPS_CA_HEADER}"
  DEPENDS
    "${CMAKE_SOURCE_DIR}/scripts/mkcaheader.sh"
    "${CMAKE_SOURCE_DIR}/scripts/mkcaheader.ps1"
  COMMENT "Generating HTTPS CA certificate header for $ENV{HTTPS_HOST}"
  VERBATIM
)

add_custom_target(gen_ca_header
  DEPENDS ${HTTPS_CA_HEADER})
add_dependencies(pico_rtos_https gen_ca_header)

target_include_directories(pico_rtos_https PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/include
  ${CMAKE_CURRENT_LIST_DIR}/generated
  # Mbed TLS port includes
  #
  #   Internal headers for the Mbed TLS port provided with lwIP. They are not
  #   typically in the pre-processor search path, but are required when using
  #   underlying Mbed TLS interfaces not exposed by the Mbed TLS port (more
  #   specifically, casting for an `mbedtls_ssl_set_hostname` call for Server
  #   Name Indication TLS extension configuration).
  #
  ${PICO_LWIP_PATH}/src/apps/altcp_tls/
  ${PICO_SDK_DIR}/lib/mbedtls/include
  ${PICO_SDK_DIR}/lib/mbedtls/include/mbedtls
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
  CYW43_TASK_STACK_SIZE=2048
  FREERTOS_CONFIG
)

target_link_libraries(${CMAKE_PROJECT_NAME}
  pico_stdlib
  pico_cyw43_arch_lwip_sys_freertos  # Wi-Fi + lwIP + FreeRTOS glue
  pico_lwip_mbedtls                  # lwIP <-> mbedTLS TLS glue (altcp_tls_*)
  pico_mbedtls                       # mbedTLS itself (mbedtls_ssl_set_hostname, etc.)
  pico_lwip_iperf
  FreeRTOS-Kernel
  FreeRTOS-Kernel-Heap4
)


# --- Doxygen documentation target ---
#
# Build with:
#
#   cmake --build <build-dir> --target docs
#
#   e.g. for debug build: cmake --build/Debug --target docs
#
# Generated HTML will be written to:
#
#   <build-dir>/docs/html/index.html
#
#   e.g. for debug build: build/Debug/docs/html/index.html
#
# The docs target is always created. If Doxygen is not installed, building the
# target prints a useful error instead of Ninja reporting "unknown target".
#

find_package(Doxygen QUIET)

if(DOXYGEN_FOUND)
  set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/docs)
  set(DOXYGEN_PROJECT_NAME ${CMAKE_PROJECT_NAME})
  set(DOXYGEN_RECURSIVE YES)
  set(DOXYGEN_EXTRACT_ALL YES)
  set(DOXYGEN_EXTRACT_STATIC YES)
  set(DOXYGEN_GENERATE_HTML YES)
  set(DOXYGEN_GENERATE_LATEX NO)
  set(DOXYGEN_GENERATE_XML NO)
  set(DOXYGEN_QUIET YES)

  doxygen_add_docs(docs
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src
    COMMENT "Generate Doxygen HTML documentation"
  )
else()
  add_custom_target(docs
    COMMAND ${CMAKE_COMMAND} -E echo "Doxygen was not found when this build directory was configured."
    COMMAND ${CMAKE_COMMAND} -E echo "Install Doxygen, then re-run CMake before building the docs target."
    COMMAND ${CMAKE_COMMAND} -E false
    COMMENT "Doxygen is required to generate documentation"
  )
endif()

# UART stdio (Debug Probe), disable USB stdio
pico_enable_stdio_uart(${CMAKE_PROJECT_NAME} 1)
pico_enable_stdio_usb(${CMAKE_PROJECT_NAME} 0)

# Define a macro only for Debug builds
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
  $<$<CONFIG:Debug>:PICO_DEBUG=1>
  $<$<CONFIG:Debug>:PICO_DEBUG_KEEP_TIMER_RUNNING=1>
  $<$<CONFIG:Debug>:PICO_DEBUG_MEMMON=1>
)

pico_add_extra_outputs(${CMAKE_PROJECT_NAME})
