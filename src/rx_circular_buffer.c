/**
 * @file rx_circular_buffer.c
 * @brief Single-producer/single-consumer circular buffer used to store received TCP data.
 */

#include "rx_circular_buffer.h"

/**
 * @brief Backing storage for the receive circular buffer.
 *
 * The buffer is written from the lwIP TCP/IP thread and read from the HTTPS task.
 */
u8_t rx_buffer[CIRC_BUFF_SZ];

// These volatile vars should be safe enough for concurrent
// read and write tasks as long asthey are accessed using atomic
// operations, e.g. 
// temp_idx = read_idx
// or
// write_idx = new_idx
//
// Do not use more complicated operations, e.g. read_idx++
//
static volatile int read_idx;

static volatile int write_idx;

static volatile int is_full;

/**
 * @brief Initialize the circular buffer state.
 * 
 * Detailed API docs are on the declaration in
 * rx_circular_buffer.h.
 */
void circ_buffer_init(void) {
    read_idx = 0;
    write_idx = 0;
    is_full = 0;
}

/**
 * @brief Write an lwIP pbuf chain into the circular buffer.
 *
 * This function is intended to be called from the lwIP
 * TCP/IP thread. It performs an all-or-nothing write: if the
 * entire pbuf chain cannot fit, no indices are updated and
 * ERR_BUF is returned.
 *
 * Detailed API docs are on the declaration in
 * rx_circular_buffer.h.
 */
err_t circ_buff_write(struct pbuf* p) {
    // local copies of indexes in case we have concurrent read and write
    int r_idx = read_idx;
    int w_idx = write_idx;
    int start_w_idx = w_idx;

    // check there is enough free buffer space
    if (is_full || (p->tot_len > CIRC_BUFF_SZ)) {
        return ERR_BUF;
    }
    if ((w_idx > r_idx) && (p->tot_len > (w_idx - r_idx))) {
        return ERR_BUF;
    }
    if ((w_idx < r_idx) && (p->tot_len > (CIRC_BUFF_SZ + w_idx - r_idx))) {
        return ERR_BUF;
    }

    for (const struct pbuf *q = p; q != NULL; q = q->next) {
        const u8_t* src = (const u8_t*)q->payload;

        for (u16_t i = 0; i < q->len; i++) {
            rx_buffer[(i + w_idx) % CIRC_BUFF_SZ] = src[i];
        }
        w_idx = (w_idx + q->len) % CIRC_BUFF_SZ;
    }

    // Check how much have we written to buffer.
    if (((w_idx + CIRC_BUFF_SZ - start_w_idx) % CIRC_BUFF_SZ) != p->tot_len) {
        // partial write not acceptable; it's all or nothing.
        // Don't update "real" indices.
        return ERR_BUF;
    }

    // Are we full yet?
    if (w_idx == r_idx) {
        // we may need to protect is_full from concurrent/conflicting updates,
        // but for now we'll take our chances.
        is_full = 1;
    }

    // update the "real" index
    write_idx = w_idx;

    return ERR_OK;
}

/**
 * @brief Read up to @p plen bytes from the circular buffer.
 *
 * This function is intended to be called from a consumer task
 * context.
 *
 * Detailed API docs are on the declaration in
 * rx_circular_buffer.h.
 */
size_t circ_buff_read(u8_t* p, size_t plen) {
    // local copies of indexes in case we have concurrent read and write
    int r_idx = read_idx;
    int w_idx = write_idx;

    // check if there's anything to read
    if ((plen == 0) || ((r_idx == w_idx) && !is_full)) {
        return 0;
    }

    // We'll read the lesser of plen and number of bytes in buffer
    size_t n_bytes = 0;

    if (r_idx == w_idx) {
       n_bytes = CIRC_BUFF_SZ;
    } else {
        n_bytes = (w_idx + CIRC_BUFF_SZ - r_idx) % CIRC_BUFF_SZ;
    }

    n_bytes = (n_bytes < plen) ? n_bytes : plen;

    for (int i = 0; i < n_bytes; i++) {
        p[i] = rx_buffer[r_idx];
        r_idx = (r_idx + 1) % CIRC_BUFF_SZ;
    }

    // we may need to protect is_full from concurrent/conflicting updates,
    // but for now we'll take our chances.
    is_full = 0;

    // update the "real" index
    read_idx = r_idx;

    return n_bytes;
}


#ifdef PICO_DEBUG
/**
 * @brief Print buffer read/write indices and full/empty state (debug builds).
 * 
 * Detailed API docs are on the declaration in
 * rx_circular_buffer.h.
*/
void print_circ_buff_info(void) {
    int r_idx = read_idx;
    int w_idx = write_idx;

    printf("rx_circ_buff  read index=%1d  write index=%1d  (%s)\n",
           r_idx, w_idx, (r_idx == w_idx)?(is_full?"full":"empty"):"not full");
}
#else
void print_circ_buff_info(void) { }
#endif


