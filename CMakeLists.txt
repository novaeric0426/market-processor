cmake_minimum_required(VERSION 3.20)
project(market-data-engine
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Low-latency market data processing engine"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies ---
cmake_policy(SET CMP0167 NEW)
find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(simdjson REQUIRED)
find_package(nlohmann_json REQUIRED)

# --- Main library (all engine sources) ---
add_library(mde_lib
    src/config/config_loader.cpp
    src/feed/ws_client.cpp
    src/feed/feed_handler.cpp
    src/feed/depth_parser.cpp
    src/engine/processing_thread.cpp
)

target_include_directories(mde_lib PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(mde_lib PUBLIC
    Boost::boost
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
    yaml-cpp::yaml-cpp
    simdjson::simdjson
    nlohmann_json::nlohmann_json
)

# --- Executable ---
add_executable(market-engine src/main.cpp)
target_link_libraries(market-engine PRIVATE mde_lib)

# --- Tests ---
option(MDE_BUILD_TESTS "Build tests" ON)
if(MDE_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)

    add_executable(test_spsc_queue test/test_spsc_queue.cpp)
    target_link_libraries(test_spsc_queue PRIVATE mde_lib GTest::gtest_main)
    add_test(NAME test_spsc_queue COMMAND test_spsc_queue)

    add_executable(test_depth_parser test/test_depth_parser.cpp)
    target_link_libraries(test_depth_parser PRIVATE mde_lib GTest::gtest_main)
    add_test(NAME test_depth_parser COMMAND test_depth_parser)
endif()

# --- Benchmarks ---
option(MDE_BUILD_BENCH "Build benchmarks" OFF)
if(MDE_BUILD_BENCH)
    find_package(benchmark REQUIRED)
    # Benchmarks will be added as they are created
endif()
