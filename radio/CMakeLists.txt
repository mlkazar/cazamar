cmake_minimum_required(VERSION 3.12)

project(Radio)

################################
# Define radio
################################
add_executable(radio)
target_sources(radio
  PRIVATE
    radio.cc
)
target_link_libraries(radio
  PRIVATE
    stream
    rst
    core
)
