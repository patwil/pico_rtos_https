/**
 * @file pico_tls.h
 * @brief Types and APIs for implementing HTTPS on Pico using lwIP altcp_tls and mbedTLS.
 */

#pragma once

// Pico SDK
#include "pico/stdlib.h"            // Standard library
#include "pico/cyw43_arch.h"        // Pico W wireless

// FreeRTOS tasks prefer to use vTaskDelay to sleep
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// lwIP
#include "lwip/dns.h"               // Hostname resolution
#include "lwip/altcp_tls.h"         // TCP + TLS (+ HTTP == HTTPS)
#include "altcp_tls_mbedtls_structs.h"
#include "lwip/prot/iana.h"         // HTTPS port number
#include "lwip/tcpip.h"

// Mbed TLS
#include "mbedtls/ssl.h"            // Server Name Indication TLS extension

#ifdef MBEDTLS_DEBUG_C
#include "mbedtls/debug.h"          // Mbed TLS debugging
#endif /* MBEDTLS_DEBUG_C */

#include "mbedtls/check_config.h"

#include "https_ca_cert.h"
#include "wifi_config.h"

// DNS response polling interval
//
//  Interval with which to poll for responses to DNS queries.
//
/** @brief DNS response polling interval in milliseconds. */
#define HTTPS_RESOLVE_POLL_INTERVAL             100             // ms

// TCP + TLS connection establishment polling interval
//
//  Interval with which to poll for establishment of TCP + TLS connection
//
/** @brief Poll interval in milliseconds while establishing TCP+TLS connection. */
#define HTTPS_ALTCP_CONNECT_POLL_INTERVAL       100             // ms

// TCP + TLS idle connection polling interval
//
//  Interval with which to poll application (i.e. call registered polling
//  callback function) when TCP + TLS connection is idle.
//
//  The callback function should be registered with altcp_poll(). The polling
//  interval is given in units of 'coarse grain timer shots'; one shot
//  corresponds to approximately 500 ms.
//
//  https://www.nongnu.org/lwip/2_1_x/group__altcp.html
//
/** @brief Idle poll interval in lwIP "coarse timer shots" (~500ms each). */
#define HTTPS_ALTCP_IDLE_POLL_INTERVAL          2               // shots

/* Macros *********************************************************************/

// Array length
/** @brief Compute the number of elements in a static array. */
#define LEN(array) (sizeof array)/(sizeof array[0])

/* Data structures ************************************************************/

/**
 * @brief High-level status notifications sent from lwIP callbacks to the HTTPS task.
 */
typedef enum https_status_e {
    HTTPS_OK = 0,
    HTTPS_CONNECTED,
    HTTPS_SENT,
    HTTPS_RX_DATA,
    HTTPS_RX_DONE,
    HTTPS_RX_ERROR,
    HTTPS_WRITE_ERROR,
    HTTPS_TLS_CONFIG_ERROR,
    HTTPS_SET_HOSTNAME_ERROR,
    HTTPS_CONNECTION_ERROR,
    HTTPS_ERROR
} https_status_t;

/**
 * @brief Per-connection state shared between the HTTPS task and the TCP/IP thread.
 */
typedef struct connection_state_s {
    struct altcp_pcb* pcb;
    struct altcp_tls_config* pconfig;
    ip_addr_t ipaddr;
    int error;
    const char* http_request;
    QueueHandle_t q;
} connection_state_t;

// Size of queue for tls_state.q
/** @brief Length of the queue used to send tcp_evt_t events to the HTTPS task. */
#define TLS_STATE_QUEUE_LEN     16

// Following type is used to convey tx/rx info
// from altcp callbacks to https task
/**
 * @brief Event sent from lwIP callbacks to the HTTPS task.
 */
typedef struct {
    /** Event/status code. */
    https_status_t status;
    /** Associated length (bytes), when applicable. */
    size_t len;
} tcp_evt_t;


/* Functions ******************************************************************/

/**
 * @brief Abort the active TLS connection (must run on lwIP TCP/IP thread).
 * @param arg Pointer to connection_state_t.
 */
void tcpip_tls_abort(void *arg);

/**
 * @brief Close the active TLS connection if open (must run on lwIP TCP/IP thread).
 * @param arg Pointer to connection_state_t.
 */
void tcpip_tls_close(void *arg);

/**
 * @brief Free and clear the TLS client configuration.
 * @param p_connection_state Connection state whose config is freed.
 */
void pico_tls_free_config(connection_state_t* p_connection_state);

/**
 * @brief Resolve @p hostname to an IP address using lwIP DNS.
 * @param hostname Hostname to resolve.
 * @param[out] ipaddr Filled with the resolved address on success.
 * @return ERR_OK on success, otherwise an lwIP error.
 */
err_t resolve_hostname(const char* hostname, ip_addr_t* ipaddr);

/**
 * @brief Start a TCP+TLS client connection (must run on lwIP TCP/IP thread).
 * @param arg Pointer to connection_state_t.
 */
void tcpip_start_client(void *arg);



