cmake_minimum_required(VERSION 3.16)

project(ColorMoveArt2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Boost REQUIRED)

#--------------------------------- Find CGAL ---------------------------------#
list(APPEND CMAKE_PREFIX_PATH "/home/geotyper/CPP_lib")
find_package(CGAL REQUIRED COMPONENTS Core)

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
    src/HelpStructures.h
    src/GeomCreate.h
    src/GeomCreate.cpp
    src/CgalMeshBuilder.h
    src/CgalMeshTypes.h
    src/CgalMeshPrimitives.cpp
    src/CgalMeshOperations.cpp
    src/CgalMeshTriangulation.cpp
    src/CgalMeshBuilderTentacles.h
    src/CgalMeshBuilderTentacles.cpp
    resources.qrc
)

add_executable(ColorMoveArt2 ${PROJECT_SOURCES})

target_link_libraries(ColorMoveArt2 PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Boost::headers
    CGAL::CGAL
)

target_include_directories(ColorMoveArt2 PRIVATE
    /home/geotyper/CPP_lib/glm
)

# Copy art_source to build directory
file(COPY art_source DESTINATION ${CMAKE_BINARY_DIR})

# Copy shaders to build directory so they can be loaded at runtime
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders DESTINATION ${CMAKE_BINARY_DIR})
