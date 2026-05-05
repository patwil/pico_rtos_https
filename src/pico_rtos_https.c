#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "FreeRTOS.h"
#include "task.h"

#include "pico_ssid.h"
#include "pico_tls.h"
#include "lwip/altcp_tls.h"

#define HTTP_REQUEST \
  "GET " URL_REQUEST " HTTP/1.1\r\n" \
  "Host: " SERVER_HOSTNAME "\r\n" \
  "User-Agent: picow/1.0\r\n" \
  "Accept: */*\r\n" \
  "Accept-Encoding: identity\r\n" /* avoid gzip */ \
  "Connection: close\r\n" \
  "\r\n"


#if defined(PICO_DEBUG_KEEP_TIMER_RUNNING)
#include "hardware/structs/timer.h"
static inline void dbg_keep_timer_running(void) { timer_hw->dbgpause = 0; }
#else
static inline void dbg_keep_timer_running(void) { }
#endif

#if defined(PICO_DEBUG_MEMMON)
static void dump_task_stacks(void) {
    char buf[512];
    vTaskList(buf);
    printf("Task          State   Prio     Stack   Num      Affinity\n%s\n", buf);

    // Also dump heap_4 minimum ever free:
    printf("heap: free=%u  min_ever_free=%u\n",
           (unsigned)xPortGetFreeHeapSize(),
           (unsigned)xPortGetMinimumEverFreeHeapSize());
}

static void memmon_task(void *p) {
    (void)p;
    for (;;) {
        dump_task_stacks();
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
#endif /* PICO_DEBUG_MEMMON */

static void blink_task(void *param) {
    (void)param;
    bool led = false;

    for (;;) {
        led = !led;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
        //printf("blink: %d (tick=%lu)\n", led ? 1 : 0, (unsigned long)xTaskGetTickCount());
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

#if 0
static void https_task(void *param) {
/*    static const uint8_t cert_ok[] = TLS_ROOT_CERT_OK;
    static EXAMPLE_HTTP_REQUEST_T req = {0};
    req.hostname = HOST;
    req.url = URL_REQUEST;
    req.headers_fn = http_client_header_print_fn;
    req.recv_fn = http_client_receive_print_fn;
    req.tls_config = altcp_tls_create_config_client(cert_ok, sizeof(cert_ok));
    printf("at line: %s:%d\n", __FILE__, __LINE__);
    int pass = http_client_request_sync(cyw43_arch_async_context(), &req);
    printf("at line: %s:%d\n", __FILE__, __LINE__);
    altcp_tls_free_config(req.tls_config);
    printf("at line: %s:%d\n", __FILE__, __LINE__);
    if (pass != 0) {
        printf("test failed\n");
    }
*/
    // Use a do...once so we can break out of it if there's an error.
    // I avoid GOTOs whenever I can.
    do {
        // Resolve server hostname
        ip_addr_t ipaddr;
        char* char_ipaddr;
        const char* server_hostname = SERVER_HOSTNAME;
        const int server_portnum = LWIP_IANA_PORT_HTTPS;
        const char *http_request = HTTP_REQUEST;
        printf("Resolving %s\n", server_hostname);
        if (!resolve_hostname(server_hostname, &ipaddr)) {
            printf("Failed to resolve %s\n", server_hostname);
            break;
        }
        cyw43_arch_lwip_begin();
        char_ipaddr = ipaddr_ntoa(&ipaddr);
        cyw43_arch_lwip_end();
        printf("Resolved %s (%s)\n", server_hostname, char_ipaddr);

#ifdef MBEDTLS_DEBUG_C
        mbedtls_debug_set_threshold(HTTPS_MBEDTLS_DEBUG_LEVEL);
#else
        #error "MBEDTLS_DEBUG_C not set"
#endif //MBEDTLS_DEBUG_C

        // Establish TCP + TLS connection with server
        struct altcp_pcb* pcb = NULL;
        printf("Connecting to https://%s:%d\n", char_ipaddr, server_portnum);
        if (!connect_to_host(&ipaddr, server_hostname, server_portnum, &pcb)) {
            printf("Failed to connect to https://%s:%d\n", char_ipaddr, server_portnum);
            break;
        }
        printf("Connected to https://%s:%d\n", char_ipaddr, server_portnum);

        // Send HTTP request to server
        printf("Sending request:\n\"%s\"\n", http_request);
        if(!send_request(http_request, pcb)){
            printf("Failed to send request\n");
            // Free connection configuration, connection callback argument and connection PCB
            altcp_free_config(((altcp_cb_arg_t*)(pcb->arg))->config);
            altcp_free_arg((altcp_cb_arg_t*)(pcb->arg));
            altcp_free_pcb(pcb);
            break;
        }
        printf("Request sent\n");

        // Await HTTP response
        printf("Awaiting response\n");
        vTaskDelay(pdMS_TO_TICKS(5000));
        printf("Awaited response\n");
    } while (0);

    // Done with http test; this task can exit
    vTaskDelete(NULL);
}
#endif

static int connect_to_wifi(void) {
    const int timeout_ms = 30000;
    const int n_attempts = 5;
    int attempt = 0;
    int rc = ERR_OK;
    do {
        printf("Connecting to \"%s\"...\n", PICO_WIFI_SSID);
        rc = cyw43_arch_wifi_connect_timeout_ms(PICO_WIFI_SSID,
                                                PICO_WIFI_PASSWORD,
                                                CYW43_AUTH_WPA2_AES_PSK,
                                                timeout_ms);
        if (rc == ERR_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (++attempt >= n_attempts) {
            printf("failed to connect after %1d attempts\n", attempt);
            return rc;
        }
    } while (true);
    return rc;
}

static void main_task(void *param) {
    (void)param;

    dbg_keep_timer_running();

    // If you want a short delay for UART, use vTaskDelay inside a task
    vTaskDelay(pdMS_TO_TICKS(200));

    if (cyw43_arch_init_with_country(CY43_COUNTRY_CODE)) {
        printf("cyw43_arch_init() failed\n");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    cyw43_arch_enable_sta_mode();

    if (connect_to_wifi() == ERR_OK) {

        //TaskHandle_t http_handle = NULL;
        //xTaskCreate(http_task, "http", 4096, NULL, tskIDLE_PRIORITY + 3, &http_handle);
        #if (configNUMBER_OF_CORES > 1)
            // FreeRTOS SMP API: pin to core0 so CYW43 init/IRQ context stays consistent
            //vTaskCoreAffinitySet(http_handle, 0x01);
        #endif

        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

        xTaskCreate(blink_task, "blink", 512, NULL, tskIDLE_PRIORITY + 1, NULL);
    }

    // Done with init; this task can exit
    vTaskDelete(NULL);
}

int main(void) {
    stdio_init_all();

#if defined(PICO_DEBUG_MEMMON)
    TaskHandle_t memmon_handle = NULL;
    xTaskCreate(memmon_task, "memmon", 4096, NULL, tskIDLE_PRIORITY + 2, &memmon_handle);
#endif /* PICO_DEBUG_MEMMON */

    TaskHandle_t main_handle = NULL;
    xTaskCreate(main_task, "main", 4096, NULL, tskIDLE_PRIORITY + 3, &main_handle);

#if (configNUMBER_OF_CORES > 1)
    // FreeRTOS SMP API: pin to core0 so CYW43 init/IRQ context stays consistent
    //vTaskCoreAffinitySet(main_handle, 0x01);
#endif

    vTaskStartScheduler();
    for (;;) tight_loop_contents();
}
