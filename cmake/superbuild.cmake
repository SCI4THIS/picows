include(ExternalProject)

# Three Pico architectures. Each gets its own build tree because the
# compiler and PICO_PLATFORM cannot change inside one CMake cache.
#   rp2040        Arm Cortex-M0+   (Pico)
#   rp2350-arm    Arm Cortex-M33   (Pico 2)
#   rp2350-riscv  Hazard3 RISC-V   (Pico 2)
#
# RISC-V needs riscv-none-elf-gcc, riscv32-unknown-elf-gcc, or
# riscv32-pico-elf-gcc on PATH. Pass -DPICO_TOOLCHAIN_PATH= if it lives
# outside the default search path.

find_program(PICOWS_RISCV_GCC
  NAMES riscv-none-elf-gcc riscv32-unknown-elf-gcc riscv32-pico-elf-gcc
)
set(PICOWS_RISCV_TOOLCHAIN_ARGS "")
if(PICOWS_RISCV_GCC)
  get_filename_component(_picows_riscv_bin "${PICOWS_RISCV_GCC}" DIRECTORY)
  get_filename_component(PICOWS_RISCV_TOOLCHAIN_ROOT "${_picows_riscv_bin}" DIRECTORY)
  set(PICOWS_RISCV_TOOLCHAIN_ARGS "-DPICO_TOOLCHAIN_PATH=${PICOWS_RISCV_TOOLCHAIN_ROOT}")
else()
  message(WARNING
    "No RISC-V GCC was found (riscv-none-elf-gcc / riscv32-unknown-elf-gcc / riscv32-pico-elf-gcc). "
    "The rp2350-riscv libraries will fail to configure until one is installed.")
endif()

function(picows_add_arch folder board)
  string(REPLACE "-" "_" target "${folder}")
  set(extra_args "")
  if(folder STREQUAL "rp2350-riscv" AND PICOWS_RISCV_TOOLCHAIN_ARGS)
    set(extra_args "${PICOWS_RISCV_TOOLCHAIN_ARGS}")
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

picows_add_arch(rp2040 raspberry_pi_pico)
picows_add_arch(rp2350-arm raspberry_pi_pico2)
picows_add_arch(rp2350-riscv raspberry_pi_pico2_riscv)

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
