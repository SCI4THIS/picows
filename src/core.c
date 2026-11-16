#include "core_internal.h"

#include <stdio.h>
#include <string.h>

#define main tinyusb_example_main_unused
#define httpd_init tinyusb_httpd_init_unused

#include "main.c"

#undef httpd_init
#undef main

void tinyusb_httpd_init_unused(void) {}

#define MAX_POLL_FNS 4

static struct picows_core_st {
  bool core_initialized;
  uint16_t core_port;
  char custom_dns_hostname[64];
  picows_poll_fn_t poll_fns[MAX_POLL_FNS];
  size_t num_poll_fns;
  ip4_addr_t runtime_ipaddr;
  ip4_addr_t runtime_netmask;
  ip4_addr_t runtime_gateway;
  dhcp_config_t runtime_dhcp_config;
} picows_core = {
  .core_initialized = false,
  .core_port = 80,
  .custom_dns_hostname = "tiny.usb",
  .poll_fns = {0},
  .num_poll_fns = 0,
  .runtime_ipaddr = INIT_IP4(192, 168, 7, 1),
  .runtime_netmask = INIT_IP4(255, 255, 255, 0),
  .runtime_gateway = INIT_IP4(0, 0, 0, 0),
};

static bool custom_dns_query_proc(const char *name, ip4_addr_t *addr) {
  if (0 == strcmp(name, picows_core.custom_dns_hostname)) {
    *addr = picows_core.runtime_ipaddr;
    return true;
  }
  return false;
}

void picows_default_args(picows_args_t *args) {
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

const char *picows_ipv4_to_str(uint32_t ip_addr, char *buf, size_t buf_len) {
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

bool picows_str_to_ipv4(const char *str, uint32_t *out_ip) {
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

bool picows_core_is_initialized(void) {
  return picows_core.core_initialized;
}

uint16_t picows_core_get_port(void) {
  return picows_core.core_port;
}

void picows_core_register_poll_fn(picows_poll_fn_t fn) {
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

bool picows_core_init(const picows_args_t *args) {
  if (picows_core.core_initialized) {
    if (args && args->port != 0) {
      picows_core.core_port = args->port;
    }
    return true;
  }

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
    bool has_mac = false;
    for (size_t i = 0; i < 6; i++) {
      if (args->mac_address[i] != 0) {
        has_mac = true;
        break;
      }
    }
    if (has_mac) {
      memcpy(tud_network_mac_address, args->mac_address, 6);
    }
    if (args->dns_hostname && args->dns_hostname[0] != '\0') {
      effective.dns_hostname = args->dns_hostname;
    }
    effective.disable_dhcp = args->disable_dhcp;
  }

  picows_core.core_port = effective.port;
  snprintf(picows_core.custom_dns_hostname, sizeof(picows_core.custom_dns_hostname), "%s", effective.dns_hostname);

  uint8_t ip_a = (effective.ip_addr >> 24) & 0xff;
  uint8_t ip_b = (effective.ip_addr >> 16) & 0xff;
  uint8_t ip_c = (effective.ip_addr >> 8) & 0xff;
  uint8_t ip_d = effective.ip_addr & 0xff;

  IP4_ADDR(&picows_core.runtime_ipaddr, ip_a, ip_b, ip_c, ip_d);
  IP4_ADDR(&picows_core.runtime_netmask,
           (effective.netmask >> 24) & 0xff,
           (effective.netmask >> 16) & 0xff,
           (effective.netmask >> 8) & 0xff,
           effective.netmask & 0xff);
  IP4_ADDR(&picows_core.runtime_gateway,
           (effective.gateway >> 24) & 0xff,
           (effective.gateway >> 16) & 0xff,
           (effective.gateway >> 8) & 0xff,
           effective.gateway & 0xff);

  /* Update DHCP pool entries to match the configured subnet */
  for (size_t i = 0; i < TU_ARRAY_SIZE(entries); i++) {
    uint8_t host_octet = (uint8_t)(ip_d + 1 + i);
    if (host_octet == 0 || host_octet == 255 || host_octet == ip_d) {
      host_octet = (uint8_t)(2 + i);
    }
    IP4_ADDR(&entries[i].addr, ip_a, ip_b, ip_c, host_octet);
  }

  picows_core.runtime_dhcp_config = dhcp_config;
  picows_core.runtime_dhcp_config.router = picows_core.runtime_gateway;
  picows_core.runtime_dhcp_config.dns = picows_core.runtime_ipaddr;

  /* Initialize TinyUSB and board */
  board_init();

  tusb_rhport_init_t dev_init = {
    .role = TUSB_ROLE_DEVICE,
    .speed = TUSB_SPEED_AUTO
  };
  tusb_init(BOARD_TUD_RHPORT, &dev_init);

  board_init_after_tusb();

  /* Initialize lwIP with runtime IP settings */
  struct netif *netif = &netif_data;
  lwip_init();

  netif->hwaddr_len = sizeof(tud_network_mac_address);
  memcpy(netif->hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
  netif->hwaddr[5] ^= 0x01;

  netif = netif_add(netif,
                    &picows_core.runtime_ipaddr,
                    &picows_core.runtime_netmask,
                    &picows_core.runtime_gateway,
                    NULL,
                    netif_init_cb,
                    ethernet_input);
#if LWIP_IPV6
  netif_create_ip6_linklocal_address(netif, 1);
#endif
  netif_set_default(netif);

#if LWIP_NETIF_LINK_CALLBACK
  netif_set_link_callback(netif, usbnet_netif_link_callback);
  netif_set_link_up(netif);
#else
  tud_network_link_state(BOARD_TUD_RHPORT, true);
#endif

  while (!netif_is_up(&netif_data)) {}

  if (!effective.disable_dhcp) {
    while (dhserv_init(&picows_core.runtime_dhcp_config) != ERR_OK) {}
  }
  while (dnserv_init(IP_ADDR_ANY, 53, custom_dns_query_proc) != ERR_OK) {}

  picows_core.core_initialized = true;
  return true;
}

void picows_poll(void) {
  tud_task();
  sys_check_timeouts();
  handle_link_state_switch();
  led_blinking_task();

  for (size_t i = 0; i < picows_core.num_poll_fns; i++) {
    picows_core.poll_fns[i]();
  }
}
