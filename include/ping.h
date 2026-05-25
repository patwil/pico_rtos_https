/**
 * @file ping.h
 * @brief Ping helper API (lwIP-based).
 */

#ifndef LWIP_PING_H
#define LWIP_PING_H

#include "lwip/ip_addr.h"

/**
 * @brief Get the result of the most recently completed ping.
 * @return 1 on success/reply, 0 on timeout/failure (implementation-defined).
 */
int get_last_ping_result(void);

/**
 * @brief Initialize ping subsystem for a target address.
 * @param ping_addr Target IP address to ping.
 */
void ping_init(const ip_addr_t* ping_addr);
/**
 * @brief Stop/cleanup the ping subsystem.
 */
void ping_stop(void);

/**
 * @brief Send a ping request immediately to the configured target.
 */
void ping_send_now(void);

#endif /* LWIP_PING_H */
