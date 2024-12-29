cmake_minimum_required(VERSION 3.31)

project(vt LANGUAGES CXX)

# TODO is 32bit necessary?
set(PKG_CONFIG_EXECUTABLE /usr/bin/i686-pc-linux-gnu-pkg-config)

find_package(PkgConfig)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pkg_check_modules(libelf libelf REQUIRED IMPORTED_TARGET)

add_executable(vt main.cpp)

target_compile_options(vt
    PRIVATE
    -m32
)

target_link_libraries(vt
    PRIVATE
    PkgConfig::libelf
)

target_link_options(vt
    PRIVATE
    -m32
)