if(NOT HTML OR NOT OUT)
  message(FATAL_ERROR "embed_html.cmake requires -DHTML= and -DOUT=")
endif()

file(READ "${HTML}" content)
string(REPLACE "\\" "\\\\" content "${content}")
string(REPLACE "\"" "\\\"" content "${content}")
string(REPLACE "\r" "" content "${content}")
string(REPLACE "\n" "\\n" content "${content}")

file(WRITE "${OUT}"
"/* Generated from index.html. Do not edit. */\nstatic const char index_html[] = \"${content}\";\n")
