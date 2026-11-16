/*
 * Pico 2 on the Hazard3 RISC-V core. Pin defaults match raspberry_pi_pico2.
 * The platform itself is selected in board.cmake (PICO_PLATFORM=rp2350-riscv).
 */

#ifndef TUSB_BOARD_H
#define TUSB_BOARD_H

#ifdef __cplusplus
 extern "C" {
#endif

// UART and LED are already defined in the Pico SDK board header (pico2).

#define PICO_DEFAULT_PIO_USB_DP_PIN       20
#define PICO_DEFAULT_PIO_USB_VBUSEN_PIN   22
#define PICO_DEFAULT_PIO_USB_VBUSEN_STATE 1

#ifdef PICO_DEFAULT_SPI
#define MAX3421_SPI      PICO_DEFAULT_SPI
#else
#define MAX3421_SPI      PICO_DEFAULT_SPI_INSTANCE
#endif

#define MAX3421_SCK_PIN  PICO_DEFAULT_SPI_SCK_PIN
#define MAX3421_MOSI_PIN PICO_DEFAULT_SPI_TX_PIN
#define MAX3421_MISO_PIN PICO_DEFAULT_SPI_RX_PIN
#define MAX3421_CS_PIN   10
#define MAX3421_INTR_PIN 9

#ifdef __cplusplus
 }
#endif

#endif
