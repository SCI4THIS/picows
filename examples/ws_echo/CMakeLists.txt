set(INDEX_HTML_H "${CMAKE_CURRENT_BINARY_DIR}/index_html.h")

add_custom_command(
  OUTPUT "${INDEX_HTML_H}"
  COMMAND ${CMAKE_COMMAND}
    -DHTML=${CMAKE_CURRENT_SOURCE_DIR}/index.html
    -DOUT=${INDEX_HTML_H}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_html.cmake
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/index.html"
  VERBATIM
)

add_executable(ws_echo
  main.c
  "${INDEX_HTML_H}"
)
set_source_files_properties("${INDEX_HTML_H}" PROPERTIES
  GENERATED TRUE
  HEADER_FILE_ONLY TRUE
)
target_include_directories(ws_echo PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}"
  "${PICOWS_ROOT}/src"
)
picows_initialize_project(ws_echo)

pico_enable_stdio_uart(ws_echo 1)
pico_add_extra_outputs(ws_echo)
