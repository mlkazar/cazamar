cmake_minimum_required(VERSION 3.12)

project(System)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

add_subdirectory(rpc)
add_subdirectory(voter)
add_subdirectory(fin)
add_subdirectory(mfand)
add_subdirectory(radio)
add_subdirectory(vaccine)
