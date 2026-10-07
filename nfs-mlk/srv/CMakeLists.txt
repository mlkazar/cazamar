cmake_minimum_required(VERSION 3.12)

################################
# Define nfs4_test
################################
add_executable(nfs4_test)
target_sources(nfs4_test
  PRIVATE
    main.cc
    nfsserver.cc

  PUBLIC
    FILE_SET nfsHeaders
    TYPE HEADERS
    BASE_DIRS
      .
    FILES
      nfsserver.h
)

target_link_libraries(nfs4_test
  PRIVATE
    nfssvc
)
