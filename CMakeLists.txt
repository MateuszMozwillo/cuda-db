cmake_minimum_required(VERSION 3.12)
project(gpu_db CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(db_core
    src/parser.cpp
    src/mem_table.cpp
    src/engine.cpp
)

add_executable(gpu-db
    src/main.cpp
)
target_link_libraries(gpu-db PRIVATE db_core)

if(MSVC)
    target_compile_options(gpu-db PRIVATE /O2)
    target_compile_options(db_core PRIVATE /O2)
else()
    target_compile_options(gpu-db PRIVATE -O3 -march=native -Wall -Wextra)
    target_compile_options(db_core PRIVATE -O3 -march=native -Wall -Wextra)
endif()

enable_testing()

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(Catch2)

add_executable(unit_tests
    tests/test_parser.cpp
    tests/test_mem_table.cpp
    tests/test_engine.cpp
)

target_link_libraries(unit_tests PRIVATE db_core Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(unit_tests)
