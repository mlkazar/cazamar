cmake_minimum_required(VERSION 3.12)

project(SyncCourse)

################################
# Define upcall1
################################
add_executable(upcall1)
target_sources(upcall1
  PRIVATE
    upcall-1-noref.cc
)

################################
# Define upcall2
################################
add_executable(upcall2)
target_sources(upcall2
  PRIVATE
    upcall-2-strong.cc
)

################################
# Define upcall3
################################
add_executable(upcall3)
target_sources(upcall3
  PRIVATE
    upcall-3-weak.cc
)

################################
# Define upcall4
################################
add_executable(upcall4)
target_sources(upcall4
  PRIVATE
    upcall-4-flags.cc
)
