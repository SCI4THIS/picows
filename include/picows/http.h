#ifndef PICOWS_HTTP_H
#define PICOWS_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "picows/core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct picows_http_st picows_http_t;

typedef struct picows_http_args_st {
  picows_args_t core_args;
  const uint8_t *buf;
  size_t len;
  bool is_gzipped;
} picows_http_args_t;

/**
 * Two-phase constructor for the picows HTTP server.
 *
 * Phase 1 (query allocation size):
 *   size_t siz = picows_http(&args, NULL);
 *
 * Phase 2 (initialize allocated instance and start server):
 *   picows_http_t *hs = (picows_http_t *)malloc(siz);
 *   picows_http(&args, hs);
 *
 * Automatically calls internal picows_core_init(&args->core_args) on phase 2.
 *
 * @param args  Pointer to HTTP server configuration arguments.
 * @param hs    Pointer to caller-allocated picows_http_t memory, or NULL to query size.
 * @return      Size in bytes required for picows_http_t (or 0 on error).
 */
size_t picows_http(const picows_http_args_t *args, picows_http_t *hs);

/**
 * Return the configuration arguments associated with an initialized HTTP server.
 *
 * @param srv   Initialized picows_http_t instance.
 * @return      Pointer to the stored picows_http_args_t.
 */
const picows_http_args_t *picows_http_args(const picows_http_t *srv);

/**
 * Yield the port bound by the standalone HTTP server so that a WebSocket server
 * sharing the same port can bind to it and serve both HTTP GET / and WebSocket upgrades.
 *
 * @param srv   Initialized picows_http_t instance.
 */
void picows_http_yield_port(picows_http_t *srv);

#ifdef __cplusplus
}
#endif

#endif /* PICOWS_HTTP_H */
