/* SPDX-License-Identifier: GPL-3.0-only */
#include "http_parse.h"

#include <string.h>

picows_http_parse_status_t picows_http_parse_feed(char *buf, size_t cap, size_t *len,
                                                  const uint8_t *data, size_t data_len) {
  if (!buf || !len || cap < 5) {
    return PICOWS_HTTP_PARSE_TOO_LARGE;
  }
  size_t n = *len;
  if (n >= cap) {
    n = cap - 1;
  }
  if (!data && data_len > 0) {
    return PICOWS_HTTP_PARSE_TOO_LARGE;
  }
  for (size_t i = 0; i < data_len; i++) {
    if (n + 1 >= cap) {
      buf[n] = '\0';
      *len = n;
      return PICOWS_HTTP_PARSE_TOO_LARGE;
    }
    buf[n++] = (char)data[i];
    if (n >= 4 && buf[n - 4] == '\r' && buf[n - 3] == '\n' && buf[n - 2] == '\r' &&
        buf[n - 1] == '\n') {
      buf[n] = '\0';
      *len = n;
      if (strncmp(buf, "GET / ", 6) == 0) {
        return PICOWS_HTTP_PARSE_GET_ROOT;
      }
      return PICOWS_HTTP_PARSE_NOT_FOUND;
    }
  }
  buf[n] = '\0';
  *len = n;
  return PICOWS_HTTP_PARSE_NEED_MORE;
}
