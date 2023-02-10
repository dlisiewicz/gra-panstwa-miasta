cmake_minimum_required(VERSION 3.10)
project(server)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -O0 -g -pthread")

add_executable(server server.cpp)
