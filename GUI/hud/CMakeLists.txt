cmake_minimum_required(VERSION 3.16)
project(HUD_QtWidgets LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Gui SerialPort Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Gui SerialPort Sql)

add_executable(hud
  main.cpp
  HudWidget.h
  HudWidget.cpp
  DummyDataSource.h
  DataSource.h
  HudSample.h
  HudLogger.h
  HudLogger.cpp
  UartCborSource.h
  UartCborSource.cpp
)

target_link_libraries(hud PRIVATE
  Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::Widgets
  Qt${QT_VERSION_MAJOR}::Gui
  Qt${QT_VERSION_MAJOR}::SerialPort
  Qt${QT_VERSION_MAJOR}::Sql
)
