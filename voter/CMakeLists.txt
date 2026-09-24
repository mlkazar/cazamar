cmake_minimum_required(VERSION 3.12)

project(VoterLib)

################################
# define contents of libvoter
################################
add_library(voter STATIC)
target_sources(voter
  PRIVATE
    voter.cc

  PUBLIC
    FILE_SET voterHeaders
    TYPE HEADERS
    BASE_DIRS
      .
    FILES
      voter.h
      votersdr.h
)

target_link_libraries(voter PRIVATE rpc core)

################################
# Define vtest
################################
add_executable(vtest)
target_sources(vtest
  PRIVATE
    vtest.cc
)
target_link_libraries(vtest
  PRIVATE
    voter
    rpc
    core
)
