#include "picows/http.h"
#include "core_internal.h"

#include <string.h>
#include <stdio.h>

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#define MAX_HTTP_CONNS 4

typedef struct http_conn_state_st {
  struct tcp_pcb *pcb;
  picows_http_t *srv;
  size_t offset;
  size_t acked;
  size_t response_total;
  bool serving;
  char req_buf[16];
  size_t req_len;
} http_conn_state_t;

struct picows_http_st {
  picows_http_args_t args;
  uint16_t port;
  struct tcp_pcb *listen_pcb;
  http_conn_state_t conns[MAX_HTTP_CONNS];
  bool initialized;
};

static http_conn_state_t *alloc_http_conn(picows_http_t *srv, struct tcp_pcb *pcb) {
  for (size_t i = 0; i < MAX_HTTP_CONNS; i++) {
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

static bool send_http_chunks(http_conn_state_t *st) {
  if (!st || !st->serving || !st->pcb || !st->srv) {
    return true;
  }
  const uint8_t *buf = st->srv->args.buf;
  size_t total_len = st->srv->args.len;
  const size_t chunk_size = 512;
  bool sent_any = false;
  while (st->offset < total_len) {
    size_t remaining = total_len - st->offset;
    size_t chunk = remaining < chunk_size ? remaining : chunk_size;
    if (tcp_write(st->pcb, buf + st->offset, (u16_t)chunk, TCP_WRITE_FLAG_COPY) != ERR_OK) {
      if (!sent_any) {
        return false;
      }
      tcp_output(st->pcb);
      break;
    }
    sent_any = true;
    st->offset += chunk;
    tcp_output(st->pcb);
  }
  if (st->offset >= total_len) {
    st->serving = false;
  }
  return true;
}

static err_t http_on_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
  http_conn_state_t *st = (http_conn_state_t *)arg;
  if (!st) {
    return ERR_OK;
  }
  st->acked += len;
  if (st->serving) {
    if (!send_http_chunks(st)) {
      tcp_arg(pcb, NULL);
      free_http_conn(st);
      tcp_abort(pcb);
      return ERR_ABRT;
    }
    if (st->serving) {
      return ERR_OK;
    }
  }
  if (st->acked >= st->response_total) {
    tcp_arg(pcb, NULL);
    free_http_conn(st);
    if (tcp_close(pcb) != ERR_OK) {
      tcp_abort(pcb);
      return ERR_ABRT;
    }
  }
  return ERR_OK;
}

static void http_on_err(void *arg, err_t err) {
  (void)err;
  http_conn_state_t *st = (http_conn_state_t *)arg;
  if (st) {
    free_http_conn(st);
  }
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
    tcp_arg(pcb, NULL);
    free_http_conn(st);
    if (tcp_close(pcb) != ERR_OK) {
      tcp_abort(pcb);
      return ERR_ABRT;
    }
    return ERR_OK;
  }

  for (size_t i = 0; i < pb->tot_len && st->req_len < sizeof(st->req_buf) - 1; i++) {
    st->req_buf[st->req_len++] = (char)pbuf_get_at(pb, (u16_t)i);
    st->req_buf[st->req_len] = '\0';
  }

  /* Check for end of HTTP headers (\r\n\r\n) */
  bool headers_done = false;
  for (size_t i = 0; i + 3 < pb->tot_len; i++) {
    if (pbuf_get_at(pb, (u16_t)i) == '\r' &&
        pbuf_get_at(pb, (u16_t)(i + 1)) == '\n' &&
        pbuf_get_at(pb, (u16_t)(i + 2)) == '\r' &&
        pbuf_get_at(pb, (u16_t)(i + 3)) == '\n') {
      headers_done = true;
      break;
    }
  }

  tcp_recved(pcb, pb->tot_len);
  pbuf_free(pb);

  if (!headers_done) {
    return ERR_OK;
  }

  const picows_http_args_t *hargs = &st->srv->args;
  if (strncmp(st->req_buf, "GET / ", 6) != 0 || !hargs->buf || hargs->len == 0) {
    static const char not_found[] =
      "HTTP/1.1 404 Not Found\r\n"
      "Connection: close\r\n\r\n";
    tcp_write(pcb, not_found, sizeof(not_found) - 1, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
    tcp_arg(pcb, NULL);
    free_http_conn(st);
    if (tcp_close(pcb) != ERR_OK) {
      tcp_abort(pcb);
      return ERR_ABRT;
    }
    return ERR_OK;
  }

  char hdr[160];
  int hdr_len = snprintf(
      hdr,
      sizeof(hdr),
      "HTTP/1.1 200 OK\r\n"
      "Connection: close\r\n"
      "Content-Type: text/html\r\n"
      "%s"
      "Content-Length: %u\r\n\r\n",
      hargs->is_gzipped ? "Content-Encoding: gzip\r\n" : "",
      (unsigned int)hargs->len);

  if (hdr_len <= 0 || tcp_write(pcb, hdr, (u16_t)hdr_len, TCP_WRITE_FLAG_COPY) != ERR_OK) {
    tcp_arg(pcb, NULL);
    free_http_conn(st);
    tcp_abort(pcb);
    return ERR_ABRT;
  }

  st->offset = 0;
  st->acked = 0;
  st->response_total = (size_t)hdr_len + hargs->len;
  st->serving = true;

  if (!send_http_chunks(st)) {
    tcp_arg(pcb, NULL);
    free_http_conn(st);
    tcp_abort(pcb);
    return ERR_ABRT;
  }

  return ERR_OK;
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
  return ERR_OK;
}

void picows_http_yield_port(picows_http_t *srv) {
  if (srv && srv->listen_pcb) {
    if (tcp_close(srv->listen_pcb) != ERR_OK) {
      tcp_abort(srv->listen_pcb);
    }
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
  if (temp_pcb) {
    if (tcp_bind(temp_pcb, IP_ADDR_ANY, hs->port) == ERR_OK) {
      struct tcp_pcb *lpcb = tcp_listen(temp_pcb);
      if (lpcb) {
        tcp_arg(lpcb, hs);
        tcp_accept(lpcb, http_on_accept);
        hs->listen_pcb = lpcb;
      } else {
        tcp_abort(temp_pcb);
      }
    } else {
      tcp_abort(temp_pcb);
    }
  }

  hs->initialized = true;
  return sizeof(struct picows_http_st);
}

const picows_http_args_t *picows_http_args(const picows_http_t *srv) {
  if (!srv || !srv->initialized) {
    return NULL;
  }
  return &srv->args;
}
