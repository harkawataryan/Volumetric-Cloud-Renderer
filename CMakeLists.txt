cmake_minimum_required(VERSION 3.20)
project(VolumetricClouds LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(glfw3 3.3 REQUIRED)

add_executable(clouds
  src/main.cpp
  src/renderer.cpp
  src/context.cpp
  src/gl.cpp)

target_include_directories(clouds PRIVATE src third_party)
target_link_libraries(clouds PRIVATE glfw)

if(UNIX AND NOT APPLE)
  # Headless rendering goes through surfaceless EGL (Mesa).
  find_library(EGL_LIBRARY EGL REQUIRED)
  target_link_libraries(clouds PRIVATE ${EGL_LIBRARY})
endif()

if(MSVC)
  target_compile_options(clouds PRIVATE /W3)
  target_compile_definitions(clouds PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(clouds PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

# Shaders are loaded at runtime from ./shaders relative to the working
# directory (or --shaders DIR); copy them next to the binary for convenience.
add_custom_command(TARGET clouds POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:clouds>/shaders)
