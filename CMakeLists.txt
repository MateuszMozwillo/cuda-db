cmake_minimum_required(VERSION 3.12)
project(gpu_tsdb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(gpu-db
    src/main.cpp
    src/parser.cpp
)

if(MSVC)
    target_compile_options(gpu-db PRIVATE /O2)
else()
    target_compile_options(gpu-db PRIVATE -O3 -march=native -Wall -Wextra)
endif()