/* SPDX-License-Identifier: GPL-3.0-only */
/* Drop-in for mbedtls_sha1(). The WebSocket handshake uses this once. */
#ifndef PICOWS_MBEDTLS_SHA1_H
#define PICOWS_MBEDTLS_SHA1_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline uint32_t picows_sha1_rotl(uint32_t x, int n) {
  return (x << n) | (x >> (32 - n));
}

static inline void picows_sha1_block(uint32_t h[5], const uint8_t blk[64]) {
  uint32_t w[80];
  for (int i = 0; i < 16; i++) {
    w[i] = ((uint32_t)blk[i * 4] << 24) | ((uint32_t)blk[i * 4 + 1] << 16) |
           ((uint32_t)blk[i * 4 + 2] << 8) | (uint32_t)blk[i * 4 + 3];
  }
  for (int i = 16; i < 80; i++) {
    w[i] = picows_sha1_rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  }
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f, k;
    if (i < 20) {
      f = (b & c) | ((~b) & d);
      k = 0x5A827999u;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1u;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDCu;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6u;
    }
    uint32_t temp = picows_sha1_rotl(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = picows_sha1_rotl(b, 30);
    b = a;
    a = temp;
  }
  h[0] += a;
  h[1] += b;
  h[2] += c;
  h[3] += d;
  h[4] += e;
}

static inline int mbedtls_sha1(const unsigned char *input, size_t ilen, unsigned char output[20]) {
  uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  const uint8_t *p = (const uint8_t *)input;
  size_t left = ilen;
  while (left >= 64) {
    picows_sha1_block(h, p);
    p += 64;
    left -= 64;
  }
  uint8_t block[64];
  memset(block, 0, sizeof(block));
  if (left) {
    memcpy(block, p, left);
  }
  block[left] = 0x80;
  if (left >= 56) {
    picows_sha1_block(h, block);
    memset(block, 0, sizeof(block));
  }
  uint64_t bits = (uint64_t)ilen * 8u;
  for (int i = 0; i < 8; i++) {
    block[63 - i] = (uint8_t)(bits >> (8 * i));
  }
  picows_sha1_block(h, block);
  for (int i = 0; i < 5; i++) {
    output[i * 4] = (unsigned char)(h[i] >> 24);
    output[i * 4 + 1] = (unsigned char)(h[i] >> 16);
    output[i * 4 + 2] = (unsigned char)(h[i] >> 8);
    output[i * 4 + 3] = (unsigned char)h[i];
  }
  return 0;
}

#endif
