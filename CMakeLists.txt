cmake_minimum_required(VERSION 3.20)
project(cppfabricmc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

add_executable(cppfm
    src/main.cpp
    src/core/NBT.cpp
    src/game/GameServer.cpp
)

target_include_directories(cppfm PRIVATE src src/generated)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
target_link_libraries(cppfm PRIVATE Threads::Threads ZLIB::ZLIB)

add_executable(test_golden tests/test_golden.cpp src/core/NBT.cpp)
target_include_directories(test_golden PRIVATE src src/generated)
