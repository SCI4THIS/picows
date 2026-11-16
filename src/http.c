/* SPDX-License-Identifier: GPL-3.0-only */
#include "picows/http.h"
#include "core_internal.h"
#include "http_parse.h"

#include <stdio.h>
#include <string.h>

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#ifndef PICOWS_MAX_HTTP_CONNS
#define PICOWS_MAX_HTTP_CONNS 4
#endif

/* tcp_poll interval is in TCP slow-timer ticks (500 ms). 2 * 10 = 10 s. */
#define HTTP_POLL_INTERVAL 2
#define HTTP_HEADER_IDLE_POLLS 10

typedef struct http_conn_state_st {
  struct tcp_pcb *pcb;
  picows_http_t *srv;
  picows_http_parser_t parser;
  bool headers_done;
  uint8_t idle_polls;
  char hdr[192];
  size_t hdr_len;
  size_t hdr_queued;
  size_t body_len;
  size_t body_queued;
  size_t acked;
  size_t response_total;
  bool sending;
} http_conn_state_t;

struct picows_http_st {
  picows_http_args_t args;
  uint16_t port;
  struct tcp_pcb *listen_pcb;
  http_conn_state_t conns[PICOWS_MAX_HTTP_CONNS];
  bool initialized;
};

static const char http_404[] =
    "HTTP/1.1 404 Not Found\r\n"
    "Connection: close\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 10\r\n"
    "\r\n"
    "Not Found\n";

static const char http_400[] =
    "HTTP/1.1 400 Bad Request\r\n"
    "Connection: close\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 12\r\n"
    "\r\n"
    "Bad Request\n";

static http_conn_state_t *alloc_http_conn(picows_http_t *srv, struct tcp_pcb *pcb) {
  for (size_t i = 0; i < PICOWS_MAX_HTTP_CONNS; i++) {
    if (srv->conns[i].pcb == NULL) {
      memset(&srv->conns[i], 0, sizeof(srv->conns[i]));
      srv->conns[i].pcb = pcb;
      srv->conns[i].srv = srv;
      return &srv->conns[i];
    }
  }
  return NULL;
}

static void free_http_conn(http_conn_state_t *st) {
  if (st) {
    memset(st, 0, sizeof(*st));
  }
}

static void http_detach(struct tcp_pcb *pcb) {
  tcp_arg(pcb, NULL);
  tcp_err(pcb, NULL);
  tcp_recv(pcb, NULL);
  tcp_sent(pcb, NULL);
  tcp_poll(pcb, NULL, 0);
}

static err_t queue_more(http_conn_state_t *st) {
  if (!st || !st->sending || !st->pcb) {
    return ERR_OK;
  }
  while (st->hdr_queued < st->hdr_len) {
    u16_t n = (u16_t)(st->hdr_len - st->hdr_queued);
    err_t err = tcp_write(st->pcb, st->hdr + st->hdr_queued, n, TCP_WRITE_FLAG_COPY);
    if (err == ERR_MEM) {
      tcp_output(st->pcb);
      return ERR_OK;
    }
    if (err != ERR_OK) {
      return err;
    }
    st->hdr_queued += n;
  }

  const uint8_t *body = st->srv ? st->srv->args.buf : NULL;
  const size_t chunk_size = 512;
  while (st->body_queued < st->body_len) {
    size_t remaining = st->body_len - st->body_queued;
    u16_t n = (u16_t)(remaining < chunk_size ? remaining : chunk_size);
    err_t err = tcp_write(st->pcb, body + st->body_queued, n, TCP_WRITE_FLAG_COPY);
    if (err == ERR_MEM) {
      tcp_output(st->pcb);
      return ERR_OK;
    }
    if (err != ERR_OK) {
      return err;
    }
    st->body_queued += n;
  }
  tcp_output(st->pcb);
  return ERR_OK;
}

static bool response_fully_queued(const http_conn_state_t *st) {
  return st->hdr_queued >= st->hdr_len && st->body_queued >= st->body_len;
}

static err_t finish_if_acked(http_conn_state_t *st, struct tcp_pcb *pcb) {
  if (!st->sending || !response_fully_queued(st) || st->acked < st->response_total) {
    return ERR_OK;
  }
  http_detach(pcb);
  free_http_conn(st);
  if (tcp_close(pcb) != ERR_OK) {
    tcp_abort(pcb);
    return ERR_ABRT;
  }
  return ERR_OK;
}

static err_t abort_conn(http_conn_state_t *st, struct tcp_pcb *pcb) {
  http_detach(pcb);
  free_http_conn(st);
  tcp_abort(pcb);
  return ERR_ABRT;
}

static bool begin_bytes(http_conn_state_t *st, const char *bytes, size_t n) {
  if (n > sizeof(st->hdr)) {
    return false;
  }
  memcpy(st->hdr, bytes, n);
  st->hdr_len = n;
  st->hdr_queued = 0;
  st->body_len = 0;
  st->body_queued = 0;
  st->acked = 0;
  st->response_total = n;
  st->sending = true;
  st->headers_done = true;
  return queue_more(st) == ERR_OK;
}

static err_t http_on_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
  http_conn_state_t *st = (http_conn_state_t *)arg;
  if (!st) {
    return ERR_OK;
  }
  st->acked += len;
  if (st->sending) {
    err_t err = queue_more(st);
    if (err != ERR_OK) {
      return abort_conn(st, pcb);
    }
  }
  return finish_if_acked(st, pcb);
}

static void http_on_err(void *arg, err_t err) {
  (void)err;
  http_conn_state_t *st = (http_conn_state_t *)arg;
  if (st) {
    free_http_conn(st);
  }
}

static err_t http_on_poll(void *arg, struct tcp_pcb *pcb) {
  http_conn_state_t *st = (http_conn_state_t *)arg;
  if (!st) {
    return ERR_OK;
  }
  if (!st->headers_done) {
    if (++st->idle_polls >= HTTP_HEADER_IDLE_POLLS) {
      return abort_conn(st, pcb);
    }
    return ERR_OK;
  }
  if (st->sending) {
    err_t err = queue_more(st);
    if (err != ERR_OK) {
      return abort_conn(st, pcb);
    }
    return finish_if_acked(st, pcb);
  }
  return ERR_OK;
}

static err_t http_on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *pb, err_t err) {
  (void)err;
  http_conn_state_t *st = (http_conn_state_t *)arg;
  if (!st || !st->srv) {
    if (pb) {
      pbuf_free(pb);
    }
    return ERR_OK;
  }
  if (!pb) {
    http_detach(pcb);
    free_http_conn(st);
    if (tcp_close(pcb) != ERR_OK) {
      tcp_abort(pcb);
      return ERR_ABRT;
    }
    return ERR_OK;
  }

  st->idle_polls = 0;
  picows_http_parse_status_t parsed = PICOWS_HTTP_PARSE_NEED_MORE;
  if (!st->headers_done) {
    for (struct pbuf *q = pb; q != NULL; q = q->next) {
      parsed = picows_http_parse_feed(&st->parser, PICOWS_HTTP_REQ_CAP, (const uint8_t *)q->payload,
                                      q->len);
      if (parsed != PICOWS_HTTP_PARSE_NEED_MORE) {
        break;
      }
    }
  }

  tcp_recved(pcb, pb->tot_len);
  pbuf_free(pb);

  if (st->headers_done || parsed == PICOWS_HTTP_PARSE_NEED_MORE) {
    return ERR_OK;
  }

  if (parsed == PICOWS_HTTP_PARSE_TOO_LARGE) {
    if (!begin_bytes(st, http_400, sizeof(http_400) - 1)) {
      return abort_conn(st, pcb);
    }
    return finish_if_acked(st, pcb);
  }

  const picows_http_args_t *hargs = &st->srv->args;
  if (parsed != PICOWS_HTTP_PARSE_GET_ROOT || !hargs->buf || hargs->len == 0) {
    if (!begin_bytes(st, http_404, sizeof(http_404) - 1)) {
      return abort_conn(st, pcb);
    }
    return finish_if_acked(st, pcb);
  }

  int hdr_len = snprintf(st->hdr, sizeof(st->hdr),
                         "HTTP/1.1 200 OK\r\n"
                         "Connection: close\r\n"
                         "Content-Type: text/html\r\n"
                         "%s"
                         "Content-Length: %u\r\n"
                         "\r\n",
                         hargs->is_gzipped ? "Content-Encoding: gzip\r\n" : "",
                         (unsigned int)hargs->len);
  if (hdr_len <= 0 || (size_t)hdr_len >= sizeof(st->hdr)) {
    return abort_conn(st, pcb);
  }
  st->hdr_len = (size_t)hdr_len;
  st->hdr_queued = 0;
  st->body_len = hargs->len;
  st->body_queued = 0;
  st->acked = 0;
  st->response_total = (size_t)hdr_len + hargs->len;
  st->sending = true;
  st->headers_done = true;
  if (queue_more(st) != ERR_OK) {
    return abort_conn(st, pcb);
  }
  return finish_if_acked(st, pcb);
}

static err_t http_on_accept(void *arg, struct tcp_pcb *new_pcb, err_t err) {
  picows_http_t *srv = (picows_http_t *)arg;
  if (!new_pcb || err != ERR_OK || !srv) {
    if (new_pcb) {
      tcp_abort(new_pcb);
    }
    return ERR_ABRT;
  }
  http_conn_state_t *st = alloc_http_conn(srv, new_pcb);
  if (!st) {
    tcp_abort(new_pcb);
    return ERR_ABRT;
  }
  tcp_arg(new_pcb, st);
  tcp_err(new_pcb, http_on_err);
  tcp_recv(new_pcb, http_on_recv);
  tcp_sent(new_pcb, http_on_sent);
  tcp_poll(new_pcb, http_on_poll, HTTP_POLL_INTERVAL);
  return ERR_OK;
}

static void close_pcb(struct tcp_pcb *pcb) {
  if (!pcb) {
    return;
  }
  http_detach(pcb);
  if (tcp_close(pcb) != ERR_OK) {
    tcp_abort(pcb);
  }
}

void picows_http_close(picows_http_t *srv) {
  if (!srv || !srv->initialized) {
    return;
  }
  close_pcb(srv->listen_pcb);
  srv->listen_pcb = NULL;
  for (size_t i = 0; i < PICOWS_MAX_HTTP_CONNS; i++) {
    if (srv->conns[i].pcb) {
      struct tcp_pcb *pcb = srv->conns[i].pcb;
      http_detach(pcb);
      free_http_conn(&srv->conns[i]);
      tcp_abort(pcb);
    }
  }
  srv->initialized = false;
}

void picows_http_yield_port(picows_http_t *srv) {
  if (srv && srv->listen_pcb) {
    close_pcb(srv->listen_pcb);
    srv->listen_pcb = NULL;
  }
}

size_t picows_http(const picows_http_args_t *args, picows_http_t *hs) {
  if (!args) {
    return 0;
  }
  if (!hs) {
    return sizeof(struct picows_http_st);
  }

  memset(hs, 0, sizeof(*hs));
  hs->args = *args;

  if (!picows_core_init(&hs->args.core_args)) {
    return 0;
  }

  hs->port = hs->args.core_args.port != 0 ? hs->args.core_args.port : picows_core_get_port();

  struct tcp_pcb *temp_pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
  if (!temp_pcb) {
    return 0;
  }
  if (tcp_bind(temp_pcb, IP_ADDR_ANY, hs->port) != ERR_OK) {
    tcp_abort(temp_pcb);
    return 0;
  }
  struct tcp_pcb *lpcb = tcp_listen(temp_pcb);
  if (!lpcb) {
    tcp_abort(temp_pcb);
    return 0;
  }
  tcp_arg(lpcb, hs);
  tcp_accept(lpcb, http_on_accept);
  hs->listen_pcb = lpcb;
  hs->initialized = true;
  return sizeof(struct picows_http_st);
}

const picows_http_args_t *picows_http_args(const picows_http_t *srv) {
  if (!srv || !srv->initialized) {
    return NULL;
  }
  return &srv->args;
}
