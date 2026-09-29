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
    src/query.cpp
    src/cpu_executor.cpp
)

find_package(Threads REQUIRED)
target_link_libraries(db_core PUBLIC Threads::Threads)

add_executable(gpu-db
    src/main.cpp
)
target_link_libraries(gpu-db PRIVATE db_core)

add_executable(benchmark
    benchmark/benchmark.cpp
)
target_link_libraries(benchmark PRIVATE db_core)

if(MSVC)
    target_compile_options(gpu-db PRIVATE /O2)
    target_compile_options(db_core PRIVATE /O2)
    target_compile_options(benchmark PRIVATE /O2)
else()
    target_compile_options(gpu-db PRIVATE -O3 -march=native -Wall -Wextra)
    target_compile_options(db_core PRIVATE -O3 -march=native -Wall -Wextra)
    target_compile_options(benchmark PRIVATE -O3 -march=native -Wall -Wextra)
endif()

option(GPU_DB_ENABLE_CUDA "Build the CUDA query executor" ON)

if(GPU_DB_ENABLE_CUDA)
    if(NOT DEFINED CMAKE_CUDA_COMPILER AND EXISTS /usr/local/cuda/bin/nvcc)
        set(CMAKE_CUDA_COMPILER /usr/local/cuda/bin/nvcc)
    endif()

    include(CheckLanguage)
    check_language(CUDA)

    if(CMAKE_CUDA_COMPILER)
        if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
            set(CMAKE_CUDA_ARCHITECTURES native)
        endif()

        enable_language(CUDA)
        set(CMAKE_CUDA_STANDARD 20)
        set(CMAKE_CUDA_STANDARD_REQUIRED ON)

        find_package(CUDAToolkit REQUIRED)

        add_library(db_gpu
            src/gpu_executor.cu
        )
        target_link_libraries(db_gpu PUBLIC db_core CUDA::cudart)
        target_compile_definitions(db_gpu PUBLIC GPU_DB_HAS_CUDA)
        target_compile_options(db_gpu PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:-O3 --expt-relaxed-constexpr -Xcompiler=-Wall,-Wextra>
        )

        target_link_libraries(benchmark PRIVATE db_gpu)

        message(STATUS "CUDA executor enabled (nvcc ${CMAKE_CUDA_COMPILER_VERSION}, architectures: ${CMAKE_CUDA_ARCHITECTURES})")
    else()
        message(WARNING "CUDA compiler not found, building without the GPU executor")
        set(GPU_DB_ENABLE_CUDA OFF)
    endif()
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
    tests/test_query.cpp
    tests/test_aggregation.cpp
    tests/test_cpu_executor.cpp
)

target_link_libraries(unit_tests PRIVATE db_core Catch2::Catch2WithMain)
if(GPU_DB_ENABLE_CUDA)
    target_link_libraries(unit_tests PRIVATE db_gpu)
endif()

include(CTest)
include(Catch)
catch_discover_tests(unit_tests)
