# picows

`picows` is a reusable USB Ethernet (TinyUSB ECM/RNDIS + lwIP) networking, HTTP, and WebSocket server library for Raspberry Pi Pico (RP2040 / RP2350), extracted from [pico-web-logic-analyzer](https://github.com/SCI4THIS/pico-web-logic-analyzer) so it can be added as a Git submodule in other Pico projects.

## Architecture & Libraries

`picows` provides two library targets:

1. **`picows`** (`#include <picows/ws.h>`, which includes `<picows/core.h>`)
   - USB virtual network adapter (ECM/RNDIS via TinyUSB), lwIP, DHCP server, and DNS server.
   - WebSocket server: text and binary frames, unicast and broadcast.
   - Optional static page on `GET /` via `picows_ws_args_t.page`.
   - Configurable IPv4 address, netmask, gateway, port, MAC address, DNS hostname, DHCP, USB VID/PID/strings, and ECM-versus-RNDIS order via `picows_args_t`.
   - An all-zero MAC is filled from the Pico unique id, with the first byte `0x02`.
   - Polled from the main loop with `picows_poll()`. The application calls `stdio_init_all()` first. The library does not call `board_init()` and does not read the BOOTSEL button.
   - The USB/lwIP driver lives in `src/ws.cpp`. lwIP is built with `LWIP_SINGLE_NETIF`, so this stack cannot share the chip with CYW43.
   - `MEMP_NUM_TCP_PCB` is 8: four HTTP connections and four WebSocket connections. A fifth WebSocket server returns 0 from `picows_ws()`. `picows_ws_close_server()` and `picows_http_close()` release the sockets.
   - `linkoutput` returns `ERR_USE` when TinyUSB cannot take a packet. It does not re-enter `tud_task()`.

2. **`picows_http`** (`#include <picows/http.h>`) *(optional)*
   - Serves a client-supplied static HTML buffer (`buf` + `len`, raw or gzipped) on `GET /`.
   - Uses a two-phase allocation constructor: `size_t picows_http(const picows_http_args_t *args, picows_http_t *hs)`.
   - Can run on its own port, or share a port with `picows` by passing the `picows_http_t *` in `picows_ws_args_t.http`.
   - Link `picows` as well: the USB network stack is part of that library.

## Example

`examples/ws_echo` serves an "It works" page with a text box and a list of messages echoed by the WebSocket server. A standalone build dynamically detects installed cross-compilers (`arm-none-eabi-gcc` for RP2040 / RP2350-Arm, and `riscv32-*-elf-gcc` / `riscv-none-elf-gcc` for RP2350-RISC-V) and produces `ws_echo.uf2` next to the libraries for each available architecture.

```
cmake -S . -B build
cmake --build build
```

Outputs:

| Directory | Board | CPU | Required Compiler |
| --- | --- | --- | --- |
| `build/dist/rp2040/` | Pico | Arm Cortex-M0+ | `arm-none-eabi-gcc` |
| `build/dist/rp2350-arm/` | Pico 2 | Arm Cortex-M33 | `arm-none-eabi-gcc` |
| `build/dist/rp2350-riscv/` | Pico 2 | Hazard3 RISC-V | `riscv32-corev-elf-gcc` / `riscv32-pico-elf-gcc` / `riscv32-unknown-elf-gcc` / `riscv-none-elf-gcc` |

Each built directory contains `libpicows.a`, `libpicows_http.a`, `ws_echo.elf`, and `ws_echo.uf2`.

### Installing Cross-Compilers

- **ARM (`rp2040` and `rp2350-arm`)**:
  - On Arch Linux ARM (`aarch64` / Omarchy):
    ```bash
    yay -S --needed gcc-arm-none-eabi-bin
    ```
  - On Arch Linux (`x86_64`):
    ```bash
    sudo pacman -S --needed arm-none-eabi-gcc arm-none-eabi-newlib arm-none-eabi-binutils
    ```
- **RISC-V (`rp2350-riscv`)**:
  - Prebuilt binaries from Raspberry Pi (`x86_64` and `aarch64`): download `riscv-toolchain-15-*-lin.tar.gz` from [pico-sdk-tools releases](https://github.com/raspberrypi/pico-sdk-tools/releases) and add its `bin/` directory to `PATH` (or pass `-DPICO_TOOLCHAIN_PATH=/path/to/riscv-toolchain`).
  - Or via AUR:
    ```bash
    yay -S --needed riscv32-unknown-elf-gcc
    ```

Flash `ws_echo.uf2`, then open `http://192.168.7.1/` or `http://tiny.usb/`.

The same build runs `tests/http_parse_test` on the host. It checks that an HTTP header split across packets is recognized, and that the WebSocket accept hash matches RFC 6455.

From a project that has already called `pico_sdk_init()`:

```cmake
add_subdirectory(picows)
picows_initialize_project(my_app)
```

`picows_initialize_project` links `picows` and `pico_stdlib` and selects the C++ linker. It does not change `BOARD` or `CMAKE_BUILD_TYPE`. Link `picows_http` as well for the optional HTTP server. `PICOWS_ENABLE_IPERF` compiles lwIP iperf; the application has to start that server itself.

`libpicows.a` contains picows, lwIP, and the TinyUSB device stack. It does not contain the Pico startup files. The application still links `pico_stdlib`. Do not also link the SDK's `tinyusb_device`.

See `NOTICE` for which license applies to which files.

## Upstream Submodule Handling

`submodules/pico-ws-server` is kept **pristine** in the source tree. Any required USB-ECM and runtime static HTML changes live in `submodules/pico-ws-server.patch`. During the CMake build, `submodules/pico-ws-server` is staged into the build directory where `submodules/pico-ws-server.patch` is applied before compiling.

## Usage Example

Link `picows` (and `picows_http` only if you want the standalone HTTP server). The final link must use the C++ linker.

```c
#include <stdlib.h>
#include "pico/stdlib.h"
#include <picows/ws.h>

static const uint8_t index_html[] =
    "<html><body><h1>It works</h1></body></html>";

static void on_ws_message(picows_ws_t *ws, uint32_t conn_id, const void *data, size_t len, void *user_data) {
  (void)user_data;
  (void)len; /* TEXT payloads are NUL-terminated past len */
  picows_ws_send_text(ws, conn_id, (const char *)data);
}

int main(void) {
  stdio_init_all();

  picows_ws_args_t ws_args = {
    .core_args = {
      .ip_addr = PICOWS_IPV4(192, 168, 7, 1),
      .port = 80,
    },
    .page = index_html,
    .page_len = sizeof(index_html) - 1,
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
