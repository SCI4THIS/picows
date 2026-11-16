if(POLICY CMP0225)
  cmake_policy(SET CMP0225 NEW)
endif()
include(ExternalProject)

# Three Pico architectures. Each gets its own build tree because the
# compiler and PICO_PLATFORM cannot change inside one CMake cache.
#   rp2040        Arm Cortex-M0+   (Pico)
#   rp2350-arm    Arm Cortex-M33   (Pico 2)
#   rp2350-riscv  Hazard3 RISC-V   (Pico 2)
#
# Each architecture is dynamically included in the build only if its
# corresponding cross-compiler toolchain is detected.

find_program(PICOWS_ARM_GCC
  NAMES arm-none-eabi-gcc
)

find_program(PICOWS_RISCV_GCC
  NAMES riscv32-corev-elf-gcc riscv32-pico-elf-gcc riscv32-unknown-elf-gcc riscv-none-elf-gcc
)

set(PICOWS_RISCV_TOOLCHAIN_ARGS "")
if(PICOWS_RISCV_GCC)
  get_filename_component(_picows_riscv_bin "${PICOWS_RISCV_GCC}" DIRECTORY)
  get_filename_component(PICOWS_RISCV_TOOLCHAIN_ROOT "${_picows_riscv_bin}" DIRECTORY)
  set(PICOWS_RISCV_TOOLCHAIN_ARGS "-DPICO_TOOLCHAIN_PATH=${PICOWS_RISCV_TOOLCHAIN_ROOT}")
endif()

function(picows_add_arch folder board)
  string(REPLACE "-" "_" target "${folder}")
  set(extra_args "")
  if(folder STREQUAL "rp2350-riscv" AND PICOWS_RISCV_TOOLCHAIN_ARGS)
    list(APPEND extra_args "${PICOWS_RISCV_TOOLCHAIN_ARGS}")
  endif()
  if(DEFINED PICOTOOL_FETCH_FROM_GIT_PATH)
    list(APPEND extra_args "-DPICOTOOL_FETCH_FROM_GIT_PATH=${PICOTOOL_FETCH_FROM_GIT_PATH}")
  elseif(DEFINED ENV{PICOTOOL_FETCH_FROM_GIT_PATH})
    list(APPEND extra_args "-DPICOTOOL_FETCH_FROM_GIT_PATH=$ENV{PICOTOOL_FETCH_FROM_GIT_PATH}")
  endif()
  if(DEFINED picotool_DIR)
    list(APPEND extra_args "-Dpicotool_DIR=${picotool_DIR}")
  endif()
  ExternalProject_Add(picows_${target}
    SOURCE_DIR "${PICOWS_ROOT}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/arch/${folder}"
    CMAKE_ARGS
      -DPICOWS_SINGLE_ARCH=ON
      -DPICOWS_BUILD_EXAMPLE=ON
      -DBOARD=${board}
      -DPICO_SDK_PATH=${PICOWS_ROOT}/submodules/pico-sdk
      -DPICOWS_DIST_DIR=${CMAKE_BINARY_DIR}/dist/${folder}
      -DCMAKE_BUILD_TYPE=Release
      ${extra_args}
    BUILD_ALWAYS YES
    INSTALL_COMMAND ""
    USES_TERMINAL_CONFIGURE YES
    USES_TERMINAL_BUILD YES
  )
endfunction()

if(PICOWS_ARM_GCC)
  message(STATUS "Found ARM toolchain (${PICOWS_ARM_GCC}): enabling rp2040 and rp2350-arm builds")
  picows_add_arch(rp2040 raspberry_pi_pico)
  picows_add_arch(rp2350-arm raspberry_pi_pico2)
else()
  message(WARNING
    "No ARM bare-metal GCC (arm-none-eabi-gcc) was found on PATH; skipping rp2040 and rp2350-arm builds.\n"
    "To acquire it on Arch Linux / Omarchy:\n"
    "  yay -S --needed gcc-arm-none-eabi-bin\n"
    "Or on x86_64 Arch:\n"
    "  sudo pacman -S --needed arm-none-eabi-gcc arm-none-eabi-newlib arm-none-eabi-binutils")
endif()

if(PICOWS_RISCV_GCC)
  message(STATUS "Found RISC-V toolchain (${PICOWS_RISCV_GCC}): enabling rp2350-riscv build")
  picows_add_arch(rp2350-riscv raspberry_pi_pico2_riscv)
else()
  message(STATUS
    "No RISC-V GCC (riscv32-corev-elf-gcc / riscv32-pico-elf-gcc / riscv32-unknown-elf-gcc / riscv-none-elf-gcc) "
    "was found on PATH; skipping rp2350-riscv build.\n"
    "  To acquire the RP2350 Hazard3 RISC-V toolchain:\n"
    "  - Prebuilt binaries (Raspberry Pi pico-sdk-tools, supports x86_64 and aarch64):\n"
    "      Download riscv-toolchain-15-*-lin.tar.gz from:\n"
    "      https://github.com/raspberrypi/pico-sdk-tools/releases\n"
    "      Extract and add its bin/ directory to PATH (or pass -DPICO_TOOLCHAIN_PATH=/path/to/riscv-toolchain).\n"
    "  - Or via AUR:\n"
    "      yay -S --needed riscv32-unknown-elf-gcc   (or xpack-riscv-none-elf-gcc-bin)")
endif()

find_program(PICOWS_HOST_CC gcc PATHS /usr/bin NO_DEFAULT_PATH)
if(NOT PICOWS_HOST_CC)
  find_program(PICOWS_HOST_CC NAMES gcc cc)
endif()
if(PICOWS_HOST_CC)
  set(PICOWS_HOST_TEST ${CMAKE_BINARY_DIR}/http_parse_test)
  add_custom_target(picows_host_tests ALL
    COMMAND ${PICOWS_HOST_CC} -std=c11 -Wall -Wextra -Werror
            -I${PICOWS_ROOT}/src -I${PICOWS_ROOT}/src/mbedtls_compat
            -o ${PICOWS_HOST_TEST}
            ${PICOWS_ROOT}/src/http_parse.c
            ${PICOWS_ROOT}/tests/http_parse_test.c
    COMMAND ${PICOWS_HOST_TEST}
    COMMENT "Host HTTP request parser test"
    VERBATIM
  )
endif()
