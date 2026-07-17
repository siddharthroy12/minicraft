cmake_minimum_required(VERSION 3.15)
project(minicraft CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/raylib)

add_executable(minicraft
    src/main.cpp
    src/World.cpp
    src/Chunk.cpp
    src/Textures.cpp
)

target_link_libraries(minicraft PRIVATE raylib)

if(APPLE)
    target_link_libraries(minicraft PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()
