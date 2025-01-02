cmake_minimum_required(VERSION 3.31)

project(gamedata-gen LANGUAGES CXX)

find_package(PkgConfig)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pkg_check_modules(libelf libelf REQUIRED IMPORTED_TARGET)

add_executable(gamedata-gen)

target_sources(gamedata-gen
    PRIVATE
    src/formatter.cpp
    src/formatter.hpp
    src/parser.cpp
    src/parser.hpp
    src/reader.cpp
    src/reader.hpp
    src/main.cpp
)

target_link_libraries(gamedata-gen
    PRIVATE
    PkgConfig::libelf
)
