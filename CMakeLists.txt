cmake_minimum_required(VERSION 3.20)

project(ImageCompare
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Cross-platform image comparison tool"
)

# ---- C++ Standard ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Qt6 Configuration ----
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Widgets
    Gui
    Concurrent
    Test
)

# ---- Compiler Warnings ----
if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- Subdirectories ----
add_subdirectory(src)

# ---- Testing ----
enable_testing()
add_subdirectory(tests)
