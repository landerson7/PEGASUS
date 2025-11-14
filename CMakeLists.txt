cmake_minimum_required(VERSION 3.16)
project(MyQtQuickApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Try Qt6 first; fall back to Qt5 automatically
find_package(Qt6 COMPONENTS Quick REQUIRED QUIET)
if (NOT Qt6_FOUND)
  find_package(Qt5  COMPONENTS Quick REQUIRED)
endif()

add_executable(MyQtQuickApp
  src/main.cpp
  qml/qml.qrc
)

add_executable(oled_test
    src/oled_test_main.cpp
    src/ssd1306.cpp
)


if (Qt6_FOUND)
  target_link_libraries(MyQtQuickApp PRIVATE Qt6::Quick)
else()
  target_link_libraries(MyQtQuickApp PRIVATE Qt5::Quick)
endif()
