cmake_minimum_required(VERSION 2.8.4)
project(Webserver)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")



find_package(Boost COMPONENTS system thread REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

add_subdirectory(AnalysisGraph)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(Webserver main.cpp server_http.hpp)

target_link_libraries(Webserver ${Boost_LIBRARIES})
target_link_libraries(Webserver analysisGraphLib)