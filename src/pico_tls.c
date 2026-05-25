/**
 * @file pico_tls.c
 * @brief lwIP altcp_tls glue for starting, sending, receiving, and aborting HTTPS connections on the TCP/IP thread.
 */


/* Includes *******************************************************************/

#include <stdio.h>
#include "pico_tls.h" // Options, macros, forward declarations

#include "rx_circular_buffer.h"

/**
 * @brief altcp receive callback (runs on lwIP TCP/IP thread).
 *
 * Copies received data into the circular buffer without blocking, and notifies
 * the HTTPS task via a queue event.
 *
 * @param arg connection_state_t*.
 * @param pcb Connection PCB.
 * @param p Received pbuf chain (NULL when remote closes).
 * @param err lwIP status for this receive.
 * @return ERR_OK on success, ERR_MEM to apply backpressure, or another lwIP error.
 */
static err_t altcp_recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    connection_state_t* p_connection_state = (connection_state_t*)arg;

    // p == NULL means the remote side closed the connection.
    if (p == NULL) {
        tcp_evt_t e = {.status = HTTPS_RX_DONE, .len = 0};
        xQueueSendFromISR(p_connection_state->q, &e, NULL);   // ok from TCP/IP thread too
        p_connection_state->error = altcp_close(pcb);
        return ERR_OK;
    }

    if (err != ERR_OK) {
        pbuf_free(p);
        p_connection_state->error = err;
        // tell reader task that there's something wrong
        tcp_evt_t e = {.status = HTTPS_RX_ERROR, .len = 0};
        xQueueSendFromISR(p_connection_state->q, &e, NULL);   // ok from TCP/IP thread too
        return err;
    }

    // Try to copy the received TCP data into the circular buffer.
    // Do not block here.
    if (p->tot_len > CIRC_BUFF_SZ) {
        // Application cannot ever accept this in one piece as
        // rx data will never fit in the rx buffer,
        // Do not return ERR_MEM forever.
        pbuf_free(p);
        altcp_abort(pcb);
        p_connection_state->error = ERR_BUF;
        p_connection_state->pcb = NULL;
        // tell reader task that there's something wrong
        tcp_evt_t e = {.status = HTTPS_RX_ERROR, .len = 0};
        xQueueSendFromISR(p_connection_state->q, &e, NULL);   // ok from TCP/IP thread too
        return ERR_ABRT;
    }

    if (circ_buff_write(p) != ERR_OK) {
        // Not enough room in the rx buffer right now.
        // Returning ERR_MEM tells lwIP that the application could not
        // accept the data yet.
        // Important:
        //   - do not call altcp_recved()
        //   - do not call pbuf_free()
        //
        // lwIP will keep this pbuf as refused data and retry the recv
        // callback later.
        return ERR_MEM;
    }

    // tell reader task that there's something to read
    tcp_evt_t e = {.status = HTTPS_RX_DATA, .len = p->tot_len};
    xQueueSendFromISR(p_connection_state->q, &e, NULL);   // ok from TCP/IP thread too

    // We have consumed all bytes in this pbuf.
    altcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    return ERR_OK;
}

/**
 * @brief altcp error callback (runs on lwIP TCP/IP thread).
 * @param arg connection_state_t*.
 * @param err lwIP error code.
 */
static void altcp_err_cb(void *arg, err_t err) {
    connection_state_t* p_connection_state = (connection_state_t*)arg;

    p_connection_state->error = err;
    // tell reader task
    tcp_evt_t e = {.status = HTTPS_ERROR, .len = 0};
    xQueueSendFromISR(p_connection_state->q, &e, NULL);   // ok from TCP/IP thread too
}

/**
 * @brief altcp sent callback (runs on lwIP TCP/IP thread).
 * @param arg connection_state_t*.
 * @param pcb Connection PCB.
 * @param len Number of bytes acknowledged.
 * @return ERR_OK.
 */
static err_t altcp_sent_cb(void* arg, struct altcp_pcb* pcb, u16_t len){
    connection_state_t* p_connection_state = (connection_state_t*)arg;

    // tell reader task that there's a TCP + TLS data acknowledgement
    tcp_evt_t e = {.status = HTTPS_SENT, .len = len};
    xQueueSendFromISR(p_connection_state->q, &e, NULL);   // ok from TCP/IP thread too

    return ERR_OK;
}

/**
 * @brief altcp connect callback (runs on lwIP TCP/IP thread).
 *
 * Notifies the HTTPS task that the connection is established and writes the HTTP
 * request to the TLS stream.
 *
 * @param arg connection_state_t*.
 * @param pcb Connection PCB.
 * @param err Connection status (lwIP uses ERR_OK here).
 * @return ERR_OK or lwIP error from altcp_write().
 */
static err_t altcp_connect_cb(void *arg, struct altcp_pcb *pcb, err_t err) {
    // no need to check err as it's always ERR_OK

    connection_state_t* p_connection_state = (connection_state_t*)arg;

    // tell reader task that we're ready to send
    tcp_evt_t e = {.status = HTTPS_CONNECTED, .len = 0};
    xQueueSendFromISR(p_connection_state->q, &e, NULL);   // ok from TCP/IP thread too

    err = altcp_write(p_connection_state->pcb,
                      p_connection_state->http_request,
                      strlen(p_connection_state->http_request),
                      TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        // tell reader task
        tcp_evt_t e = {.status = HTTPS_WRITE_ERROR, .len = 0};
        xQueueSendFromISR(p_connection_state->q, &e, NULL);   // ok from TCP/IP thread too
        return err;
    }
    altcp_output(p_connection_state->pcb);
    return ERR_OK;
}

/**
 * @brief altcp poll callback (runs on lwIP TCP/IP thread).
 * @param arg User argument.
 * @param pcb Connection PCB.
 * @return ERR_OK.
 */
static err_t altcp_poll_cb(void *arg, struct altcp_pcb *pcb) {
    // TCP + TLS connection idle.
    // Not used currently.
    return ERR_OK;
}

/**
 * @brief DNS resolution callback used by dns_gethostbyname().
 * @param name Hostname queried.
 * @param resolved_ip_addr Resolved address, or NULL on failure.
 * @param ipaddr User pointer to ip_addr_t to fill.
 */
static void gethostbyname_cb(const char *name, const ip_addr_t *resolved_ip_addr, void* ipaddr) {
    if (resolved_ip_addr) {
        *((ip_addr_t*)ipaddr) = *resolved_ip_addr;        // Successful resolution
    } else {
       ((ip_addr_t*)ipaddr)->addr = IPADDR_NONE;          // Failed resolution
    }
}

/**
 * @brief Abort the active TLS connection (must run on lwIP
 *        TCP/IP thread).
 *
 * Detailed API docs are on the declaration in pico_tls.h.
 */
void tcpip_tls_abort(void *arg) {
    connection_state_t* p_connection_state = (connection_state_t*)arg;
    if (p_connection_state->pcb) {
        altcp_abort(p_connection_state->pcb);
        p_connection_state->pcb = NULL;
    }
}

/**
 * @brief Close the active TLS connection if open (runs on
 *    lwIP TCP/IP thread).
 *
 * If close returns ERR_MEM, the caller can scheduleanother
 * close attempt later.
 *
 * Detailed API docs are on the declaration in pico_tls.h.
 *  */
void tcpip_tls_close(void *arg) {
    connection_state_t* p_connection_state = (connection_state_t*)arg;
    if (p_connection_state->pcb) {
        // Anything other than ERR_MEM is OK as it probably means that the pcb we
        // supplied is invalid because connection is closed already.
        p_connection_state->error = altcp_close(p_connection_state->pcb);
        if (p_connection_state->error != ERR_MEM) {
            p_connection_state->error = ERR_OK;
            p_connection_state->pcb = NULL;
        }
        // we'll schedule this again if there was an error
    }
}

/**
 * @brief Free and clear the TLS client configuration in the
 *    connection state.
 * 
 * Detailed API docs are on the declaration in pico_tls.h.
 */
void pico_tls_free_config(connection_state_t* p_connection_state) {
    if (p_connection_state->pconfig) {
        altcp_tls_free_config(p_connection_state->pconfig);
        p_connection_state->pconfig = NULL;
    }
}

// Resolve hostname
/**
 * @brief Resolve a hostname to an IP address using lwIP DNS.
 *
 * If the lookup is asynchronous, this function polls until completion using
 * vTaskDelay().
 *
 * Detailed API docs are on the declaration in pico_tls.h.
 */
err_t resolve_hostname(const char* hostname, ip_addr_t* ipaddr) {

    // Zero address
    ipaddr->addr = IPADDR_ANY;

    // Attempt resolution
    err_t err = dns_gethostbyname(hostname, ipaddr, gethostbyname_cb, ipaddr);

    if (err == ERR_INPROGRESS) {

        // Await resolution
        //
        //  IP address will be made available shortly (by callback) upon DNS
        //  query response.
        //
        while (ipaddr->addr == IPADDR_ANY) {
            vTaskDelay(pdMS_TO_TICKS(HTTPS_RESOLVE_POLL_INTERVAL));
        }
        if (ipaddr->addr != IPADDR_NONE) {
            err = ERR_OK;
        }
    }

    return err;
}

/**
 * @brief Start a TLS client connection on the lwIP TCP/IP
 *       thread.
 *
 * Creates the TLS PCB, configures SNI hostname, sets
 * callbacks,and initiates the connection to HTTPS (port 443).
 * Reports status back to the HTTPS task via the queue in the
 * connection state.
 *          
 * Detailed API docs are on the declaration in pico_tls.h.
 */
void tcpip_start_client(void *arg) {
    connection_state_t* p_connection_state = (connection_state_t*)arg;

    // ALL raw/altcp calls go here:
    p_connection_state->pcb = altcp_tls_new(p_connection_state->pconfig, IP_PROTO_TCP);
    if (p_connection_state->pcb == NULL) {
        // signal failure to task
        tcp_evt_t e = {.status = HTTPS_TLS_CONFIG_ERROR, .len = 0};
        xQueueSendFromISR(p_connection_state->q, &e, NULL);   // ok from TCP/IP thread too
        return;
    }

    // Configure hostname for Server Name Indication extension
    // Simplify arg typecast to aid debugging
    struct altcp_pcb* pcb = p_connection_state->pcb;
    altcp_mbedtls_state_t* pmbedtls_state = (altcp_mbedtls_state_t*)(pcb->state);
    mbedtls_ssl_context* pssl_context = &(pmbedtls_state->ssl_context);
    err_t err = mbedtls_ssl_set_hostname(pssl_context, HTTPS_HOST);
    if (err != ERR_OK) {
        altcp_abort(p_connection_state->pcb);
        p_connection_state->pcb = NULL;
        // signal failure to task
        tcp_evt_t e = {.status = HTTPS_SET_HOSTNAME_ERROR, .len = 0};
        xQueueSendFromISR(p_connection_state->q, &e, NULL);   // ok from TCP/IP thread too
        return;
    }

    altcp_arg(p_connection_state->pcb, p_connection_state);
    altcp_recv(p_connection_state->pcb, altcp_recv_cb);
    altcp_err(p_connection_state->pcb, altcp_err_cb);
    altcp_sent(p_connection_state->pcb, altcp_sent_cb);

    err = altcp_connect(p_connection_state->pcb, &p_connection_state->ipaddr, LWIP_IANA_PORT_HTTPS, altcp_connect_cb);
    if (err != ERR_OK) {
        altcp_abort(p_connection_state->pcb);
        p_connection_state->pcb = NULL;
        // signal failure to task
        tcp_evt_t e = {.status = HTTPS_CONNECTION_ERROR, .len = 0};
        xQueueSendFromISR(p_connection_state->q, &e, NULL);   // ok from TCP/IP thread too
        return;
    }
}

