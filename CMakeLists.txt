cmake_minimum_required(VERSION 3.20)
project(Waysheet VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WAYSHEET_ENABLE_IMGUI "Enable Dear ImGui UI build" ON)

find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
  add_compile_definitions(WAYSHEET_HAS_SQLITE=1)
endif()

add_executable(waysheet
  src/main.cpp
  src/app.cpp
  src/fuel_math.cpp
  src/database.cpp
  src/form_manager.cpp
  src/theme_manager.cpp
)

add_custom_command(TARGET waysheet POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:waysheet>/fonts"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/fonts/NotoSans-Regular.ttf"
          "$<TARGET_FILE_DIR:waysheet>/fonts/NotoSans-Regular.ttf"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/fonts/NotoEmoji-Regular.ttf"
          "$<TARGET_FILE_DIR:waysheet>/fonts/NotoEmoji-Regular.ttf"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/fonts/fontawesome-webfont.ttf"
          "$<TARGET_FILE_DIR:waysheet>/fonts/fontawesome-webfont.ttf"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/fonts/fa-solid-900.ttf"
          "$<TARGET_FILE_DIR:waysheet>/fonts/fa-solid-900.ttf"
  COMMENT "Copying runtime fonts to output directory"
)

add_custom_target(copy_runtime_fonts ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Release/fonts"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/fonts/NotoSans-Regular.ttf"
          "${CMAKE_BINARY_DIR}/Release/fonts/NotoSans-Regular.ttf"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/fonts/NotoEmoji-Regular.ttf"
          "${CMAKE_BINARY_DIR}/Release/fonts/NotoEmoji-Regular.ttf"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/fonts/fontawesome-webfont.ttf"
          "${CMAKE_BINARY_DIR}/Release/fonts/fontawesome-webfont.ttf"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/fonts/fa-solid-900.ttf"
          "${CMAKE_BINARY_DIR}/Release/fonts/fa-solid-900.ttf"
  COMMENT "Ensuring runtime fonts are present in build/Release/fonts"
)
add_dependencies(copy_runtime_fonts waysheet)

target_include_directories(waysheet PRIVATE src)

if(MSVC)
  target_compile_options(waysheet PRIVATE /utf-8)
endif()

if(SQLite3_FOUND)
  target_link_libraries(waysheet PRIVATE SQLite::SQLite3)
endif()

if(WAYSHEET_ENABLE_IMGUI)
  include(FetchContent)
  find_package(OpenGL REQUIRED)

  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    glfw
    URL https://github.com/glfw/glfw/archive/refs/tags/3.4.zip
  )
  FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.zip
  )
  FetchContent_MakeAvailable(glfw imgui)

  add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  )
  target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
  )
  target_compile_definitions(imgui_lib PUBLIC IMGUI_USE_WCHAR32)
  target_link_libraries(imgui_lib PUBLIC glfw OpenGL::GL)

  target_link_libraries(waysheet PRIVATE imgui_lib glfw OpenGL::GL)
  target_compile_definitions(waysheet PRIVATE WAYSHEET_ENABLE_IMGUI=1 IMGUI_USE_WCHAR32)
endif()
