cmake_minimum_required(VERSION 3.16)
project(HUD_QtWidgets LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Sql)

qt_add_executable(hud
  main.cpp
  HudWidget.h
  HudWidget.cpp
  DataSource.h
  DummyDataSource.h
)

target_link_libraries(hud PRIVATE
  Qt6::Widgets
  Qt6::Gui
  Qt6::Sql
)
