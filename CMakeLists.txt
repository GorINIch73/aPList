cmake_minimum_required(VERSION 3.20)
project(Waysheet VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
  if(TARGET SQLite3::SQLite3)
    target_link_libraries(waysheet PRIVATE SQLite3::SQLite3)
  else()
    target_link_libraries(waysheet PRIVATE SQLite::SQLite3)
  endif()
endif()

include(FetchContent)
find_package(OpenGL REQUIRED)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  glfw
  URL https://github.com/glfw/glfw/archive/refs/tags/3.4.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
  imgui
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
  imguifiledialog
  URL https://github.com/aiekick/ImGuiFileDialog/archive/refs/tags/v0.6.8.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
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

set(CMAKE_DISABLE_FIND_PACKAGE_imgui TRUE)
FetchContent_MakeAvailable(imguifiledialog)
unset(CMAKE_DISABLE_FIND_PACKAGE_imgui)
target_link_libraries(ImGuiFileDialog PUBLIC imgui_lib)

target_link_libraries(waysheet PRIVATE imgui_lib ImGuiFileDialog glfw OpenGL::GL)
target_compile_definitions(waysheet PRIVATE
  IMGUI_USE_WCHAR32
  WAYSHEET_INSTALL_FONT_DIR="${CMAKE_INSTALL_FULL_DATADIR}/aPList/fonts"
)

install(TARGETS waysheet
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/fonts/"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/aPList/fonts"
  FILES_MATCHING PATTERN "*.ttf"
)

install(FILES "${CMAKE_SOURCE_DIR}/aPlist.desktop"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
)

install(FILES "${CMAKE_SOURCE_DIR}/assets/icons/aPlist.svg"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
)
