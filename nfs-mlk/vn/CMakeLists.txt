cmake_minimum_required(VERSION 3.12)

project(VnLib)

################################################################
################Library Vn
################################################################
add_library(vn STATIC)
target_sources(vn
  PRIVATE
    vn.cc

  PUBLIC
    FILE_SET vnHeaders
    TYPE HEADERS
    BASE_DIRS
      .
    FILES
      vn.h
)

################################################################
################Library VnTest
################################################################
add_library(vntest STATIC)
target_sources(vntest
  PRIVATE
    vntest.cc

  PUBLIC
    FILE_SET vntestHeaders
    TYPE HEADERS
    BASE_DIRS
      .
    FILES
      vntest.h
)
