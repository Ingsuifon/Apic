cmake_minimum_required(VERSION 3.21)
project(Alic)

set(CMAKE_CXX_STANDARD 14)
add_subdirectory(gflags-2.2.2)
add_subdirectory(lz4-1.9.4)
#
link_libraries(gflags::gflags)

message(echo ${CMAKE_CURRENT_SOURCE_DIR})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

add_executable(IntegerConvertor IntegerConvertor.cpp IntegerConvertor.h FileOperator.h AthEncoding.h AthCompressor.h)

add_executable(DecodingTest DecodingTest.cpp FileOperator.h AthEncoding.h AthCompressor.h)

add_definitions(-DUSEUPDATE)