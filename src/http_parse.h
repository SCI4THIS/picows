/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PICOWS_HTTP_PARSE_H
#define PICOWS_HTTP_PARSE_H

#include <stddef.h>
#include <stdint.h>

/* Room for a request line and a few headers, including the terminating NUL. */
#define PICOWS_HTTP_REQ_CAP 512

typedef enum picows_http_parse_status {
  PICOWS_HTTP_PARSE_NEED_MORE = 0,
  PICOWS_HTTP_PARSE_GET_ROOT,
  PICOWS_HTTP_PARSE_NOT_FOUND,
  PICOWS_HTTP_PARSE_TOO_LARGE
} picows_http_parse_status_t;

/*
 * Append data to buf. Headers may span calls. *len is the stored byte count.
 * GET_ROOT means the request line starts with "GET / ".
 * TOO_LARGE means the header block exceeded cap before "\r\n\r\n".
 */
picows_http_parse_status_t picows_http_parse_feed(char *buf, size_t cap, size_t *len,
                                                  const uint8_t *data, size_t data_len);

#endif
