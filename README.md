# picows

`picows` is a reusable USB Ethernet (TinyUSB ECM/RNDIS + lwIP) networking, HTTP, and WebSocket server library for Raspberry Pi Pico (RP2040 / RP2350), extracted from [pico-web-logic-analyzer](https://github.com/SCI4THIS/pico-web-logic-analyzer) so it can be added as a Git submodule in other Pico projects.

## Architecture & Libraries

`picows` provides three modular library targets and corresponding headers:

1. **`picows_core`** (`#include <picows/core.h>`)
   - Sets up the USB virtual network adapter (ECM/RNDIS via TinyUSB), lwIP stack, DHCP server, and DNS server.
   - Configurable IPv4 address, netmask, gateway, port, MAC address, DNS hostname, and DHCP toggle via `picows_args_t`.
   - Automatically initialized by `picows_http` or `picows_ws` via internal `picows_core_init()`.
   - Polled in the main loop via `picows_poll()`.

2. **`picows_http`** (`#include <picows/http.h>`) *(optional)*
   - Serves a client-supplied static HTML buffer (`buf` + `len`, raw or gzipped) on `GET /`.
   - Uses a two-phase allocation constructor: `size_t picows_http(const picows_http_args_t *args, picows_http_t *hs)`.
   - Can run standalone or share a port (e.g., port 80) with `picows_ws`.

3. **`picows_ws`** (`#include <picows/ws.h>`) *(optional)*
   - Provides a WebSocket server over the USB lwIP interface.
   - Uses a two-phase allocation constructor: `size_t picows_ws(const picows_ws_args_t *args, picows_ws_t *ws)`.
   - Supports text and binary frames, unicast and broadcast, and optional port sharing with a `picows_http_t` instance.

## Upstream Submodule Handling

`submodules/pico-ws-server` is kept **pristine** in the source tree. Any required USB-ECM and runtime static HTML changes live in `submodules/pico-ws-server.patch`. During the CMake build, `submodules/pico-ws-server` is staged into the build directory where `submodules/pico-ws-server.patch` is applied before compiling.

## Usage Example

```c
#include <stdlib.h>
#include <picows/core.h>
#include <picows/http.h>
#include <picows/ws.h>

static const uint8_t index_html[] = "<html><body><h1>Hello from Pico!</h1></body></html>";

static void on_ws_message(picows_ws_t *ws, uint32_t conn_id, const void *data, size_t len, void *user_data) {
  (void)user_data;
  picows_ws_send_binary(ws, conn_id, data, len);
}

int main(void) {
  picows_http_args_t http_args = {
    .core_args = {
      .ip_addr = PICOWS_IPV4(192, 168, 7, 1),
      .port = 80,
    },
    .buf = index_html,
    .len = sizeof(index_html) - 1,
    .is_gzipped = false,
  };

  size_t http_sz = picows_http(&http_args, NULL);
  picows_http_t *http = (picows_http_t *)malloc(http_sz);
  picows_http(&http_args, http);

  picows_ws_args_t ws_args = {
    .core_args = http_args.core_args,
    .http = http,
    .tcp_nodelay = true,
    .on_message = on_ws_message,
  };

  size_t ws_sz = picows_ws(&ws_args, NULL);
  picows_ws_t *ws = (picows_ws_t *)malloc(ws_sz);
  picows_ws(&ws_args, ws);

  while (1) {
    picows_poll();
  }
}
```
