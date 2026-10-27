cmake_minimum_required(VERSION 3.16)
project(DaBom VERSION 0.0.6 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(dabom WIN32
  src/main.c
  src/image.c
  src/files.c
  src/config.c
  src/actions.c
  src/ctxmenu.c
)

target_include_directories(dabom PRIVATE src)
target_compile_definitions(dabom PRIVATE UNICODE _UNICODE)
if(MSVC)
  target_compile_options(dabom PRIVATE /W3 /utf-8)
endif()

target_link_libraries(dabom PRIVATE
  user32 gdi32 comctl32 ole32 shell32 windowscodecs uuid comdlg32 dwmapi advapi32 uxtheme winspool
)

set_target_properties(dabom PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  OUTPUT_NAME "dabom"
)
