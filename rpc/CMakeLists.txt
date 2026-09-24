cmake_minimum_required(VERSION 3.12)

project(RpcLib)

################################
# Define contents of librpc.a
################################
add_library(rpc STATIC)
target_sources(rpc
  PRIVATE
    rpc.cc
    restcall.cc
    sdr.cc

  PUBLIC
    FILE_SET rpcHeaders
    TYPE HEADERS
    BASE_DIRS
      .
    FILES
      rpc.h
      restcall.h
      sdr.h
)

################################
# Define contents of libcore.a
################################
add_library(core STATIC)
target_sources(core
  PRIVATE
    osp.cc
    cthread.cc
    cdisp.cc
    osptimer.cc

  PUBLIC
    FILE_SET coreHeaders
    TYPE HEADERS
    BASE_DIRS
      .
    FILES
      ospmbuf.h
      cdisp.h
      cthread.h
      dqueue.h
      osp.h
      ospmbuf.h
      osptimer.h
      osptypes.h
)

################################
# Define contents of libext.a
################################
add_library(ext STATIC)
target_sources(ext
  PRIVATE
    xgml.cc
    json.cc
    jsonprinter.cc

  PUBLIC
    FILE_SET extHeaders
    TYPE HEADERS
    BASE_DIRS
      .
    FILES
      json.h jsonprint.h xgml.h
)

################################
# Define rpctest
################################
add_executable(rpctest)
target_sources(rpctest
  PRIVATE
    rpctest.cc
)
target_link_libraries(rpctest
  PRIVATE
    core
    rpc
)

################################
# Define rpcshutdowntest
################################
add_executable(rpcshutdowntest)
target_sources(rpcshutdowntest
  PRIVATE
    rpcshutdowntest.cc
)
target_link_libraries(rpcshutdowntest
  PRIVATE
    core
    rpc
    ext
)

################################
# Define cdisptest
################################
add_executable(cdisptest)
target_sources(cdisptest
  PRIVATE
    cdisptest.cc
)
target_link_libraries(cdisptest
  PRIVATE
    core
)

################################
# Define timertest
################################
add_executable(timertest)
target_sources(timertest
  PRIVATE
    timertest.cc
)
target_link_libraries(timertest
  PRIVATE
    core
)

################################
# Define jsontest
################################
add_executable(jsontest)
target_sources(jsontest
  PRIVATE
    jsontest.cc
)
target_link_libraries(jsontest
  PRIVATE
    ext
)

################################
# Define jsonprinter
################################
add_executable(jsonprinter)
target_sources(jsonprinter
  PRIVATE
    jsonprinter.cc
)
target_link_libraries(jsonprinter
  PRIVATE
    ext
)

################################
# Define xgmltest
################################
add_executable(xgmltest)
target_sources(xgmltest
  PRIVATE
    xgmltest.cc
)
target_link_libraries(xgmltest
  PRIVATE
    ext
)
