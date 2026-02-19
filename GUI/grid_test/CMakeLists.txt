cmake_minimum_required(VERSION 3.16)
project(grid_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui)

qt_add_executable(grid_test
  src/main.cpp
  src/GridWidget.h
  src/GridWidget.cpp
)

target_link_libraries(grid_test PRIVATE Qt6::Widgets Qt6::Gui)