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
 * Send a null-terminated TEXT WebSocket message to a specific connection.
 */
bool picows_ws_send_text(picows_ws_t *srv, uint32_t conn_id, const char *text);

/**
 * Send a BINARY WebSocket message to a specific connection.
 */
bool picows_ws_send_binary(picows_ws_t *srv, uint32_t conn_id, const void *data, size_t len);

/**
 * Broadcast a null-terminated TEXT WebSocket message to all connected clients.
 */
bool picows_ws_broadcast_text(picows_ws_t *srv, const char *text);

/**
 * Broadcast a BINARY WebSocket message to all connected clients.
 */
bool picows_ws_broadcast_binary(picows_ws_t *srv, const void *data, size_t len);

/**
 * Close a WebSocket connection.
 */
bool picows_ws_close(picows_ws_t *srv, uint32_t conn_id);

#ifdef __cplusplus
}
#endif

#endif /* PICOWS_WS_H */
