/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PICOWS_CORE_INTERNAL_H
#define PICOWS_CORE_INTERNAL_H

#include "picows/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Internal initialization function for the USB network + lwIP stack.
 * The first successful call owns IP, netmask, gateway, MAC, DNS name, DHCP,
 * and USB identity. A later call with a different configuration returns false.
 * Returns false if USB or lwIP setup fails. If args is NULL, defaults are used.
 * The application starts clocks and stdio before this runs.
 */
bool picows_core_init(const picows_args_t *args);

/**
 * Check if picows_core_init has already been called.
 */
bool picows_core_is_initialized(void);

/**
 * Get the configured default port from picows_core_init (default 80).
 */
uint16_t picows_core_get_port(void);

/**
 * Register a poll callback (used by picows_ws to hook into picows_poll()).
 */
typedef void (*picows_poll_fn_t)(void);
void picows_core_register_poll_fn(picows_poll_fn_t fn);

#ifdef __cplusplus
}
#endif

#endif /* PICOWS_CORE_INTERNAL_H */
