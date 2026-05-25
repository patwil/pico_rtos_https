/**
 * @file rx_circular_buffer.h
 * @brief Circular buffer API for received TCP data.
 */

#pragma once

#include <stddef.h>
#include "lwip/err.h"
#include "lwip/arch.h"
#include "lwip/altcp_tls.h"

/** @brief Size of the receive circular buffer in bytes. */
#define CIRC_BUFF_SZ        (16*1024)

void circ_buffer_init(void);

/**
 * @brief Write an lwIP pbuf chain into the circular buffer.
 * @param p pbuf chain to copy.
 * @return ERR_OK on success, ERR_BUF if insufficient space.
 */
err_t circ_buff_write(struct pbuf* p);

/**
 * @brief Read bytes from the circular buffer.
 * @param[out] p Destination buffer.
 * @param plen Maximum bytes to read.
 * @return Number of bytes actually read.
 */
size_t circ_buff_read(u8_t* p, size_t plen);

/**
 * @brief Print buffer indices/state (debug builds).
 */
void print_circ_buff_info(void);


