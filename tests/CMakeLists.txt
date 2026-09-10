cmake_minimum_required(VERSION 3.28)
project(fff-layout-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Network)
find_path(OBS_INCLUDE_DIR obs-module.h REQUIRED)

enable_testing()
add_executable(layout-tests layout-tests.cpp ../src/fff-session.cpp ../src/fff-http-server.cpp)
target_include_directories(layout-tests PRIVATE ../src "${OBS_INCLUDE_DIR}")
target_link_libraries(layout-tests PRIVATE Qt6::Core Qt6::Network)
add_test(NAME layout-tests COMMAND layout-tests)
