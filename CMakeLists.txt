cmake_minimum_required(VERSION 2.6)
project(touchpoint)

include_directories("/usr/local/include/libevdev-1.0")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
add_executable(touchpoint main.cpp)
target_link_libraries(touchpoint xdo evdev)

install(TARGETS touchpoint RUNTIME DESTINATION bin)
