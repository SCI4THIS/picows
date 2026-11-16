/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PICOWS_WS_H
#define PICOWS_WS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "picows/core.h"
#include "picows/http.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct picows_ws_st picows_ws_t;

typedef void (*picows_ws_connect_cb_t)(picows_ws_t *ws, uint32_t conn_id, void *user_data);
typedef void (*picows_ws_close_cb_t)(picows_ws_t *ws, uint32_t conn_id, void *user_data);
typedef void (*picows_ws_message_cb_t)(picows_ws_t *ws, uint32_t conn_id, const void *data, size_t len, void *user_data);

typedef struct picows_ws_args_st {
  picows_args_t core_args;
  picows_http_t *http;    /* Optional HTTP server sharing this port */
  /**
   * Optional raw page served on GET / by the WebSocket listener.
   * Ignored when http is set; that server's buffer is used instead.
   */
  const uint8_t *page;
  size_t page_len;
  bool page_gzipped;
  uint32_t max_conns;     /* 0 defaults to 1 */
  bool tcp_nodelay;       /* true disables Nagle's algorithm */
  picows_ws_connect_cb_t on_connect;
  picows_ws_close_cb_t on_close;
  picows_ws_message_cb_t on_message;
  void *user_data;
} picows_ws_args_t;

/**
 * Two-phase constructor for the picows WebSocket server.
 *
 * Phase 1 (query allocation size):
 *   size_t siz = picows_ws(&args, NULL);
 *
 * Phase 2 (initialize allocated instance and start server):
 *   picows_ws_t *ws = (picows_ws_t *)malloc(siz);
 *   picows_ws(&args, ws);
 *
 * Automatically calls internal picows_core_init(&args->core_args) on phase 2.
 *
 * @param args  Pointer to WebSocket server configuration arguments.
 * @param ws    Pointer to caller-allocated picows_ws_t memory, or NULL to query size.
 * @return      Size in bytes required for picows_ws_t (or 0 on error).
 */
size_t picows_ws(const picows_ws_args_t *args, picows_ws_t *ws);

/**
 * Return the configuration arguments associated with an initialized WebSocket server.
 *
 * @param srv   Initialized picows_ws_t instance.
 * @return      Pointer to the stored picows_ws_args_t.
 */
const picows_ws_args_t *picows_ws_args(const picows_ws_t *srv);

/**
 * Poll incoming WebSocket messages for a server instance (also called automatically by picows_poll()).
 */
void picows_ws_poll(picows_ws_t *srv);

/**
 * Queue one null-terminated text WebSocket message on a connection.
 *
 * False means the frame was not queued: the server is not running, or
 * tcp_write failed because the send buffer, the TCP segment pool, or the
 * lwIP heap could not hold it. True means the frame is on that connection's
 * TCP send queue. A busy USB transmit still returns true; picows_poll()
 * retries the segment. A later call with the same bytes queues a second frame.
 *
 * The whole frame has to fit in the free send buffer. TCP_SND_BUF is
 * 4 * TCP_MSS (5840 bytes) of queued TCP payload, including bytes still
 * waiting for ACK. The server adds a 2-byte header when the payload is
 * under 126 bytes, and a 4-byte header when it is under 65536 bytes.
 * The copy also has to fit in the lwIP heap (MEM_SIZE is 1600 bytes), so a
 * shorter frame can fail while other data is queued. An 8192-byte payload
 * is larger than TCP_SND_BUF and does not fit.
 */
bool picows_ws_send_text(picows_ws_t *srv, uint32_t conn_id, const char *text);

/**
 * Queue one binary WebSocket message. Same result and size rules as
 * picows_ws_send_text().
 */
bool picows_ws_send_binary(picows_ws_t *srv, uint32_t conn_id, const void *data, size_t len);

/**
 * Queue one text WebSocket message on every connection. Same result and
 * size rules as picows_ws_send_text(), applied to each connection.
 */
bool picows_ws_broadcast_text(picows_ws_t *srv, const char *text);

/**
 * Queue one binary WebSocket message on every connection. Same result and
 * size rules as picows_ws_send_text(), applied to each connection.
 */
bool picows_ws_broadcast_binary(picows_ws_t *srv, const void *data, size_t len);

/**
 * Close a WebSocket connection.
 */
bool picows_ws_close(picows_ws_t *srv, uint32_t conn_id);

/**
 * Stop the listener, abort its connections, and destroy the server object.
 * The caller frees the memory it allocated for srv after this returns.
 * At most two servers are accepted; picows_ws() returns 0 for a third.
 * max_conns above PICOWS_MAX_WS_CONNS (4) also returns 0.
 *
 * A call from a connect, close, or message callback queues srv. picows_poll()
 * or picows_ws_poll() destroys it once the WebSocket stack has returned from
 * that callback. Do not free srv until that poll call returns.
 */
void picows_ws_close_server(picows_ws_t *srv);

#ifdef __cplusplus
}
#endif

#endif /* PICOWS_WS_H */
