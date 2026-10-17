cmake_minimum_required(VERSION 3.12)

project(SyncCourse)

set(CMAKE_BUILD_TYPE "Debug")

################################
# Define upcall
################################
add_executable(upcall)
target_sources(upcall
  PRIVATE
    upcall.cc
    reader1-norefs.h
    reader2-strong.h
    reader3-weak.h
    reader4-flags.h
)
