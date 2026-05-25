/**
 * @file pico_rtos_https.c
 * @brief FreeRTOS HTTPS client task for Raspberry Pi Pico W / Pico 2 W using lwIP + mbedTLS.
 */

#include <stdio.h>

#include "pico_tls.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "ping.h"
#include "hardware/watchdog.h"
#include "rx_circular_buffer.h"

// If present this include file is used to override
// some definitions and settings 
#if defined(USE_PRIVATE_INCLUDE)
    #include "private/private_defs.h"
#endif

#if PICO_DEBUG
    #define PICO_DEBUG_PRINT(format,args...) printf(format, ## args)
#else
    #define PICO_DEBUG_PRINT(...)
#endif

#define PICO_ERROR_PRINT(format,args...) fprintf(stderr, format, ## args)

// We'll reset if any monitored task
// misses this many watchdog checks
#define MAX_WATCHDOG_MISS_COUNT     3

// How often watchdog checks performed
#define WATCHDOG_CHECK_TIMER        60  // seconds

// How long to wait between fatal error condition
// and pulling the reset trigger
#define DELAY_BEFORE_RESET          10  // seconds

#ifndef HTTPS_REQUEST
    #ifndef HTTPS_HOST
        #error HTTPS_HOST is not defined
    #endif

    // Default for User Agent. It should be defined in cmake.
    #ifndef USER_AGENT
        #define USER_AGENT "pico2w"
    #endif

    #define HTTPS_REQUEST \
      "GET / HTTP/1.1\r\n" \
      "Host: " HTTPS_HOST "\r\n" \
      "User-Agent: " USER_AGENT "/1.0\r\n" \
      "Accept: */*\r\n" \
      "Accept-Encoding: identity\r\n" /* avoid gzip */ \
      "Connection: close\r\n" \
      "\r\n"
#endif // !HTTPS_REQUEST

/**
 * @brief Convert an lwIP error code to a short human-readable string.
 * @param e lwIP error code.
 * @return Static string describing @p e.
 */
static const char* lwip_errstr(err_t e) {
    switch(e){
    case ERR_OK: return "OK";
    case ERR_MEM: return "MEM";                 // Out of memory
    case ERR_BUF: return "BUF";                 // Buffer
    case ERR_TIMEOUT: return "TIMEOUT";         // Timeout
    case ERR_RTE: return "RTE";                 // Routing problem
    case ERR_INPROGRESS: return "INPROGRESS";   // Operation in progress
    case ERR_VAL: return "VAL";                 // Illegal value
    case ERR_WOULDBLOCK: return "WOULDBLOCK";   // Operation would block
    case ERR_USE: return "USE";                 // Address in use
    case ERR_ALREADY: return "ALREADY";         // Already connecting
    case ERR_ISCONN: return "ISCONN";           // Conn already established
    case ERR_CONN: return "CONN";               // Not connected
    case ERR_IF: return "IF";                   // Low-level netif error
    case ERR_ABRT: return "ABRT";               // Connection aborted
    case ERR_RST: return "RST";                 // Connection reset
    case ERR_CLSD: return "CLSD";               // Connection closed
    case ERR_ARG: return "ARG";                 // Illegal argument
    default: return "?";
    }
}

/**
 * @brief Convert HTTPS state machine status to a human-readable string.
 * @param https_status Status enum value.
 * @return Static string describing @p https_status.
 */
static const char* https_status_str(https_status_t https_status) {
      switch (https_status) {
      case HTTPS_OK: return "OK";
      case HTTPS_CONNECTED: return "Connected";
      case HTTPS_SENT: return "Sent";
      case HTTPS_RX_DATA: return "Rx Data";
      case HTTPS_RX_DONE: return "Rx Done";
      case HTTPS_RX_ERROR: return "Rx error";
      case HTTPS_WRITE_ERROR: return "Write error";
      case HTTPS_TLS_CONFIG_ERROR: return "TLS Config error";
      case HTTPS_SET_HOSTNAME_ERROR: return "Set Hostname error";
      case HTTPS_CONNECTION_ERROR: return "Connection error";
      case HTTPS_ERROR: return "Error";
      default: break;
      }
      return "Unknown https status";
}

typedef enum ping_status_e {
    PING_OK      = 0,
    PING_TIMEOUT = 1,
    PING_ERROR   = -1
} ping_status_t;

// Watchdog flags for each of our tasks that
// ought to be running. Main task finishes after setup.
static volatile int https_task_wdog_flag;
static volatile int check_net_task_wdog_flag;
static volatile int blink_task_wdog_flag;

#if defined(PICO_DEBUG_MEMMON)
static volatile int memmon_task_wdog_flag;
#endif

#define WIFI_RECONNECT_BACKOFF_MS  10000
#define TCPIP_OPERATION_PENDING    0xdeadbeef

#if defined(PICO_DEBUG_KEEP_TIMER_RUNNING)
#include "hardware/structs/timer.h"
static inline void dbg_keep_timer_running(void) { timer_hw->dbgpause = 0; }
#else
static inline void dbg_keep_timer_running(void) { }
#endif

#if defined(PICO_DEBUG_MEMMON)
/**
 * @brief Print memory usage for each active task. Only called
 *        in debug builds. Stack usage is historical High Water
 *        Mark (HWM) of task stack, rather than current stack
 *        usage.
 */
static void dump_task_stacks(void) {
    char buf[512];
    vTaskList(buf);
    PICO_DEBUG_PRINT("Task          State   Prio     Stack   Num      Affinity\n%s\n", buf);

    // Also dump heap_4 minimum ever free:
    PICO_DEBUG_PRINT("heap: free=%u  min_ever_free=%u\n",
           (unsigned)xPortGetFreeHeapSize(),
           (unsigned)xPortGetMinimumEverFreeHeapSize());
}

/**
 *  @brief FreeRTOS task that, once a minute, displays memory
 *         usage and, 30 seconds later, circular buffer
 *         indices/state. (debug builds)
 *
 * @param arg Unused.
 */
static void memmon_task(void *p) {
    (void)p;
    for (;;) {
        dump_task_stacks();
        memmon_task_wdog_flag = 0;
        vTaskDelay(pdMS_TO_TICKS(30000));
        print_circ_buff_info();
        memmon_task_wdog_flag = 0;
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
#endif /* PICO_DEBUG_MEMMON */

/**
 * @brief Fetch the current IPv4 gateway address from the default lwIP netif.
 *
 * On success, writes the gateway address into @p ip_addr.
 *
 * @param[out] ip_addr Destination for the gateway IP (must not be NULL).
 * @return @p ip_addr on success, NULL on failure.
 */
static ip_addr_t* get_gateway_ip(ip_addr_t* ip_addr) {
    if (ip_addr == NULL) {
        PICO_ERROR_PRINT("NULL ip_addr arg in %s\n", __FUNCTION__);
        return NULL;
    }

    ip_addr->addr = IPADDR_NONE;

    struct netif *n = netif_default;
    if (!n) {
        PICO_ERROR_PRINT("netif_default is NULL\n");
        return NULL;
    }

    // For IPv4:
    ip_addr_set_ip4_u32(ip_addr, ip4_addr_get_u32(netif_ip4_gw(n)));
    return ip_addr;
}

/**
 * @brief Print the current IPv4 gateway address (debug builds).
 */
static void print_gateway_ip(void) {
    ip_addr_t gw;
    if (get_gateway_ip(&gw)) {
        // Print it
        PICO_DEBUG_PRINT("Gateway: %s\n", gw.addr != IPADDR_NONE ? ipaddr_ntoa(&gw) : "not set");
    }

    // If you want it as a u32 in network byte order:
    // uint32_t gw_u32 = ip4_addr_get_u32(gw);
}

/**
 * @brief Ping the current gateway and return the previous ping result.
 *
 * The first call (or any call after @p ping_target is reset to IPADDR_NONE)
 * initializes the ping module with the current gateway IP.
 *
 * @param[in,out] ping_target Gateway target address; set to IPADDR_NONE to re-init.
 * @return PING_OK, PING_TIMEOUT, or PING_ERROR.
 */
static ping_status_t ping_gateway(ip_addr_t* ping_target) {

    if (ping_target == NULL) {
        return PING_ERROR;
    }

    // See if we need to (re)initialise
    if (ping_target->addr == IPADDR_NONE) {
        if ((get_gateway_ip(ping_target) == NULL) || (ping_target->addr == IPADDR_NONE)) {
            return PING_ERROR;
        }

        ping_init(ping_target);
        PICO_DEBUG_PRINT("init ping to %s\n", ipaddr_ntoa(ping_target));

        // Presume that ping is OK for now
        return PING_OK;
    }

    ping_status_t ping_rc = (get_last_ping_result() == 1) ? PING_OK : PING_TIMEOUT;

    ping_send_now();

    return ping_rc;
}

/**
 * @brief Trigger a software reset after an optional delay.
 *
 * Uses FreeRTOS delay and then enables the hardware watchdog with a 1ms timeout.
 *
 * @param kdelay Delay in seconds before resetting.
 */
void software_reset(const u32_t kdelay) {
    if (kdelay) {
        PICO_ERROR_PRINT("\nReset in %1u second%s...\n", kdelay, kdelay == 1 ? "" : "s");
    }
    vTaskDelay(pdMS_TO_TICKS(kdelay * 1000));
    watchdog_enable(1, 1);
    while (true);
}

/**
 * @brief Connect to the configured Wi-Fi SSID with retries and timeout.
 *
 * @return lwIP error code (ERR_OK on success).
 */
static int connect_to_wifi(void) {
    const int ktimeout_ms = 15000; // 15s
    const int kmax_attempts = 3;   // 45s. Has to be under a minute for watchdog
    int attempt = 0;
    int rc = ERR_OK;
    do {
        PICO_DEBUG_PRINT("Connecting to \"%s\"...\n", PICO_WIFI_SSID);
        rc = cyw43_arch_wifi_connect_timeout_ms(PICO_WIFI_SSID,
                                                PICO_WIFI_PASSWORD,
                                                CYW43_AUTH_WPA2_AES_PSK,
                                                ktimeout_ms);
        if (rc == ERR_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));

    } while (attempt++ < kmax_attempts);
    if (rc != ERR_OK) {
        PICO_ERROR_PRINT("failed to connect after %1d attempts\n", attempt);
        return rc;
    }
#ifdef PICO_DEBUG
    vTaskDelay(pdMS_TO_TICKS(1000));
    print_gateway_ip();
#endif
    return rc;
}

/**
 * @brief FreeRTOS task that blinks the Pico W LED and kicks its watchdog flag.
 * @param arg Unused.
 */
static void blink_task(void *arg) {
    (void)arg;
    bool led = false;

    for (;;) {
        led = !led;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
        vTaskDelay(pdMS_TO_TICKS(1000));
        blink_task_wdog_flag = 1;
    }
}

/**
 * @brief FreeRTOS task that monitors other tasks and resets on repeated misses.
 * @param arg Unused.
 */
void watchdog_task(void* arg) {
    (void)arg;

    const int kmax_wdog_miss_count = MAX_WATCHDOG_MISS_COUNT;

    for (;;) {
        // Delay before checking each task
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_CHECK_TIMER*1000));

        bool do_reset = false;

        // We don't check main task because it finishes after
        // setting up other tasks

        // make copies of task flags so they won't change
        // while watchdog check is done.
        int https_task_flag_counter = https_task_wdog_flag;

        // We could just OR these checks together, however
        // it's good to see if more than one task has
        // timed out.
        if (https_task_flag_counter >= kmax_wdog_miss_count) {
            PICO_ERROR_PRINT("\nHTTPS task watchdog fail\n");
            do_reset = true;
        }
        // break down flag updates into parts, so that
        // task flags can be updated atomically.
        https_task_flag_counter++;
        https_task_wdog_flag = https_task_flag_counter;

        int check_net_task_flag_counter = check_net_task_wdog_flag;
        if (check_net_task_flag_counter >= kmax_wdog_miss_count) {
            PICO_ERROR_PRINT("\nCheck Net task watchdog fail\n");
            do_reset = true;
        }
        check_net_task_flag_counter++;
        check_net_task_wdog_flag = check_net_task_flag_counter;

        // It's an unimportant task, but we'll check it
        // just because...
        int blink_task_flag_counter = blink_task_wdog_flag;
        if (blink_task_flag_counter >= kmax_wdog_miss_count) {
            PICO_ERROR_PRINT("\nBlink task watchdog fail\n");
            do_reset = true;
        }
        blink_task_flag_counter++;
        blink_task_wdog_flag = blink_task_flag_counter;

#if defined(PICO_DEBUG_MEMMON)
        int memmon_task_flag_counter = memmon_task_wdog_flag;
        if (memmon_task_flag_counter >= kmax_wdog_miss_count) {
            PICO_ERROR_PRINT("\nmemmon task watchdog fail\n");
            do_reset = true;
        }
        memmon_task_flag_counter++;
        memmon_task_wdog_flag = memmon_task_flag_counter;
#endif

        if (do_reset) {
            const u32_t kreset_delay = DELAY_BEFORE_RESET;
            software_reset(kreset_delay);
            assert(0); // should never reach this line
        }
    }
}

/**
 * @brief FreeRTOS task that monitors Wi-Fi link and periodically pings the gateway.
 * @param arg Unused.
 */
void check_net_task(void* arg) {
    (void)arg;

    ip_addr_t gateway_ip = {0};
    gateway_ip.addr = IPADDR_NONE;

    // check wifi link status every 10 seconds, but ping
    // gateway once a minute.
    const u32_t kping_interval = 6;
    u32_t ping_timer = kping_interval;

    for (;;) {
        check_net_task_wdog_flag = 0;

        int rc = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        if (rc != CYW43_LINK_UP) {

            PICO_DEBUG_PRINT("WiFi not ready (link=%d). Reconnecting...\n", rc);

            // We need to check if gateway has changed after reconnect.
            // Setting addr to NONE will force ping reinitialisation.
            gateway_ip.addr = IPADDR_NONE;

            int retry = 0;
            do {
                rc = connect_to_wifi();
                check_net_task_wdog_flag = 0;
                if (rc == ERR_OK) {
                    break;
                }
                // Force a clean state before reconnect
                cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);

                vTaskDelay(pdMS_TO_TICKS(10000));
            } while (retry++ < 20);
            if (rc != ERR_OK) {
                PICO_ERROR_PRINT("Failed to reconnect to wifi\n");
                const u32_t kreset_delay = DELAY_BEFORE_RESET;
                software_reset(kreset_delay);
                assert(0); // should never reach this line
            }

            // try again from the top
            continue;
        }

        if (--ping_timer == 0) {
            ping_status_t pingrc = ping_gateway(&gateway_ip);
            if (pingrc != PING_OK) {
                // Maybe we should do something if ping keeps failing,
                // but for now we'll just report it.
                PICO_ERROR_PRINT("ping gateway %s\n", pingrc == PING_TIMEOUT ? "Timeout" : "Error");
            }
            ping_timer = kping_interval;
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/**
 * @brief FreeRTOS task that performs periodic HTTPS GET requests.
 *
 * This task resolves the hostname, creates a TLS config, starts the lwIP TCP/TLS
 * client on the TCP/IP thread, and drains received data from a circular buffer.
 *
 * In lwIP, the raw TCP/altcp API (including altcp_tls) is not
 * thread-safe: its protocol control blocks (PCBs), timers,
 * retransmits, and callback dispatch all live in lwIP’s
 * TCP/IP core.
 * In the FreeRTOS port (pico_cyw43_arch_lwip_sys_freertos),
 * that core runs in the dedicated tcpip thread.
 *
 * So anything that creates/modifies a PCB:
 * altcp_new/altcp_connect/altcp_write/altcp_close, setting
 * callbacks, etc. - must run on the tcpip thread (or under lwIP
 * core locking). Otherwise you get races between your task and
 * the tcpip thread (e.g. freeing/changing a PCB while the core
 * is processing it), which can show up as missing callbacks,
 * ERR_ABRT, hangs in altcp_close(), or hard-to-reproduce
 * deadlocks. The safe pattern is to do all altcp work inside
 * the tcpip thread via callbacks (recv/connected/sent) or by
 * scheduling work with tcpip_callback().
 *
 * @param arg Unused.
 */
void https_task(void *arg) {
    (void)arg;

    circ_buffer_init();

    for (;;) {

        https_task_wdog_flag = 0;

        connection_state_t connection_state = {0};
        connection_state.http_request = HTTPS_REQUEST;

        // Event queue to talk from lwIP thread -> your task
        QueueHandle_t q;
        err_t err = resolve_hostname(HTTPS_HOST, &connection_state.ipaddr);
        if (err == ERR_OK) {
            PICO_DEBUG_PRINT("%s IP address: %s\n", HTTPS_HOST, ipaddr_ntoa(&connection_state.ipaddr));
        } else {
            PICO_ERROR_PRINT("Error (%d) resolving IP address for %s\n", (int)err, HTTPS_HOST);
            // Delay to let net check task fix any local network errors
            vTaskDelay(pdMS_TO_TICKS(15000));
            continue;
        }

        q = xQueueCreate(TLS_STATE_QUEUE_LEN, sizeof(tcp_evt_t));

        if (q == NULL) {
            PICO_ERROR_PRINT("Error creating queue\n");
            break;
        }
        connection_state.q = q;

        u8_t ca_cert[] = HTTPS_CA_CERT;
        struct altcp_tls_config* pconfig = altcp_tls_create_config_client(ca_cert, sizeof(ca_cert));
        if (pconfig == NULL) {
            PICO_ERROR_PRINT("Error creating TLS client config\n");
            break;
        }
        connection_state.pconfig = pconfig;

        // Schedule the TCP/IP-thread function:
        err = tcpip_callback(tcpip_start_client, &connection_state);
        if (err == ERR_OK) {
            tcp_evt_t e;
            bool in_progress = true;
            while (in_progress && xQueueReceive(q, &e, portMAX_DELAY)) {
                switch (e.status) {
                case HTTPS_OK:
                    break;
                case HTTPS_CONNECTED:
                case HTTPS_SENT:
                    PICO_DEBUG_PRINT("%s to %s\n", https_status_str(e.status), HTTPS_HOST);
                    break;
                case HTTPS_RX_DATA:
                    int tot_len = e.len;
                    u8_t rx_data[1024+1]; // 1KB plus trailing NULL
                    size_t rx_data_max_size = LEN(rx_data) - 1;
                    size_t tot_bytes_read = 0;
                    PICO_DEBUG_PRINT("Received %d bytes:\n", tot_len);
                    while (tot_bytes_read < tot_len) {
                        size_t n = circ_buff_read(rx_data, rx_data_max_size);
                        if (n == 0) {
                            break;
                        }
                        rx_data[n] = 0; // trailing NULL
                        fputs(rx_data, stdout);
                        tot_bytes_read += n;
                    }
                    putchar('\n');
                    if (tot_bytes_read != tot_len) {
                        PICO_ERROR_PRINT("\n##### Warning: read %d rx bytes, but was expecting %d #####\n", tot_bytes_read, tot_len);
                    }
                    break;
                case HTTPS_RX_DONE:
                    PICO_DEBUG_PRINT("\nRx done. Closing connection\n");
                    vTaskDelay(pdMS_TO_TICKS(500));  // Allow tcpip task time to close connection
                    // if TCPIP task closed connection OK we won't need to
                    connection_state.pcb = NULL;
                    in_progress = false;
                    break;
                case HTTPS_RX_ERROR:
                    PICO_ERROR_PRINT("%s: (%s). Aborting\n", https_status_str(e.status), lwip_errstr(connection_state.error));
                    err = tcpip_callback(tcpip_tls_abort, &connection_state);
                    if (err != ERR_OK) {
                        PICO_ERROR_PRINT("Error (%s) calling tcpip_callback(tcpip_tls_abort)\n", lwip_errstr(err));
                    }
                    vTaskDelay(pdMS_TO_TICKS(500)); // Allow tcpip task time to abort connection
                    in_progress = false;
                    break;
                case HTTPS_WRITE_ERROR:
                case HTTPS_TLS_CONFIG_ERROR:
                case HTTPS_SET_HOSTNAME_ERROR:
                case HTTPS_CONNECTION_ERROR:
                case HTTPS_ERROR:
                    PICO_ERROR_PRINT("%s (%s): aborting\n", https_status_str(e.status), lwip_errstr(connection_state.error));
                    in_progress = false;
                    err = tcpip_callback(tcpip_tls_abort, &connection_state);
                    if (err != ERR_OK) {
                        PICO_ERROR_PRINT("Error (%s) calling tcpip_callback(tcpip_tls_abort)\n", lwip_errstr(err));
                    }
                    vTaskDelay(pdMS_TO_TICKS(500)); // Allow tcpip task time to abort connection
                    if (connection_state.pcb) {
                        PICO_DEBUG_PRINT("Setting PCB to null after TLS abort.\n");
                        connection_state.pcb = NULL;
                    }
                    break;
                default:
                    PICO_ERROR_PRINT("%s (%d)\n", https_status_str(e.status), (int)e.status);
                    in_progress = false;
                    break;
                }
            }
        } else {
            PICO_ERROR_PRINT("tcpip_callback failed: %d\n", (int)err);
        }
        pico_tls_free_config(&connection_state);
        if (connection_state.q) {
            vQueueDelete(connection_state.q);
            connection_state.q = NULL;
        }
        // wait a minute before doing connection again,
        // but keep watchdog happy
        for (int i = 0; i < 6; i++) {
            https_task_wdog_flag = 0;
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
        PICO_DEBUG_PRINT("\ndo it again\n");
    }
    // Note that terminating this task will cause watchdog timeout
    // folllowed by reset.
    PICO_DEBUG_PRINT("\nTerminating https task\n");
    vTaskDelete(NULL);
}

/**
 * @brief FreeRTOS setup task: init Wi-Fi, start worker tasks, then exit.
 * @param arg Unused.
 */
static void main_task(void *arg) {
    (void)arg;

    dbg_keep_timer_running();

    // short delay for UART
    vTaskDelay(pdMS_TO_TICKS(200));

    if (cyw43_arch_init_with_country(CY43_COUNTRY_CODE)) {
        PICO_ERROR_PRINT("cyw43_arch_init() failed\n");
        const u32_t kreset_delay = DELAY_BEFORE_RESET;
        software_reset(kreset_delay);
    }

    cyw43_arch_enable_sta_mode();

    if (connect_to_wifi() == ERR_OK) {

        xTaskCreate(check_net_task, "check_net", 2048, NULL, tskIDLE_PRIORITY + 2, NULL);

        // allow net connection time to settle
        vTaskDelay(pdMS_TO_TICKS(10000));

        xTaskCreate(https_task, "https", 4096, NULL, tskIDLE_PRIORITY + 2, NULL);

        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

        xTaskCreate(blink_task, "blink", 1024, NULL, tskIDLE_PRIORITY + 1, NULL);

        xTaskCreate(watchdog_task, "watchdog", 1024, NULL, tskIDLE_PRIORITY + 1, NULL);
    }

    // Done with init; this task can exit
    vTaskDelete(NULL);
}

/**
 * @brief Program entry point.
 *
 * Initializes stdio, starts the main FreeRTOS task, then starts the scheduler.
 *
 * @return Never returns.
 */
int main(void) {
    stdio_init_all();

#if defined(PICO_DEBUG_MEMMON)
    xTaskCreate(memmon_task, "memmon", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);
#endif /* PICO_DEBUG_MEMMON */

    xTaskCreate(main_task, "main", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);

    vTaskStartScheduler();

    for (;;) tight_loop_contents();
}
