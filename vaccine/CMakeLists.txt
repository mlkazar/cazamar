cmake_minimum_required(VERSION 3.12)

project(Vaccine)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

################################
# Define zipdecode
################################
add_executable(zipdecode)
target_sources(zipdecode
  PRIVATE
    zipdecode.cc
)
target_link_libraries(zipdecode
  PRIVATE
    ext
    rpc
    core
)

################################
# Define ritestores
################################
add_executable(ritestores)
target_sources(ritestores
  PRIVATE
    ritestores.cc
)
target_link_libraries(ritestores
  PRIVATE
    rst
    ext
    rpc
    core
    OpenSSL::SSL
    OpenSSL::Crypto
)

################################
# Define ritecheck
################################
add_executable(ritecheck)
target_sources(ritecheck
  PRIVATE
    ritecheck.cc
    distutils.cc
)
target_link_libraries(ritecheck
  PRIVATE
    ext
    rpc
    core
    rst
    OpenSSL::SSL
    OpenSSL::Crypto
)

################################
# Define walcheck
################################
add_executable(walcheck)
target_sources(walcheck
  PRIVATE
    walcheck.cc
)
target_link_libraries(walcheck
  PRIVATE
    ext
    rpc
    core
    rst
    OpenSSL::SSL
    OpenSSL::Crypto
)

################################
# Define tsp
################################
add_executable(tsp)
target_sources(tsp
  PRIVATE
    tsp.cc
    distutils.cc
)
target_link_libraries(tsp
  PRIVATE
    ext
    rpc
    core
    CURL::libcurl
)
