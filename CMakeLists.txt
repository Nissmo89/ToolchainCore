cmake_minimum_required(VERSION 3.21)

project(ToolchainCore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 QUIET COMPONENTS Core)
if (NOT Qt6_FOUND)
    find_package(Qt5 REQUIRED COMPONENTS Core)
    set(QT_PACKAGE Qt5)
else()
    set(QT_PACKAGE Qt6)
endif()

option(TOOLCHAINCORE_ENABLE_INTERNAL_TCC "Enable the internal libtcc backend" OFF)

add_library(ToolchainCore STATIC)
add_library(ToolchainCore::ToolchainCore ALIAS ToolchainCore)
set_target_properties(ToolchainCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(ToolchainCore
    PUBLIC
        src/ccc/ccc_global.h
        src/ccc/types.h
        src/ccc/environment.h
        src/ccc/engine.h
        src/ccc/backends/compilerbackend.h
        src/ccc/backends/externalcompilerbackend.h
    PRIVATE
        src/ccc/environment.cpp
        src/ccc/engine.cpp
        src/ccc/backends/externalcompilerbackend.cpp
)

target_include_directories(ToolchainCore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(ToolchainCore
    PUBLIC
        ${QT_PACKAGE}::Core
)

target_compile_features(ToolchainCore PUBLIC cxx_std_20)
target_compile_definitions(ToolchainCore PUBLIC CCC_STATIC)

if (TOOLCHAINCORE_ENABLE_INTERNAL_TCC)
    target_compile_definitions(ToolchainCore PUBLIC TOOLCHAINCORE_ENABLE_INTERNAL_TCC)
endif()
