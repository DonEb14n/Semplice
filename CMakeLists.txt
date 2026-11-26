cmake_minimum_required(VERSION 3.25)

project(Semplice VERSION 1.0.0 LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The GUI application (Windows only). On other systems only semplice_core and its tests are built.
option(SEMPLICE_BUILD_APP "Build the Semplice application" ${WIN32})
option(SEMPLICE_BUILD_TESTS "Build the unit tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
include(TagLib)

set(SEMPLICE_QT_COMPONENTS Core Sql Test)
if(SEMPLICE_BUILD_APP)
    list(APPEND SEMPLICE_QT_COMPONENTS Gui Quick QuickControls2 Network Multimedia LinguistTools)
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS ${SEMPLICE_QT_COMPONENTS})

qt_standard_project_setup(REQUIRES 6.8)

add_subdirectory(src/core)
if(SEMPLICE_BUILD_APP)
    add_subdirectory(src/platform)
    add_subdirectory(src/ui)
    add_subdirectory(src/qml)
    add_subdirectory(src/app)
endif()

if(SEMPLICE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
