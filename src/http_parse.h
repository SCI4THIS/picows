/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PICOWS_HTTP_PARSE_H
#define PICOWS_HTTP_PARSE_H

#include <stddef.h>
#include <stdint.h>

/* Maximum total HTTP request header block size before "\r\n\r\n". */
#define PICOWS_HTTP_REQ_CAP 4096

/* Streaming parser state (~12 bytes instead of buffering all 4096 bytes). */
typedef struct picows_http_parser_st {
  size_t total_len;
  char prefix[6];     /* First 6 bytes of request line (to match "GET / ") */
  uint8_t prefix_len;
  uint8_t crlf_state; /* 0..4 tracking "\r\n\r\n" */
} picows_http_parser_t;

typedef enum picows_http_parse_status {
  PICOWS_HTTP_PARSE_NEED_MORE = 0,
  PICOWS_HTTP_PARSE_GET_ROOT,
  PICOWS_HTTP_PARSE_NOT_FOUND,
  PICOWS_HTTP_PARSE_TOO_LARGE
} picows_http_parse_status_t;

/*
 * Feed incoming TCP segment bytes directly without copying the full header block.
 * Headers may span calls.
 * GET_ROOT means the request line starts with "GET / " and "\r\n\r\n" was reached.
 * TOO_LARGE means the header block exceeded cap before "\r\n\r\n".
 */
picows_http_parse_status_t picows_http_parse_feed(picows_http_parser_t *parser, size_t cap,
                                                  const uint8_t *data, size_t data_len);

#endif
