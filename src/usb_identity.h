/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PICOWS_USB_IDENTITY_H
#define PICOWS_USB_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* vid or pid 0 leaves the descriptor default. NULL strings leave the defaults. */
void picows_usb_set_identity(uint16_t vid, uint16_t pid, const char *manufacturer,
                             const char *product, const char *interface_name,
                             bool rndis_first);

#ifdef __cplusplus
}
#endif

#endif
