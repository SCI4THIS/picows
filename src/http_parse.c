/* SPDX-License-Identifier: GPL-3.0-only */
#include "http_parse.h"

#include <string.h>

picows_http_parse_status_t picows_http_parse_feed(picows_http_parser_t *parser, size_t cap,
                                                  const uint8_t *data, size_t data_len) {
  if (!parser || cap < 5) {
    return PICOWS_HTTP_PARSE_TOO_LARGE;
  }
  if (!data && data_len > 0) {
    return PICOWS_HTTP_PARSE_TOO_LARGE;
  }

  static const char term[4] = {'\r', '\n', '\r', '\n'};

  for (size_t i = 0; i < data_len; i++) {
    if (parser->total_len + 1 >= cap) {
      return PICOWS_HTTP_PARSE_TOO_LARGE;
    }
    char c = (char)data[i];
    parser->total_len++;

    if (parser->prefix_len < sizeof(parser->prefix)) {
      parser->prefix[parser->prefix_len++] = c;
    }

    if (c == term[parser->crlf_state]) {
      parser->crlf_state++;
      if (parser->crlf_state == 4) {
        if (parser->prefix_len == 6 && memcmp(parser->prefix, "GET / ", 6) == 0) {
          return PICOWS_HTTP_PARSE_GET_ROOT;
        }
        return PICOWS_HTTP_PARSE_NOT_FOUND;
      }
    } else if (c == '\r') {
      parser->crlf_state = 1;
    } else {
      parser->crlf_state = 0;
    }
  }

  return PICOWS_HTTP_PARSE_NEED_MORE;
}
