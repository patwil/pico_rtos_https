cmake_minimum_required(VERSION 3.13...3.27)

set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1 CACHE BOOL "" FORCE)

# Let caller override: -DPICO_BOARD=...
set(PICO_BOARD pico_w CACHE STRING "Pico board")
set(PICO_SDK_DIR $ENV{PICO_SDK_PATH})

include_directories(${CMAKE_CURRENT_LIST_DIR})
include(pico_sdk_import.cmake)

project(pico_rtos_https C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

message("CMAKE_PROJECT_NAME = ${CMAKE_PROJECT_NAME}")

pico_sdk_init()

include(FetchContent)

# --- FreeRTOSConfig.h provider (MUST exist before importing FreeRTOS) ---
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}          # FreeRTOSConfig.h is here
)

target_compile_definitions(freertos_config INTERFACE
  projCOVERAGE_TEST=0
)

# --- Fetch FreeRTOS-Kernel (but DO NOT MakeAvailable / add_subdirectory) ---
FetchContent_Declare(freertos_kernel
  GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
  GIT_TAG        main   # best practice: pin to a tag/commit later
)

FetchContent_GetProperties(freertos_kernel)
if(NOT freertos_kernel_POPULATED)
  FetchContent_Populate(freertos_kernel)
endif()

set(FREERTOS_KERNEL_PATH ${freertos_kernel_SOURCE_DIR} CACHE PATH "FreeRTOS Kernel path")
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
  include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/Community-Supported-Ports/GCC/RP2350_ARM_NTZ/FreeRTOS_Kernel_import.cmake)
else()
  message(FATAL_ERROR "Unsupported PICO_BOARD='${PICO_BOARD}'.")
endif()

add_executable(pico_rtos_https
  src/pico_rtos_https.c
  src/pico_tls.c
  src/freertos_hooks.c
  src/freertos_assert.c
)

# Wireless network SSID and password (from environment vars) are cached in header file.
#
# Create header file doesn't exist.
#
set(PICO_SSID_H ${CMAKE_CURRENT_BINARY_DIR}/generated/pico_ssid.h)
if(NOT EXISTS "${PICO_SSID_H}")
  if(NOT DEFINED ENV{PICO_WIFI_SSID} OR "$ENV{PICO_WIFI_SSID}" STREQUAL "")
    message(FATAL_ERROR "PICO_WIFI_SSID env var be be set to a non-empty string.")
  endif()
  if(NOT DEFINED ENV{PICO_WIFI_PASSWORD} OR "$ENV{PICO_WIFI_PASSWORD}" STREQUAL "")
    message(FATAL_ERROR "PICO_WIFI_PASSWORD env var be be set to a non-empty string.")
  endif()
endif()

add_custom_command(
  OUTPUT  ${PICO_SSID_H}
  COMMAND ${CMAKE_CURRENT_LIST_DIR}/scripts/mkssidheader.sh
          "\"$ENV{PICO_WIFI_SSID}\"" "\"$ENV{PICO_WIFI_PASSWORD}\"" ${PICO_SSID_H}
)

add_custom_target(gen_ssid_header DEPENDS ${PICO_SSID_H})
add_dependencies(pico_rtos_https gen_ssid_header)

# CA certificate for HTTPS server (from environment var) is cached in header file.
#
# Create header file if it doesn't exist.
#
set(HTTPS_CA_H ${CMAKE_CURRENT_BINARY_DIR}/generated/https_ca_cert.h)

if(NOT EXISTS "${HTTPS_CA_H}")
  if(NOT DEFINED ENV{HTTPS_HOST} OR "$ENV{HTTPS_HOST}" STREQUAL "")
    message(FATAL_ERROR "HTTPS_HOST env var be be set to a non-empty string.")
  endif()
endif()

add_custom_command(
  OUTPUT  ${HTTPS_CA_H}
  COMMAND ${CMAKE_CURRENT_LIST_DIR}/scripts/mkcaheader.sh
          "\"$ENV{HTTPS_HOST}\"" ${HTTPS_CA_H}
)

add_custom_target(gen_ca_header DEPENDS ${HTTPS_CA_H})
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
  FreeRTOS-Kernel
  FreeRTOS-Kernel-Heap4
)

# UART stdio (Debug Probe), disable USB stdio
pico_enable_stdio_uart(${CMAKE_PROJECT_NAME} 1)
pico_enable_stdio_usb(${CMAKE_PROJECT_NAME} 0)

# Define a macro only for Debug builds
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
  $<$<CONFIG:Debug>:PICO_DEBUG_KEEP_TIMER_RUNNING=1>
  $<$<CONFIG:Debug>:PICO_DEBUG_MEMMON=1>
)

pico_add_extra_outputs(${CMAKE_PROJECT_NAME})
