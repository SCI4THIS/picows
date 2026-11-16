/* SPDX-License-Identifier: GPL-3.0-only */
#include "picows/ws.h"
#include "picows/http.h"
#include "core_internal.h"

#include <cstdio>
#include <cstring>
#include <new>

#include "bsp/board_api.h"
#include "dhserver.h"
#include "dnserver.h"
#include "lwip/ethip6.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include "tusb.h"
#include "pico/unique_id.h"
#include "usb_identity.h"

#include "pico_ws_server/web_socket_server.h"

/*
 * USB ECM/RNDIS + lwIP netif. This used to be the TinyUSB net_lwip_webserver
 * example, compiled into the library by #include "main.c". It lives here
 * because picows owns the USB network stack. picows_http is optional and
 * calls picows_core_init() below. The echo page is examples/ws_echo.
 *
 * Linux and Windows use RNDIS; Linux and macOS use CDC-ECM. Descriptor order
 * prefers ECM so Linux does not bind rndis_host. The first MAC byte is 0x02,
 * a locally administered address.
 */

extern "C" uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

static struct netif netif_data;

#define PICOWS_DHCP_ENTRY_COUNT 3

static dhcp_entry_t dhcp_entries[PICOWS_DHCP_ENTRY_COUNT] = {
    {{0}, {PP_HTONL(LWIP_MAKEU32(192, 168, 7, 2))}, 24 * 60 * 60},
    {{0}, {PP_HTONL(LWIP_MAKEU32(192, 168, 7, 3))}, 24 * 60 * 60},
    {{0}, {PP_HTONL(LWIP_MAKEU32(192, 168, 7, 4))}, 24 * 60 * 60},
};

static const dhcp_config_t dhcp_config = {
    .router = {PP_HTONL(LWIP_MAKEU32(0, 0, 0, 0))},
    .port = 67,
    .dns = {PP_HTONL(LWIP_MAKEU32(192, 168, 7, 1))},
    .domain = "usb",
    .num_entry = PICOWS_DHCP_ENTRY_COUNT,
    .entries = dhcp_entries,
};

static err_t linkoutput_fn(struct netif *netif, struct pbuf *p) {
  (void)netif;

  /* Do not call tud_task() here. picows_poll() is already inside tud_task()
   * when a received segment generates a reply. ERR_USE leaves the TCP segment
   * on the unsent queue; the next poll retries it. */
  if (!tud_ready() || !tud_network_can_xmit(p->tot_len)) {
    return ERR_USE;
  }
  tud_network_xmit(p, 0);
  return ERR_OK;
}

static err_t ip4_output_fn(struct netif *netif, struct pbuf *p, const ip4_addr_t *addr) {
  return etharp_output(netif, p, addr);
}

#if LWIP_IPV6
static err_t ip6_output_fn(struct netif *netif, struct pbuf *p, const ip6_addr_t *addr) {
  return ethip6_output(netif, p, addr);
}
#endif

static err_t netif_init_cb(struct netif *netif) {
  LWIP_ASSERT("netif != NULL", (netif != NULL));
  netif->mtu = CFG_TUD_NET_MTU;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
  netif->state = NULL;
  netif->name[0] = 'E';
  netif->name[1] = 'X';
  netif->linkoutput = linkoutput_fn;
  netif->output = ip4_output_fn;
#if LWIP_IPV6
  netif->output_ip6 = ip6_output_fn;
#endif
  return ERR_OK;
}

static void usbnet_link_callback(struct netif *netif) {
  bool link_up = netif_is_link_up(netif);
  tud_network_link_state(BOARD_TUD_RHPORT, link_up);
}

extern "C" bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
  struct netif *netif = &netif_data;

  if (size) {
    struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
    if (p == NULL) {
      printf("ERROR: Failed to allocate pbuf of size %d\n", size);
      return false;
    }
    pbuf_take(p, src, size);
    if (netif->input(p, netif) != ERR_OK) {
      printf("ERROR: netif input failed\n");
      pbuf_free(p);
    }
    tud_network_recv_renew();
  }
  return true;
}

extern "C" uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
  struct pbuf *p = (struct pbuf *)ref;
  (void)arg;
  return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

extern "C" sys_prot_t sys_arch_protect(void) {
  return 0;
}

extern "C" void sys_arch_unprotect(sys_prot_t pval) {
  (void)pval;
}

extern "C" uint32_t sys_now(void) {
  return tusb_time_millis_api();
}

#define MAX_POLL_FNS 4
#define PICOWS_USB_VID_DEFAULT 0xCafe
#define PICOWS_USB_PID_DEFAULT 0x4018

struct picows_net_cfg {
  uint32_t ip_addr;
  uint32_t netmask;
  uint32_t gateway;
  uint8_t mac[6];
  uint8_t disable_dhcp;
  uint8_t usb_rndis_first;
  uint16_t usb_vid;
  uint16_t usb_pid;
  char dns[64];
  char usb_manufacturer[32];
  char usb_product[32];
  char usb_interface[40];
};

static struct picows_core_st {
  bool core_initialized;
  bool init_failed;
  uint16_t core_port;
  struct picows_net_cfg cfg;
  picows_poll_fn_t poll_fns[MAX_POLL_FNS];
  size_t num_poll_fns;
  ip4_addr_t runtime_ipaddr;
  ip4_addr_t runtime_netmask;
  ip4_addr_t runtime_gateway;
  dhcp_config_t runtime_dhcp_config;
} picows_core = {
    .core_initialized = false,
    .init_failed = false,
    .core_port = 80,
    .poll_fns = {0},
    .num_poll_fns = 0,
    .runtime_ipaddr = {PP_HTONL(LWIP_MAKEU32(192, 168, 7, 1))},
    .runtime_netmask = {PP_HTONL(LWIP_MAKEU32(255, 255, 255, 0))},
    .runtime_gateway = {PP_HTONL(LWIP_MAKEU32(0, 0, 0, 0))},
};

static bool custom_dns_query_proc(const char *name, ip4_addr_t *addr) {
  if (0 == strcmp(name, picows_core.cfg.dns)) {
    *addr = picows_core.runtime_ipaddr;
    return true;
  }
  return false;
}

static void picows_copy_cfg_str(char *dst, size_t dst_len, const char *src, const char *fallback) {
  const char *use = (src && src[0] != '\0') ? src : fallback;
  snprintf(dst, dst_len, "%s", use);
}

static bool picows_mac_is_set(const uint8_t mac[6]) {
  for (size_t i = 0; i < 6; i++) {
    if (mac[i] != 0) {
      return true;
    }
  }
  return false;
}

static void picows_mac_from_board(uint8_t mac[6]) {
  pico_unique_board_id_t id;
  pico_get_unique_board_id(&id);
  mac[0] = 0x02;
  mac[1] = id.id[2];
  mac[2] = id.id[3];
  mac[3] = id.id[4];
  mac[4] = id.id[5];
  mac[5] = (uint8_t)(id.id[6] ^ id.id[7]);
}

static bool picows_resolve_cfg(const picows_args_t *args, struct picows_net_cfg *out, uint16_t *port_out) {
  memset(out, 0, sizeof(*out));
  picows_args_t effective;
  picows_default_args(&effective);
  if (args) {
    if (args->ip_addr != 0) {
      effective.ip_addr = args->ip_addr;
    }
    if (args->netmask != 0) {
      effective.netmask = args->netmask;
    }
    if (args->gateway != 0) {
      effective.gateway = args->gateway;
    }
    if (args->port != 0) {
      effective.port = args->port;
    }
    if (picows_mac_is_set(args->mac_address)) {
      memcpy(effective.mac_address, args->mac_address, 6);
    }
    if (args->dns_hostname && args->dns_hostname[0] != '\0') {
      effective.dns_hostname = args->dns_hostname;
    }
    effective.disable_dhcp = args->disable_dhcp;
    effective.usb_vid = args->usb_vid;
    effective.usb_pid = args->usb_pid;
    effective.usb_manufacturer = args->usb_manufacturer;
    effective.usb_product = args->usb_product;
    effective.usb_interface = args->usb_interface;
    effective.usb_rndis_first = args->usb_rndis_first;
  }
  if (!effective.dns_hostname || strlen(effective.dns_hostname) >= sizeof(out->dns)) {
    return false;
  }
  out->ip_addr = effective.ip_addr;
  out->netmask = effective.netmask;
  out->gateway = effective.gateway;
  if (picows_mac_is_set(effective.mac_address)) {
    memcpy(out->mac, effective.mac_address, 6);
  } else {
    picows_mac_from_board(out->mac);
  }
  out->disable_dhcp = effective.disable_dhcp ? 1 : 0;
  out->usb_rndis_first = effective.usb_rndis_first ? 1 : 0;
  out->usb_vid = effective.usb_vid != 0 ? effective.usb_vid : PICOWS_USB_VID_DEFAULT;
  out->usb_pid = effective.usb_pid != 0 ? effective.usb_pid : PICOWS_USB_PID_DEFAULT;
  picows_copy_cfg_str(out->dns, sizeof(out->dns), effective.dns_hostname, "tiny.usb");
  picows_copy_cfg_str(out->usb_manufacturer, sizeof(out->usb_manufacturer), effective.usb_manufacturer,
                      "TinyUSB");
  picows_copy_cfg_str(out->usb_product, sizeof(out->usb_product), effective.usb_product, "TinyUSB Device");
  picows_copy_cfg_str(out->usb_interface, sizeof(out->usb_interface), effective.usb_interface,
                      "TinyUSB Network Interface");
  if (port_out) {
    *port_out = effective.port;
  }
  return true;
}

extern "C" void picows_default_args(picows_args_t *args) {
  if (!args) {
    return;
  }
  memset(args, 0, sizeof(*args));
  args->ip_addr = PICOWS_IPV4(192, 168, 7, 1);
  args->netmask = PICOWS_IPV4(255, 255, 255, 0);
  args->gateway = PICOWS_IPV4(0, 0, 0, 0);
  args->port = 80;
  args->dns_hostname = "tiny.usb";
  args->disable_dhcp = false;
}

extern "C" const char *picows_ipv4_to_str(uint32_t ip_addr, char *buf, size_t buf_len) {
  if (!buf || buf_len == 0) {
    return NULL;
  }
  unsigned int a = (ip_addr >> 24) & 0xff;
  unsigned int b = (ip_addr >> 16) & 0xff;
  unsigned int c = (ip_addr >> 8) & 0xff;
  unsigned int d = ip_addr & 0xff;
  int n = snprintf(buf, buf_len, "%u.%u.%u.%u", a, b, c, d);
  if (n < 0 || (size_t)n >= buf_len) {
    return NULL;
  }
  return buf;
}

extern "C" bool picows_str_to_ipv4(const char *str, uint32_t *out_ip) {
  if (!str || !out_ip) {
    return false;
  }
  unsigned int a = 0, b = 0, c = 0, d = 0;
  char tail = '\0';
  if (sscanf(str, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4) {
    return false;
  }
  if (a > 255 || b > 255 || c > 255 || d > 255) {
    return false;
  }
  *out_ip = PICOWS_IPV4(a, b, c, d);
  return true;
}

extern "C" bool picows_core_is_initialized(void) {
  return picows_core.core_initialized;
}

extern "C" uint16_t picows_core_get_port(void) {
  return picows_core.core_port;
}

extern "C" void picows_core_register_poll_fn(picows_poll_fn_t fn) {
  if (!fn) {
    return;
  }
  for (size_t i = 0; i < picows_core.num_poll_fns; i++) {
    if (picows_core.poll_fns[i] == fn) {
      return;
    }
  }
  if (picows_core.num_poll_fns < MAX_POLL_FNS) {
    picows_core.poll_fns[picows_core.num_poll_fns++] = fn;
  }
}

extern "C" bool picows_core_init(const picows_args_t *args) {
  if (picows_core.init_failed) {
    return false;
  }

  struct picows_net_cfg cfg;
  uint16_t port = 80;
  if (!picows_resolve_cfg(args, &cfg, &port)) {
    return false;
  }
  if (picows_core.core_initialized) {
    return memcmp(&cfg, &picows_core.cfg, sizeof(cfg)) == 0;
  }

  memcpy(tud_network_mac_address, cfg.mac, 6);
  picows_usb_set_identity(cfg.usb_vid, cfg.usb_pid, cfg.usb_manufacturer, cfg.usb_product,
                          cfg.usb_interface, cfg.usb_rndis_first != 0);

  /* Clocks and stdio belong to the application. Call stdio_init_all() before this. */
  tusb_rhport_init_t dev_init = {
      .role = TUSB_ROLE_DEVICE,
      .speed = TUSB_SPEED_AUTO,
  };
  if (!tusb_init(BOARD_TUD_RHPORT, &dev_init)) {
    return false;
  }

  uint8_t ip_a = (cfg.ip_addr >> 24) & 0xff;
  uint8_t ip_b = (cfg.ip_addr >> 16) & 0xff;
  uint8_t ip_c = (cfg.ip_addr >> 8) & 0xff;
  uint8_t ip_d = cfg.ip_addr & 0xff;

  IP4_ADDR(&picows_core.runtime_ipaddr, ip_a, ip_b, ip_c, ip_d);
  IP4_ADDR(&picows_core.runtime_netmask, (cfg.netmask >> 24) & 0xff, (cfg.netmask >> 16) & 0xff,
           (cfg.netmask >> 8) & 0xff, cfg.netmask & 0xff);
  IP4_ADDR(&picows_core.runtime_gateway, (cfg.gateway >> 24) & 0xff, (cfg.gateway >> 16) & 0xff,
           (cfg.gateway >> 8) & 0xff, cfg.gateway & 0xff);

  for (size_t i = 0; i < PICOWS_DHCP_ENTRY_COUNT; i++) {
    uint8_t host_octet = (uint8_t)(ip_d + 1 + i);
    if (host_octet == 0 || host_octet == 255 || host_octet == ip_d) {
      host_octet = (uint8_t)(2 + i);
    }
    IP4_ADDR(&dhcp_entries[i].addr, ip_a, ip_b, ip_c, host_octet);
  }

  picows_core.runtime_dhcp_config = dhcp_config;
  picows_core.runtime_dhcp_config.router = picows_core.runtime_gateway;
  picows_core.runtime_dhcp_config.dns = picows_core.runtime_ipaddr;

  lwip_init();

  struct netif *netif = &netif_data;
  netif->hwaddr_len = sizeof(tud_network_mac_address);
  memcpy(netif->hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
  netif->hwaddr[5] ^= 0x01;

  netif = netif_add(netif, &picows_core.runtime_ipaddr, &picows_core.runtime_netmask,
                    &picows_core.runtime_gateway, NULL, netif_init_cb, ethernet_input);
  if (!netif) {
    picows_core.init_failed = true;
    return false;
  }
#if LWIP_IPV6
  netif_create_ip6_linklocal_address(netif, 1);
#endif
  netif_set_default(netif);

#if LWIP_NETIF_LINK_CALLBACK
  netif_set_link_callback(netif, usbnet_link_callback);
  netif_set_link_up(netif);
#else
  tud_network_link_state(BOARD_TUD_RHPORT, true);
#endif

  if (!netif_is_up(&netif_data)) {
    picows_core.init_failed = true;
    return false;
  }

  if (!cfg.disable_dhcp) {
    if (dhserv_init(&picows_core.runtime_dhcp_config) != ERR_OK) {
      picows_core.init_failed = true;
      return false;
    }
  }
  if (dnserv_init(IP_ADDR_ANY, 53, custom_dns_query_proc) != ERR_OK) {
    picows_core.init_failed = true;
    return false;
  }

  picows_core.cfg = cfg;
  picows_core.core_port = port;
  picows_core.core_initialized = true;
  return true;
}

static void ws_cull_run(void);

extern "C" void picows_poll(void) {
  tud_task();
  sys_check_timeouts();
  /* TCP callbacks have returned. Destroy servers they queued. */
  ws_cull_run();

  for (size_t i = 0; i < picows_core.num_poll_fns; i++) {
    picows_core.poll_fns[i]();
  }
}

struct picows_ws_st {
  picows_ws_args_t args;
  uint16_t port;
  WebSocketServer server;
  bool initialized;

  explicit picows_ws_st(uint32_t max_conns)
      : args{}, port(80), server(max_conns), initialized(false) {}
};

#define MAX_WS_INSTANCES 2

static struct picows_ws_global_st {
  picows_ws_t *instances[MAX_WS_INSTANCES];
  size_t num_instances;
} picows_ws_global = {
    .instances = {nullptr},
    .num_instances = 0,
};

/* Close from a user callback is queued. popMessages and the TCP callbacks
 * still use the server after that callback returns. */
static int ws_callback_depth;
static picows_ws_t *ws_cull[MAX_WS_INSTANCES];
static size_t ws_cull_count;
static bool ws_culling;

extern "C" void __attribute__((weak)) picows_http_yield_port(picows_http_t *srv) {
  (void)srv;
}

extern "C" const picows_http_args_t *__attribute__((weak))
picows_http_args(const picows_http_t *srv) {
  (void)srv;
  return nullptr;
}

static void on_ws_connect(WebSocketServer &server, uint32_t conn_id) {
  auto *ws = static_cast<picows_ws_t *>(server.getCallbackExtra());
  if (ws && ws->args.on_connect) {
    ws_callback_depth++;
    ws->args.on_connect(ws, conn_id, ws->args.user_data);
    ws_callback_depth--;
  }
}

static void on_ws_close(WebSocketServer &server, uint32_t conn_id) {
  auto *ws = static_cast<picows_ws_t *>(server.getCallbackExtra());
  if (ws && ws->args.on_close) {
    ws_callback_depth++;
    ws->args.on_close(ws, conn_id, ws->args.user_data);
    ws_callback_depth--;
  }
}

static void on_ws_message(WebSocketServer &server, uint32_t conn_id, const void *data, size_t len) {
  auto *ws = static_cast<picows_ws_t *>(server.getCallbackExtra());
  if (ws && ws->args.on_message) {
    ws_callback_depth++;
    ws->args.on_message(ws, conn_id, data, len, ws->args.user_data);
    ws_callback_depth--;
  }
}

static void ws_poll_all(void) {
  for (size_t i = 0; i < picows_ws_global.num_instances; i++) {
    if (picows_ws_global.instances[i] && picows_ws_global.instances[i]->initialized) {
      picows_ws_global.instances[i]->server.popMessages();
    }
  }
  /* Message callbacks have returned and popMessages is done with the servers. */
  ws_cull_run();
}

static size_t ws_fail(picows_ws_t *ws) {
  ws->initialized = false;
  ws->~picows_ws_st();
  return 0;
}

extern "C" size_t picows_ws(const picows_ws_args_t *args, picows_ws_t *ws) {
  if (!args) {
    return 0;
  }
  if (!ws) {
    return sizeof(struct picows_ws_st);
  }

  uint32_t max_conns = args->max_conns != 0 ? args->max_conns : 1;
  if (max_conns > PICOWS_MAX_WS_CONNS || picows_ws_global.num_instances >= MAX_WS_INSTANCES) {
    return 0;
  }

  new (ws) picows_ws_st(max_conns);
  ws->args = *args;
  ws->args.max_conns = max_conns;

  if (!picows_core_init(&ws->args.core_args)) {
    return ws_fail(ws);
  }

  ws->port = ws->args.core_args.port != 0 ? ws->args.core_args.port : picows_core_get_port();

  if (ws->args.http) {
    picows_http_yield_port(ws->args.http);
    const picows_http_args_t *hargs = picows_http_args(ws->args.http);
    if (hargs) {
      ws->server.setStaticHtml(hargs->buf, hargs->len, hargs->is_gzipped);
    }
  } else if (ws->args.page != nullptr && ws->args.page_len > 0) {
    ws->server.setStaticHtml(ws->args.page, ws->args.page_len, ws->args.page_gzipped);
  }

  ws->server.setCallbackExtra(ws);
  ws->server.setConnectCallback(on_ws_connect);
  ws->server.setCloseCallback(on_ws_close);
  ws->server.setMessageCallback(on_ws_message);
  ws->server.setTcpNoDelay(ws->args.tcp_nodelay);

  if (!ws->server.startListening(ws->port)) {
    return ws_fail(ws);
  }

  ws->initialized = true;
  picows_ws_global.instances[picows_ws_global.num_instances++] = ws;
  picows_core_register_poll_fn(ws_poll_all);

  return sizeof(struct picows_ws_st);
}

static void ws_close_server_now(picows_ws_t *srv) {
  if (!srv || !srv->initialized) {
    return;
  }
  size_t kept = 0;
  for (size_t i = 0; i < picows_ws_global.num_instances; i++) {
    if (picows_ws_global.instances[i] != srv) {
      picows_ws_global.instances[kept++] = picows_ws_global.instances[i];
    }
  }
  for (size_t i = kept; i < picows_ws_global.num_instances; i++) {
    picows_ws_global.instances[i] = nullptr;
  }
  picows_ws_global.num_instances = kept;
  srv->initialized = false;
  srv->~picows_ws_st();
}

static void ws_cull_add(picows_ws_t *srv) {
  for (size_t i = 0; i < ws_cull_count; i++) {
    if (ws_cull[i] == srv) {
      return;
    }
  }
  if (ws_cull_count < MAX_WS_INSTANCES) {
    ws_cull[ws_cull_count++] = srv;
  }
}

static void ws_cull_run(void) {
  if (ws_culling || ws_callback_depth > 0) {
    return;
  }
  ws_culling = true;
  while (ws_cull_count > 0) {
    picows_ws_t *srv = ws_cull[--ws_cull_count];
    ws_cull[ws_cull_count] = nullptr;
    ws_close_server_now(srv);
  }
  ws_culling = false;
}

extern "C" void picows_ws_close_server(picows_ws_t *srv) {
  if (!srv || !srv->initialized) {
    return;
  }
  if (ws_callback_depth > 0) {
    ws_cull_add(srv);
    return;
  }
  ws_close_server_now(srv);
}

extern "C" const picows_ws_args_t *picows_ws_args(const picows_ws_t *srv) {
  if (!srv || !srv->initialized) {
    return nullptr;
  }
  return &srv->args;
}

extern "C" void picows_ws_poll(picows_ws_t *srv) {
  /* A server queued by an earlier TCP callback is already off the stack. */
  ws_cull_run();
  if (srv && srv->initialized) {
    srv->server.popMessages();
  }
  ws_cull_run();
}

extern "C" bool picows_ws_send_text(picows_ws_t *srv, uint32_t conn_id, const char *text) {
  if (!srv || !srv->initialized) {
    return false;
  }
  return srv->server.sendMessage(conn_id, text);
}

extern "C" bool picows_ws_send_binary(picows_ws_t *srv, uint32_t conn_id, const void *data,
                                      size_t len) {
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
