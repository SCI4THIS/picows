#include "picows/ws.h"
#include "picows/http.h"
#include "core_internal.h"

#include <cstring>
#include <new>

#include "pico_ws_server/web_socket_server.h"

struct picows_ws_st {
  picows_ws_args_t args;
  uint16_t port;
  WebSocketServer server;
  bool initialized;

  explicit picows_ws_st(uint32_t max_conns)
      : args{}, port(80), server(max_conns), initialized(false) {}
};

#define MAX_WS_INSTANCES 4

static struct picows_ws_global_st {
  picows_ws_t *instances[MAX_WS_INSTANCES];
  size_t num_instances;
} picows_ws_global = {
  .instances = {nullptr},
  .num_instances = 0,
};

extern "C" void __attribute__((weak)) picows_http_yield_port(picows_http_t *srv) {
  (void)srv;
}

extern "C" const picows_http_args_t * __attribute__((weak)) picows_http_args(const picows_http_t *srv) {
  (void)srv;
  return nullptr;
}

static void ws_poll_all(void) {
  for (size_t i = 0; i < picows_ws_global.num_instances; i++) {
    if (picows_ws_global.instances[i] && picows_ws_global.instances[i]->initialized) {
      picows_ws_global.instances[i]->server.popMessages();
    }
  }
}

static void on_ws_connect(WebSocketServer &server, uint32_t conn_id) {
  auto *ws = static_cast<picows_ws_t *>(server.getCallbackExtra());
  if (ws && ws->args.on_connect) {
    ws->args.on_connect(ws, conn_id, ws->args.user_data);
  }
}

static void on_ws_close(WebSocketServer &server, uint32_t conn_id) {
  auto *ws = static_cast<picows_ws_t *>(server.getCallbackExtra());
  if (ws && ws->args.on_close) {
    ws->args.on_close(ws, conn_id, ws->args.user_data);
  }
}

static void on_ws_message(WebSocketServer &server, uint32_t conn_id, const void *data, size_t len) {
  auto *ws = static_cast<picows_ws_t *>(server.getCallbackExtra());
  if (ws && ws->args.on_message) {
    ws->args.on_message(ws, conn_id, data, len, ws->args.user_data);
  }
}

extern "C" size_t picows_ws(const picows_ws_args_t *args, picows_ws_t *ws) {
  if (!args) {
    return 0;
  }
  if (!ws) {
    return sizeof(struct picows_ws_st);
  }

  uint32_t max_conns = args->max_conns != 0 ? args->max_conns : 1;
  new (ws) picows_ws_st(max_conns);
  ws->args = *args;
  ws->args.max_conns = max_conns;

  if (!picows_core_init(&ws->args.core_args)) {
    return 0;
  }

  ws->port = ws->args.core_args.port != 0 ? ws->args.core_args.port : picows_core_get_port();

  if (ws->args.http) {
    picows_http_yield_port(ws->args.http);
    const picows_http_args_t *hargs = picows_http_args(ws->args.http);
    if (hargs) {
      ws->server.setStaticHtml(hargs->buf, hargs->len, hargs->is_gzipped);
    }
  }

  ws->server.setCallbackExtra(ws);
  ws->server.setConnectCallback(on_ws_connect);
  ws->server.setCloseCallback(on_ws_close);
  ws->server.setMessageCallback(on_ws_message);
  ws->server.setTcpNoDelay(ws->args.tcp_nodelay);

  if (!ws->server.startListening(ws->port)) {
    return 0;
  }

  ws->initialized = true;

  bool registered = false;
  for (size_t i = 0; i < picows_ws_global.num_instances; i++) {
    if (picows_ws_global.instances[i] == ws) {
      registered = true;
      break;
    }
  }
  if (!registered && picows_ws_global.num_instances < MAX_WS_INSTANCES) {
    picows_ws_global.instances[picows_ws_global.num_instances++] = ws;
  }

  picows_core_register_poll_fn(ws_poll_all);

  return sizeof(struct picows_ws_st);
}

extern "C" const picows_ws_args_t *picows_ws_args(const picows_ws_t *srv) {
  if (!srv || !srv->initialized) {
    return nullptr;
  }
  return &srv->args;
}

extern "C" void picows_ws_poll(picows_ws_t *srv) {
  if (srv && srv->initialized) {
    srv->server.popMessages();
  }
}

extern "C" bool picows_ws_send_text(picows_ws_t *srv, uint32_t conn_id, const char *text) {
  if (!srv || !srv->initialized) {
    return false;
  }
  return srv->server.sendMessage(conn_id, text);
}

extern "C" bool picows_ws_send_binary(picows_ws_t *srv, uint32_t conn_id, const void *data, size_t len) {
  if (!srv || !srv->initialized) {
    return false;
  }
  return srv->server.sendMessage(conn_id, data, len);
}

extern "C" bool picows_ws_broadcast_text(picows_ws_t *srv, const char *text) {
  if (!srv || !srv->initialized) {
    return false;
  }
  return srv->server.broadcastMessage(text);
}

extern "C" bool picows_ws_broadcast_binary(picows_ws_t *srv, const void *data, size_t len) {
  if (!srv || !srv->initialized) {
    return false;
  }
  return srv->server.broadcastMessage(data, len);
}

extern "C" bool picows_ws_close(picows_ws_t *srv, uint32_t conn_id) {
  if (!srv || !srv->initialized) {
    return false;
  }
  return srv->server.close(conn_id);
}
