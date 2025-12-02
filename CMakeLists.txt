cmake_minimum_required(VERSION 3.16)

project(ColorMoveArt2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(PROJECT_SOURCES
    src/main.cpp
    src/SqueegeeWindow.h
    src/SqueegeeWindow.cpp
)

add_executable(ColorMoveArt2 ${PROJECT_SOURCES})

target_link_libraries(ColorMoveArt2 PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
)

# Copy art_source to build directory
file(COPY art_source DESTINATION ${CMAKE_BINARY_DIR})
