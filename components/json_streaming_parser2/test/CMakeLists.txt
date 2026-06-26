cmake_minimum_required(VERSION 3.10)
project(json_parser_test CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test_parser
    ${CMAKE_CURRENT_SOURCE_DIR}/test_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../JsonStreamingParser2.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../ElementValue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../JsonPathCallback.cpp
)

target_include_directories(test_parser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
