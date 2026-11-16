#ifndef PICOWS_CORE_H
#define PICOWS_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Helper macro to construct an IPv4 address in host byte order.
 */
#define PICOWS_IPV4(a, b, c, d) \
  ((uint32_t)((((uint32_t)(a) & 0xff) << 24) | \
              (((uint32_t)(b) & 0xff) << 16) | \
              (((uint32_t)(c) & 0xff) << 8)  | \
              ((uint32_t)(d) & 0xff)))

typedef struct picows_args_st {
  /**
   * Device IPv4 address in host byte order (e.g. PICOWS_IPV4(192, 168, 7, 1))
   * or 0 to use default 192.168.7.1.
   */
  uint32_t ip_addr;

  /**
   * Subnet mask in host byte order (e.g. PICOWS_IPV4(255, 255, 255, 0))
   * or 0 to use default 255.255.255.0.
   */
  uint32_t netmask;

  /**
   * Gateway IPv4 address in host byte order, or 0 for default (0.0.0.0).
   */
  uint32_t gateway;

  /**
   * Server TCP port (0 uses default: 80).
   */
  uint16_t port;

  /**
   * Optional 6-byte USB network MAC address override (all zeros uses default).
   */
  uint8_t mac_address[6];

  /**
   * Optional DNS hostname resolved to device IP (NULL uses default "tiny.usb").
   */
  const char *dns_hostname;

  /**
   * Disable the embedded DHCP server (false = DHCP enabled by default).
   */
  bool disable_dhcp;
} picows_args_t;

/**
 * Populate a picows_args_t structure with default settings:
 * - ip_addr: 192.168.7.1
 * - netmask: 255.255.255.0
 * - gateway: 0.0.0.0
 * - port: 80
 * - dns_hostname: "tiny.usb"
 * - disable_dhcp: false
 */
void picows_default_args(picows_args_t *args);

/**
 * Helper to convert an IPv4 address in host byte order to a dotted-decimal string.
 * @param ip_addr   IPv4 address in host byte order (e.g. PICOWS_IPV4(192, 168, 7, 1)).
 * @param buf       Output buffer (at least 16 bytes recommended).
 * @param buf_len   Size of buf in bytes.
 * @return          Pointer to buf, or NULL if buf is invalid.
 */
const char *picows_ipv4_to_str(uint32_t ip_addr, char *buf, size_t buf_len);

/**
 * Helper to parse a dotted-decimal string (e.g. "192.168.7.1") into a host-byte-order uint32_t.
 * @param str       Dotted-decimal IPv4 string.
 * @param out_ip    Output uint32_t in host byte order.
 * @return          True if parsed successfully.
 */
bool picows_str_to_ipv4(const char *str, uint32_t *out_ip);

/**
 * Poll the USB network stack (TinyUSB + lwIP timeouts) and any registered
 * picows servers. Call regularly in the main loop.
 */
void picows_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* PICOWS_CORE_H */
