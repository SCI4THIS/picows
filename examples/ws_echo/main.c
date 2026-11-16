/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include <stdlib.h>

#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "picows/ws.h"

#include "index_html.h"

/*
 * USB mount blink from the old TinyUSB net example:
 *   250 ms  not mounted, 1000 ms mounted, 2500 ms suspended.
 * The library poll no longer drives the LED; the application does.
 */
static void led_task(void) {
#ifdef PICO_DEFAULT_LED_PIN
  static bool ready = false;
  static bool on = false;
  static uint32_t start_ms = 0;

  if (!ready) {
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    ready = true;
  }

  uint32_t interval_ms = 250;
  if (tud_suspended()) {
    interval_ms = 2500;
  } else if (tud_mounted()) {
    interval_ms = 1000;
  }

  uint32_t now = to_ms_since_boot(get_absolute_time());
  if (now - start_ms < interval_ms) {
    return;
  }
  start_ms += interval_ms;
  on = !on;
  gpio_put(PICO_DEFAULT_LED_PIN, on);
#else
  (void)0;
#endif
}

/* Incoming TEXT frames are NUL-terminated past `len`. */
static void on_ws_message(picows_ws_t *ws, uint32_t conn_id, const void *data, size_t len,
                          void *user_data) {
  (void)len;
  (void)user_data;
  picows_ws_send_text(ws, conn_id, (const char *)data);
}

int main(void) {
  /* Clocks and stdio stay in the application. picows_core_init() does not call board_init(). */
  stdio_init_all();

  picows_ws_args_t args = {
      .core_args =
          {
              .ip_addr = PICOWS_IPV4(192, 168, 7, 1),
              .port = 80,
          },
      .page = (const uint8_t *)index_html,
      .page_len = sizeof(index_html) - 1,
      .tcp_nodelay = true,
      .on_message = on_ws_message,
  };

  size_t ws_size = picows_ws(&args, NULL);
  picows_ws_t *ws = (picows_ws_t *)malloc(ws_size);
  if (ws == NULL || picows_ws(&args, ws) == 0) {
    printf("picows_ws failed\n");
    while (1) {
    }
  }

  printf("Echo server at http://192.168.7.1/ (DNS name tiny.usb)\n");

  while (1) {
    picows_poll();
    led_task();
  }
}
