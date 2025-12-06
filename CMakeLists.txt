cmake_minimum_required(VERSION 3.16)

project(ColorMoveArt2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(OPENMESH_ROOT "/home/geotyper/CPP_lib/OpenMesh" CACHE PATH "Path to OpenMesh checkout/build")
list(APPEND CMAKE_PREFIX_PATH "${OPENMESH_ROOT}/build" "${OPENMESH_ROOT}/build/Build")
find_package(OpenMesh REQUIRED CONFIG)

set(PROJECT_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/SqueegeeWindow.cpp
    src/SqueegeeWindow.h
    src/MeshViewerWidget.cpp
    src/MeshViewerWidget.h
    src/AgentProjectionWindow.cpp
    src/AgentProjectionWindow.h
    src/Noise2D.h
)

add_executable(ColorMoveArt2 ${PROJECT_SOURCES})

target_link_libraries(ColorMoveArt2 PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    OpenMeshCore
    OpenMeshTools
)

set_target_properties(ColorMoveArt2 PROPERTIES
    BUILD_RPATH "${OPENMESH_ROOT}/build/Build/lib"
)

# Copy art_source to build directory
file(COPY art_source DESTINATION ${CMAKE_BINARY_DIR})

# Copy shaders to build directory so they can be loaded at runtime
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders DESTINATION ${CMAKE_BINARY_DIR})
