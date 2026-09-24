cmake_minimum_required(VERSION 3.12)

project(Fin)

################################
# define contents of libvanguard
################################
add_library(vanguard STATIC)
target_sources(vanguard
  PRIVATE
    vanofx.cc profile.cc utils.cc yfdriver.cc

  PUBLIC
    FILE_SET vanHeaders
    TYPE HEADERS
    BASE_DIRS
      .
    FILES
      vanofx.h profile.h utils.h yfdriver.h
)

target_link_libraries(vanguard PRIVATE core ext)

################################
# Define yftest
################################
add_executable(yftest)
target_sources(yftest
  PRIVATE
    yftest.cc
)
target_link_libraries(yftest
  PRIVATE
    vanguard
    core
    ext
)

################################
# Define vantest
################################
add_executable(vantest)
target_sources(vantest
  PRIVATE
    vantest.cc
)
target_link_libraries(vantest
  PRIVATE
    vanguard
    core
    ext
)

################################
# Define van
################################
add_executable(van)
target_sources(van
  PRIVATE
    van.cc
    vancmd.cc
)
target_link_libraries(van
  PRIVATE
    vanguard
    core
    ext
)
