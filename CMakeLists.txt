cmake_minimum_required(VERSION 3.20)

set(PICOWS_ROOT ${CMAKE_CURRENT_LIST_DIR})

# Default PICO_SDK_PATH to the bundled submodule if not already set
if(NOT PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
  if(EXISTS "${PICOWS_ROOT}/submodules/pico-sdk/pico_sdk_init.cmake")
    set(PICO_SDK_PATH "${PICOWS_ROOT}/submodules/pico-sdk" CACHE PATH "Path to the Raspberry Pi Pico SDK")
  endif()
endif()

# Default BOARD to raspberry_pi_pico if not specified
if(NOT DEFINED BOARD OR "${BOARD}" STREQUAL "")
  set(BOARD raspberry_pi_pico CACHE STRING "Target board")
endif()

# If picows is built standalone (or included before family_support.cmake),
# provide a helper macro `picows_initialize_project(TARGET_NAME)` and also
# set up the 3 library targets: picows_core, picows_http, picows_ws.
include(${PICOWS_ROOT}/hw/bsp/family_support.cmake)

set(LWIP ${PICOWS_ROOT}/lib/lwip)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(picows C CXX ASM)
  family_initialize_project(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR})
endif()

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
# 1. Core Library: picows_core (TinyUSB ECM/RNDIS + lwIP + DHCP + DNS)
#--------------------------------------------------------------------+
add_library(picows_core STATIC
  ${PICOWS_ROOT}/src/core.c
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
  ${LWIP}/src/apps/lwiperf/lwiperf.c
)

target_include_directories(picows_core
  PUBLIC
    ${PICOWS_ROOT}/include
    ${LWIP}/src/include
    ${LWIP}/src/include/ipv4
    ${LWIP}/src/include/lwip/apps
  PRIVATE
    ${PICOWS_ROOT}/src
    ${PICOWS_ROOT}/lib/networking
)

target_compile_definitions(picows_core
  PUBLIC
    CFG_TUSB_MCU=OPT_MCU_RP2040
    CFG_TUSB_OS=${TINYUSB_OPT_OS}
    BUILD_PICO
    USE_ECM=1
)

target_link_libraries(picows_core
  PUBLIC
    pico_stdlib
    lwipopts_provider
  PRIVATE
    tinyusb_device
    tinyusb_board
    tinyusb_additions
)

if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
  target_compile_options(picows_core PRIVATE
    -Wno-error=null-dereference
    -Wno-error=conversion
    -Wno-error=sign-conversion
    -Wno-error=sign-compare
  )
endif()

#--------------------------------------------------------------------+
# 2. Optional HTTP Server Library: picows_http (<picows/http.h>)
#--------------------------------------------------------------------+
add_library(picows_http STATIC
  ${PICOWS_ROOT}/src/http.c
)

target_include_directories(picows_http
  PUBLIC
    ${PICOWS_ROOT}/include
  PRIVATE
    ${PICOWS_ROOT}/src
)

target_link_libraries(picows_http
  PUBLIC
    picows_core
)

#--------------------------------------------------------------------+
# 3. Optional WebSocket Server Library: picows_ws (<picows/ws.h>)
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
execute_process(
  COMMAND ${CMAKE_COMMAND} -E rm -rf ${PICO_WS_SERVER_STAGED_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${PICO_WS_SERVER_SRC_DIR} ${PICO_WS_SERVER_STAGED_DIR}
  COMMAND ${CMAKE_COMMAND} -E rm -f ${PICO_WS_SERVER_STAGED_DIR}/.git
  RESULT_VARIABLE _stage_copy_res
)
if(NOT _stage_copy_res EQUAL 0)
  message(FATAL_ERROR "Failed to stage pico-ws-server into ${PICO_WS_SERVER_STAGED_DIR}")
endif()

execute_process(
  COMMAND ${GIT_EXECUTABLE} --git-dir=${PICOWS_ROOT}/.git --work-tree=${CMAKE_CURRENT_BINARY_DIR} apply --directory=${PICO_WS_SERVER_STAGED_REL} --whitespace=error-all ${PICO_WS_SERVER_PATCH_FILE}
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  RESULT_VARIABLE _stage_patch_res
)
if(NOT _stage_patch_res EQUAL 0)
  message(FATAL_ERROR "Failed to apply ${PICO_WS_SERVER_PATCH_FILE} to staged pico-ws-server")
endif()

add_custom_command(
  OUTPUT "${PICO_WS_SERVER_STAGED_DIR}/.patched_stamp"
  DEPENDS "${PICO_WS_SERVER_PATCH_FILE}"
  COMMAND ${CMAKE_COMMAND} -E rm -rf ${PICO_WS_SERVER_STAGED_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${PICO_WS_SERVER_SRC_DIR} ${PICO_WS_SERVER_STAGED_DIR}
  COMMAND ${CMAKE_COMMAND} -E rm -f ${PICO_WS_SERVER_STAGED_DIR}/.git
  COMMAND ${GIT_EXECUTABLE} --git-dir=${PICOWS_ROOT}/.git --work-tree=${CMAKE_CURRENT_BINARY_DIR} apply --directory=${PICO_WS_SERVER_STAGED_REL} --whitespace=error-all ${PICO_WS_SERVER_PATCH_FILE}
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

add_subdirectory(
  "${PICO_WS_SERVER_STAGED_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}/pico-ws-server-build"
)

add_dependencies(pico_ws_server picows_stage_pico_ws_server)

add_library(picows_ws STATIC
  ${PICOWS_ROOT}/src/ws.cpp
)

target_include_directories(picows_ws
  PUBLIC
    ${PICOWS_ROOT}/include
  PRIVATE
    ${PICOWS_ROOT}/src
    ${PICO_WS_SERVER_STAGED_DIR}/include
)

target_link_libraries(picows_ws
  PUBLIC
    picows_core
  PRIVATE
    pico_ws_server
)
