/* SPDX-License-Identifier: GPL-3.0-only */
#include "http_parse.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void check(int cond, const char *what, int line) {
  if (!cond) {
    printf("fail:%d %s\n", line, what);
    fails++;
  }
}

#define CHECK(c) check((c), #c, __LINE__)

static void feed_str(picows_http_parser_t *parser, const char *s, picows_http_parse_status_t expect) {
  picows_http_parse_status_t st =
      picows_http_parse_feed(parser, PICOWS_HTTP_REQ_CAP, (const uint8_t *)s, strlen(s));
  CHECK(st == expect);
}

static void test_split_headers(void) {
  picows_http_parser_t parser = {0};
  feed_str(&parser, "GET / HT", PICOWS_HTTP_PARSE_NEED_MORE);
  feed_str(&parser, "TP/1.1\r\nHost: x\r\n", PICOWS_HTTP_PARSE_NEED_MORE);
  feed_str(&parser, "\r\n", PICOWS_HTTP_PARSE_GET_ROOT);
}

static void test_terminator_split(void) {
  picows_http_parser_t parser = {0};
  feed_str(&parser, "GET / HTTP/1.1\r\n\r", PICOWS_HTTP_PARSE_NEED_MORE);
  feed_str(&parser, "\n", PICOWS_HTTP_PARSE_GET_ROOT);
  /* Also test "\r\r\n\r\n" where a double CR resets crlf_state to 1 */
  memset(&parser, 0, sizeof(parser));
  feed_str(&parser, "GET / HTTP/1.1\r\r\n\r\n", PICOWS_HTTP_PARSE_GET_ROOT);
}

static void test_not_found(void) {
  picows_http_parser_t parser = {0};
  feed_str(&parser, "POST / HTTP/1.1\r\n\r\n", PICOWS_HTTP_PARSE_NOT_FOUND);
  memset(&parser, 0, sizeof(parser));
  feed_str(&parser, "GET /other HTTP/1.1\r\n\r\n", PICOWS_HTTP_PARSE_NOT_FOUND);
}

static void test_too_large(void) {
  picows_http_parser_t parser = {0};
  char chunk[64];
  memset(chunk, 'A', sizeof(chunk));
  picows_http_parse_status_t st = PICOWS_HTTP_PARSE_NEED_MORE;
  while (st == PICOWS_HTTP_PARSE_NEED_MORE) {
    st = picows_http_parse_feed(&parser, PICOWS_HTTP_REQ_CAP, (const uint8_t *)chunk,
                                sizeof(chunk));
  }
  CHECK(st == PICOWS_HTTP_PARSE_TOO_LARGE);
}

static void test_ws_accept(void) {
  const char *key = "dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  unsigned char sha[20];
  CHECK(mbedtls_sha1((const unsigned char *)key, strlen(key), sha) == 0);
  unsigned char b64[29];
  size_t olen = 0;
  CHECK(mbedtls_base64_encode(b64, sizeof(b64), &olen, sha, 20) == 0);
  CHECK(olen == 28);
  CHECK(strcmp((char *)b64, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);
  unsigned char empty[20];
  CHECK(mbedtls_sha1((const unsigned char *)"", 0, empty) == 0);
  static const unsigned char empty_sha[20] = {0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
                                              0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09};
  CHECK(memcmp(empty, empty_sha, 20) == 0);
}

int main(void) {
  test_split_headers();
  test_terminator_split();
  test_not_found();
  test_too_large();
  test_ws_accept();
  if (fails) {
    printf("%d failed\n", fails);
    return 1;
  }
  printf("ok\n");
  return 0;
}
