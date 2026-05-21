cmake_minimum_required(VERSION 3.14)
project(LFGateway)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)
include_directories(gateway/include)

file(GLOB NET_SOURCES src/net/*.cpp)
file(GLOB GATEWAY_SOURCES gateway/src/*.cpp)

add_executable(gateway src/main_gateway.cpp ${NET_SOURCES} ${GATEWAY_SOURCES})
target_link_libraries(gateway pthread mysqlclient hiredis)

# add_subdirectory(console
