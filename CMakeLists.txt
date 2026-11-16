cmake_minimum_required(VERSION 3.20)

set(PICOWS_ROOT ${CMAKE_CURRENT_LIST_DIR})

# Standalone `cmake -B build` produces libraries and the ws_echo firmware for
# RP2040, RP2350 Arm, and RP2350 RISC-V. One architecture is built when this
# file is added with add_subdirectory(), or when PICOWS_SINGLE_ARCH is set.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT PICOWS_SINGLE_ARCH)
  project(picows_superbuild LANGUAGES NONE)
  include(${PICOWS_ROOT}/cmake/superbuild.cmake)
  return()
endif()

# Default PICO_SDK_PATH to the bundled submodule if not already set
if(NOT PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
  if(EXISTS "${PICOWS_ROOT}/submodules/pico-sdk/pico_sdk_init.cmake")
    set(PICO_SDK_PATH "${PICOWS_ROOT}/submodules/pico-sdk" CACHE PATH "Path to the Raspberry Pi Pico SDK")
  endif()
endif()

set(LWIP ${PICOWS_ROOT}/lib/lwip)

# Standalone single-arch builds bring up the Pico SDK. A parent project calls
# pico_sdk_init() first and then add_subdirectory(picows). That path does not
# include family_support.cmake and does not force BOARD or CMAKE_BUILD_TYPE.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  if(NOT DEFINED BOARD OR "${BOARD}" STREQUAL "")
    set(BOARD raspberry_pi_pico CACHE STRING "Target board")
  endif()
  include(${PICOWS_ROOT}/hw/bsp/family_support.cmake)
  project(picows_project C CXX ASM)
  family_initialize_project(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR})
else()
  if(NOT TARGET pico_stdlib)
    message(FATAL_ERROR "Call pico_sdk_init() before add_subdirectory(picows)")
  endif()
  if(NOT DEFINED BOARD OR "${BOARD}" STREQUAL "")
    set(BOARD raspberry_pi_pico)
  endif()
endif()

# Link picows into an application target after pico_sdk_init() and
# add_subdirectory(picows). Does not set BOARD or CMAKE_BUILD_TYPE.
function(picows_initialize_project TARGET_NAME)
  if(NOT TARGET pico_stdlib)
    message(FATAL_ERROR "picows_initialize_project(${TARGET_NAME}) requires pico_sdk_init() first")
  endif()
  if(NOT TARGET picows)
    message(FATAL_ERROR "picows_initialize_project(${TARGET_NAME}) requires add_subdirectory(picows) first")
  endif()
  # pico_unique_id is not part of pico_stdlib. The archive calls
  # pico_get_unique_board_id() and does not compile that .c itself.
  target_link_libraries(${TARGET_NAME} PRIVATE picows pico_stdlib pico_unique_id)
  set_target_properties(${TARGET_NAME} PROPERTIES LINKER_LANGUAGE CXX)
endfunction()

if(NOT TARGET lwipopts_provider)
  add_library(lwipopts_provider INTERFACE)
  target_include_directories(lwipopts_provider INTERFACE
    ${PICOWS_ROOT}/src
    ${LWIP}/src/include
    ${LWIP}/src/include/ipv4
    ${LWIP}/src/include/lwip/apps
  )
  target_compile_definitions(lwipopts_provider INTERFACE
    CFG_TUSB_MCU=OPT_MCU_RP2040
  )
endif()

#--------------------------------------------------------------------+
# picows: USB ECM/RNDIS + lwIP + DHCP + DNS + WebSocket
#--------------------------------------------------------------------+
# Stage and patch pico-ws-server into the build directory so that
# submodules/pico-ws-server remains pristine in the source tree.
set(PICO_WS_SERVER_SRC_DIR ${PICOWS_ROOT}/submodules/pico-ws-server)
set(PICO_WS_SERVER_PATCH_FILE ${PICOWS_ROOT}/submodules/pico-ws-server.patch)
set(PICO_WS_SERVER_STAGED_REL staged/pico-ws-server)
set(PICO_WS_SERVER_STAGED_DIR ${CMAKE_CURRENT_BINARY_DIR}/${PICO_WS_SERVER_STAGED_REL})

find_package(Git REQUIRED)

# Stage and apply patch at configure time so add_subdirectory() sees the patched CMakeLists.txt,
# and also register a custom command/target so build targets re-stage if the patch or submodule changes.
# execute_process COMMAND lists are a pipeline, so stage in separate steps.
execute_process(
  COMMAND ${CMAKE_COMMAND} -E rm -rf ${PICO_WS_SERVER_STAGED_DIR}
  RESULT_VARIABLE _stage_copy_res
)
if(_stage_copy_res EQUAL 0)
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PICO_WS_SERVER_SRC_DIR} ${PICO_WS_SERVER_STAGED_DIR}
    RESULT_VARIABLE _stage_copy_res
  )
endif()
if(_stage_copy_res EQUAL 0)
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E rm -f ${PICO_WS_SERVER_STAGED_DIR}/.git
    RESULT_VARIABLE _stage_copy_res
  )
endif()
if(NOT _stage_copy_res EQUAL 0)
  message(FATAL_ERROR "Failed to stage pico-ws-server into ${PICO_WS_SERVER_STAGED_DIR}")
endif()

# git apply, when it can see this repo, treats the build tree as a subdirectory
# and skips every path under the ignored build/ directory with exit status 0.
# Stop the walk at the parent of the binary directory so the patch is applied
# relative to this build tree. --directory is resolved from that directory.
get_filename_component(_picows_apply_ceiling ${CMAKE_CURRENT_BINARY_DIR} DIRECTORY)
set(_picows_git_apply
  ${CMAKE_COMMAND} -E env "GIT_CEILING_DIRECTORIES=${_picows_apply_ceiling}"
  ${GIT_EXECUTABLE} apply -p1 --whitespace=error-all
  "--directory=${PICO_WS_SERVER_STAGED_REL}"
  "${PICO_WS_SERVER_PATCH_FILE}"
)

execute_process(
  COMMAND ${_picows_git_apply}
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  RESULT_VARIABLE _stage_patch_res
)
if(NOT _stage_patch_res EQUAL 0)
  message(FATAL_ERROR "Failed to apply ${PICO_WS_SERVER_PATCH_FILE} to staged pico-ws-server")
endif()
file(READ "${PICO_WS_SERVER_STAGED_DIR}/CMakeLists.txt" _staged_pws_cmake)
if(NOT _staged_pws_cmake MATCHES "add_library\\(pico_ws_server OBJECT")
  message(FATAL_ERROR
    "pico-ws-server patch did not change ${PICO_WS_SERVER_STAGED_DIR}/CMakeLists.txt. "
    "git apply exits 0 when it skips ignored paths; refusing to configure the upstream tree.")
endif()

add_custom_command(
  OUTPUT "${PICO_WS_SERVER_STAGED_DIR}/.patched_stamp"
  DEPENDS "${PICO_WS_SERVER_PATCH_FILE}"
  COMMAND ${CMAKE_COMMAND} -E rm -rf ${PICO_WS_SERVER_STAGED_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${PICO_WS_SERVER_SRC_DIR} ${PICO_WS_SERVER_STAGED_DIR}
  COMMAND ${CMAKE_COMMAND} -E rm -f ${PICO_WS_SERVER_STAGED_DIR}/.git
  COMMAND ${_picows_git_apply}
  COMMAND ${CMAKE_COMMAND} -E touch ${PICO_WS_SERVER_STAGED_DIR}/.patched_stamp
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Staging and patching pico-ws-server in build directory (keeping submodule pristine)"
)

add_custom_target(picows_stage_pico_ws_server
  DEPENDS "${PICO_WS_SERVER_STAGED_DIR}/.patched_stamp"
)

set(
  PICO_WS_SERVER_USE_CYW43
  OFF
  CACHE BOOL
  "Use CYW43 networking in pico-ws-server"
  FORCE
)

# pico-ws-server uses structured bindings, so this directory is C++17.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

add_subdirectory(
  "${PICO_WS_SERVER_STAGED_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}/pico-ws-server-build"
)

add_dependencies(pico_ws_server picows_stage_pico_ws_server)

set_target_properties(pico_ws_server PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
)

target_include_directories(pico_ws_server BEFORE PRIVATE
  ${PICOWS_ROOT}/src/mbedtls_compat
)

set(PICOWS_TINYUSB_SOURCES
  ${PICOWS_ROOT}/src/tusb.c
  ${PICOWS_ROOT}/src/common/tusb_fifo.c
  ${PICOWS_ROOT}/src/portable/raspberrypi/rp2040/dcd_rp2040.c
  ${PICOWS_ROOT}/src/portable/raspberrypi/rp2040/rp2040_usb.c
  ${PICOWS_ROOT}/src/device/usbd.c
  ${PICOWS_ROOT}/src/class/net/ecm_rndis_device.c
  ${PICOWS_ROOT}/src/class/net/ncm_device.c
  ${PICOWS_ROOT}/hw/bsp/rp2040/family.c
)

add_library(picows STATIC
  ${PICOWS_ROOT}/src/ws.cpp
  ${PICOWS_ROOT}/src/usb_descriptors.c
  ${PICOWS_ROOT}/lib/networking/dhserver.c
  ${PICOWS_ROOT}/lib/networking/dnserver.c
  ${PICOWS_ROOT}/lib/networking/rndis_reports.c
  ${LWIP}/src/core/altcp.c
  ${LWIP}/src/core/altcp_alloc.c
  ${LWIP}/src/core/altcp_tcp.c
  ${LWIP}/src/core/def.c
  ${LWIP}/src/core/dns.c
  ${LWIP}/src/core/inet_chksum.c
  ${LWIP}/src/core/init.c
  ${LWIP}/src/core/ip.c
  ${LWIP}/src/core/mem.c
  ${LWIP}/src/core/memp.c
  ${LWIP}/src/core/netif.c
  ${LWIP}/src/core/pbuf.c
  ${LWIP}/src/core/raw.c
  ${LWIP}/src/core/stats.c
  ${LWIP}/src/core/sys.c
  ${LWIP}/src/core/tcp.c
  ${LWIP}/src/core/tcp_in.c
  ${LWIP}/src/core/tcp_out.c
  ${LWIP}/src/core/timeouts.c
  ${LWIP}/src/core/udp.c
  ${LWIP}/src/core/ipv4/autoip.c
  ${LWIP}/src/core/ipv4/dhcp.c
  ${LWIP}/src/core/ipv4/etharp.c
  ${LWIP}/src/core/ipv4/icmp.c
  ${LWIP}/src/core/ipv4/igmp.c
  ${LWIP}/src/core/ipv4/ip4.c
  ${LWIP}/src/core/ipv4/ip4_addr.c
  ${LWIP}/src/core/ipv4/ip4_frag.c
  ${LWIP}/src/netif/ethernet.c
  ${LWIP}/src/netif/slipif.c
  ${PICOWS_TINYUSB_SOURCES}
  $<TARGET_OBJECTS:pico_ws_server>
)

set_target_properties(picows PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
)

option(PICOWS_ENABLE_IPERF "Compile lwIP iperf. The application must start the server." OFF)
if(PICOWS_ENABLE_IPERF)
  target_sources(picows PRIVATE ${LWIP}/src/apps/lwiperf/lwiperf.c)
  target_compile_definitions(picows PUBLIC INCLUDE_IPERF=1)
endif()

target_include_directories(picows
  PUBLIC
    ${PICOWS_ROOT}/include
    ${LWIP}/src/include
    ${LWIP}/src/include/ipv4
    ${LWIP}/src/include/lwip/apps
  PRIVATE
    ${PICOWS_ROOT}/src
    ${PICOWS_ROOT}/lib/networking
    ${PICOWS_ROOT}/hw
    ${PICOWS_ROOT}/hw/bsp/rp2040/boards/${BOARD}
    ${PICO_WS_SERVER_STAGED_DIR}/include
)

if(NOT TINYUSB_OPT_OS)
  set(TINYUSB_OPT_OS OPT_OS_PICO)
endif()

target_compile_definitions(picows
  PUBLIC
    CFG_TUSB_MCU=OPT_MCU_RP2040
    CFG_TUSB_OS=${TINYUSB_OPT_OS}
    BUILD_PICO
    USE_ECM=1
)

if(PICO_PLATFORM STREQUAL "rp2040")
  target_compile_definitions(picows PRIVATE
    PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
    PICO_RP2040_USB_DEVICE_UFRAME_FIX=1
  )
  # dcd_rp2040.c calls this on bus reset. It is not part of pico_stdlib.
  target_sources(picows PRIVATE
    ${PICO_SDK_PATH}/src/rp2_common/pico_fix/rp2040_usb_device_enumeration/rp2040_usb_device_enumeration.c
  )
endif()

# Header-only SDK targets. Linking pico_stdlib here compiles its INTERFACE
# sources (including crt0) into the archive. The application links pico_stdlib
# and pico_unique_id once, via picows_initialize_project().
target_link_libraries(picows
  PUBLIC
    lwipopts_provider
  PRIVATE
    pico_stdlib_headers
    pico_unique_id_headers
    pico_binary_info_headers
    hardware_sync_headers
    hardware_resets_headers
    hardware_clocks_headers
    pico_fix_rp2040_usb_device_enumeration_headers
)

# hardware/sync.h includes the Hazard3 headers when the compiler is RISC-V.
# Those headers are not part of the Arm pico_stdlib_headers closure.
if(PICO_PLATFORM STREQUAL "rp2350-riscv")
  target_link_libraries(picows PRIVATE
    hardware_hazard3_headers
    hardware_riscv_headers
  )
endif()

if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
  target_compile_options(picows PRIVATE
    -Wno-error=null-dereference
    -Wno-error=conversion
    -Wno-error=sign-conversion
    -Wno-error=sign-compare
  )
endif()

#--------------------------------------------------------------------+
# Optional HTTP server: picows_http (<picows/http.h>)
# The USB network stack is provided by picows.
#--------------------------------------------------------------------+
add_library(picows_http STATIC
  ${PICOWS_ROOT}/src/http.c
  ${PICOWS_ROOT}/src/http_parse.c
)

target_include_directories(picows_http
  PUBLIC
    ${PICOWS_ROOT}/include
  PRIVATE
    ${PICOWS_ROOT}/src
)

target_link_libraries(picows_http
  PUBLIC
    picows
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_picows_example_default ON)
else()
  set(_picows_example_default OFF)
endif()
option(PICOWS_BUILD_EXAMPLE "Build the ws_echo example" ${_picows_example_default})

if(PICOWS_BUILD_EXAMPLE)
  add_subdirectory(${PICOWS_ROOT}/examples/ws_echo ${CMAKE_CURRENT_BINARY_DIR}/ws_echo)
endif()

if(PICOWS_DIST_DIR AND PICOWS_BUILD_EXAMPLE)
  add_custom_target(picows_dist ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PICOWS_DIST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:picows> "${PICOWS_DIST_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:picows_http> "${PICOWS_DIST_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:ws_echo>" "${PICOWS_DIST_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE_DIR:ws_echo>/ws_echo.uf2" "${PICOWS_DIST_DIR}/"
    COMMENT "Copying picows libraries and ws_echo into ${PICOWS_DIST_DIR}"
  )
  add_dependencies(picows_dist picows picows_http ws_echo)
endif()
