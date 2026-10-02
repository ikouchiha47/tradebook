cmake_minimum_required(VERSION 3.23)
project(lob CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ZLIB REQUIRED)

add_executable(lob-replay src/main.cpp src/feed.cpp src/parsers/nasdaq.cpp src/book.cpp)
target_include_directories(lob-replay PRIVATE include)
target_link_libraries(lob-replay PRIVATE ZLIB::ZLIB)

enable_testing()

find_package(GTest REQUIRED)
add_executable(lob_test tests/test_parsers.cpp tests/test_book.cpp src/parsers/nasdaq.cpp src/book.cpp)
target_include_directories(lob_test PRIVATE include)
target_link_libraries(lob_test PRIVATE GTest::gtest_main)
add_test(NAME lob COMMAND lob_test)
