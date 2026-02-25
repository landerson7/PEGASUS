cmake_minimum_required(VERSION 3.16)
project(HUD_QtWidgets LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets Gui SerialPort)

add_executable(hud
  main.cpp
  HudWidget.h
  HudWidget.cpp
  DummyDataSource.h
  DataSource.h
  HudSample.h
  UartCborSource.h
  UartCborSource.cpp
)

target_link_libraries(hud PRIVATE
  Qt5::Core
  Qt5::Widgets
  Qt5::Gui
  Qt5::SerialPort
)