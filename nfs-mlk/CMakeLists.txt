cmake_minimum_required(VERSION 3.12)

project(NfsV41)

set(CMAKE_BUILD_TYPE "Debug")

find_package(OpenSSL REQUIRED)

add_subdirectory(xdr)
add_subdirectory(srv)
