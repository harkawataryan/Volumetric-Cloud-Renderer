cmake_minimum_required(VERSION 3.20)
project(VolumetricClouds LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(VolumetricClouds
    src/main.cpp
    src/Halton.h
)

# Windows / D3D11
if (WIN32)
    target_link_libraries(VolumetricClouds PRIVATE d3d11 d3dcompiler dxguid)
endif()

target_include_directories(VolumetricClouds PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_custom_command(TARGET VolumetricClouds POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:VolumetricClouds>/shaders)
