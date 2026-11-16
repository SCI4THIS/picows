/* SPDX-License-Identifier: GPL-3.0-only */
/* Drop-in for mbedtls_base64_encode(). Writes a trailing NUL when it fits. */
#ifndef PICOWS_MBEDTLS_BASE64_H
#define PICOWS_MBEDTLS_BASE64_H

#include <stddef.h>
#include <stdint.h>

static inline int mbedtls_base64_encode(unsigned char *dst, size_t dlen, size_t *olen,
                                        const unsigned char *src, size_t slen) {
  static const char tab[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t need = 4u * ((slen + 2u) / 3u);
  if (olen) {
    *olen = need;
  }
  if (!dst || dlen < need + 1u) {
    return -1;
  }
  size_t i = 0, o = 0;
  while (i + 3 <= slen) {
    uint32_t n = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8) | src[i + 2];
    dst[o++] = (unsigned char)tab[(n >> 18) & 63u];
    dst[o++] = (unsigned char)tab[(n >> 12) & 63u];
    dst[o++] = (unsigned char)tab[(n >> 6) & 63u];
    dst[o++] = (unsigned char)tab[n & 63u];
    i += 3;
  }
  if (i < slen) {
    uint32_t n = (uint32_t)src[i] << 16;
    if (i + 1 < slen) {
      n |= (uint32_t)src[i + 1] << 8;
    }
    dst[o++] = (unsigned char)tab[(n >> 18) & 63u];
    dst[o++] = (unsigned char)tab[(n >> 12) & 63u];
    dst[o++] = (i + 1 < slen) ? (unsigned char)tab[(n >> 6) & 63u] : (unsigned char)'=';
    dst[o++] = (unsigned char)'=';
  }
  dst[o] = 0;
  return 0;
}

#endif
