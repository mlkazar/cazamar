cmake_minimum_required(VERSION 3.12)

project(MusicFanD)

################################
# define contents of liboauth
################################
add_library(oauth STATIC)
target_sources(oauth
  PRIVATE
    oahash.c oauth.c oasha1.c oapass.c oaxmalloc.c

  PUBLIC
    FILE_SET oauthHeaders
    TYPE HEADERS
    BASE_DIRS
      .
    FILES
      oasha1.h
      oauth.h
      oaxmalloc.h
)
target_link_libraries(oauth PRIVATE rpc core)

################################
# libupnp.a
################################
add_library(upnp STATIC)
target_sources(upnp
  PRIVATE
    upnp.cc radixtree.cc

  PUBLIC
    FILE_SET upnpHeaders
    TYPE HEADERS
    BASE_DIRS
      .
    FILES
      upnp.h
      radixtree.h
)
target_link_libraries(upnp PRIVATE rpc core)

################################
# libmf.a
################################
add_library(mf STATIC)
target_sources(mf
  PRIVATE
    mfclient.cc

  PUBLIC
    FILE_SET mfHeaders
    TYPE HEADERS
    BASE_DIRS
      .
    FILES
      mfclient.h
)
target_link_libraries(mf PRIVATE rpc core)

################################
# librst.a
################################
add_library(rst STATIC)
target_sources(rst
  PRIVATE
    rst.cc
    bufsocket.cc
    buftls.cc
    bufgen.cc
    xapi.cc
    xapipool.cc
    sapi.cc
    sapilogin.cc
    jwt.cc

  PUBLIC
    FILE_SET rstHeaders
    TYPE HEADERS
    BASE_DIRS
      .
    FILES
      rst.h
      bufsocket.h
      buftls.h
      bufgen.h
      xapi.h
      xapipool.h
      sapi.h
      sapilogin.h
      jwt.h
)
target_link_libraries(rst PRIVATE rpc core ext OpenSSL::SSL)

################################
# libjsdb.a
################################
add_library(jsdb STATIC)
target_sources(jsdb
  PRIVATE
    jsdb.cc

  PUBLIC
    FILE_SET jsbHeaders
    TYPE HEADERS
    BASE_DIRS
      .
    FILES
      jsdb.h
)
target_link_libraries(jsdb PRIVATE core rpc ext)

################################
# liblfs.a
################################
add_library(lfs STATIC)
target_sources(lfs
  PRIVATE
    walkdisp.cc

  PUBLIC
    FILE_SET lfsHeaders
    TYPE HEADERS
    BASE_DIRS
      .
    FILES
      walkdisp.h
)
target_link_libraries(lfs PRIVATE core)

################################
# libcfs.a
################################
add_library(cfs STATIC)
target_sources(cfs
  PRIVATE
    cfs.cc
    cfsms.cc

  PUBLIC
    FILE_SET cfsHeaders
    TYPE HEADERS
    BASE_DIRS
      .
    FILES
      cfs.h
      cfsms.h
)
target_link_libraries(cfs PRIVATE core OpenSSL::SSL)

################################
# libupload.a
################################
add_library(upload STATIC)
target_sources(upload
  PRIVATE
    upload.cc

  PUBLIC
    FILE_SET uploadHeaders
    TYPE HEADERS
    BASE_DIRS
      .
    FILES
      upload.h
)
target_link_libraries(upload PRIVATE core OpenSSL::SSL)

################################
# libstream.a
################################
add_library(stream STATIC)
target_sources(stream
  PRIVATE
    radiostream.cc
    radioscan.cc

  PUBLIC
    FILE_SET streamHeaders
    TYPE HEADERS
    BASE_DIRS
      .
    FILES
      radiostream.h
      radioscan.h
)
target_link_libraries(stream PRIVATE core)

################################
# Define jsdbtest
################################
add_executable(jsdbtest)
target_sources(jsdbtest
  PRIVATE
    jsdbtest.cc
)
target_link_libraries(jsdbtest
  PRIVATE
    jsdb
    core
    ext
)

################################
# Define strload
################################
add_executable(strload)
target_sources(strload
  PRIVATE
    strload.cc
    strdb.cc
)
target_link_libraries(strload
  PRIVATE
    stream
    rst
    oauth
    jsdb
    core
    ext
)

################################
# Define scantest
################################
add_executable(scantest)
target_sources(scantest
  PRIVATE
    scantest.cc
)
target_link_libraries(scantest
  PRIVATE
    rst
    stream
    oauth
    jsdb
    core
    ext
)

################################
# Define xapitest
################################
add_executable(xapitest)
target_sources(xapitest
  PRIVATE
    xapitest.cc
)
target_link_libraries(xapitest
  PRIVATE
    rst
    rpc
    core
    OpenSSL::SSL
    OpenSSL::Crypto
)

################################
# Define sapitest
################################
add_executable(sapitest)
target_sources(sapitest
  PRIVATE
    sapitest.cc
)
target_link_libraries(sapitest
  PRIVATE
    rst
    rpc
    core
    OpenSSL::SSL
    OpenSSL::Crypto
)

################################
# Define keyserv
################################
add_executable(keyserv)
target_sources(keyserv
  PRIVATE
    keyserv.cc
)
target_link_libraries(keyserv
  PRIVATE
    rst
    rpc
    core
    OpenSSL::SSL
    OpenSSL::Crypto
)

################################
# Define apptest
################################
add_executable(apptest)
target_sources(apptest
  PRIVATE
    apptest.cc
)
target_link_libraries(apptest
  PRIVATE
    rst
    rpc
    core
    OpenSSL::SSL
    OpenSSL::Crypto
)

################################
# Define cfstest
################################
add_executable(cfstest)
target_sources(cfstest
  PRIVATE
    cfstest.cc
)
target_link_libraries(cfstest
  PRIVATE
    rst
    rpc
    cfs
    OpenSSL::SSL
    OpenSSL::Crypto
)

################################
# Define uptest
################################
add_executable(uptest)
target_sources(uptest
  PRIVATE
    uptest.cc
)
target_link_libraries(uptest
  PRIVATE
    rst
    rpc
    cfs
    lfs
    upload
    OpenSSL::SSL
    OpenSSL::Crypto
)

################################
# Define walktest
################################
add_executable(walktest)
target_sources(walktest
  PRIVATE
    walktest.cc
)
target_link_libraries(walktest
  PRIVATE
    rst
    rpc
    cfs
    lfs
    upload
    OpenSSL::SSL
    OpenSSL::Crypto
)

################################
# Define mfand
################################
add_executable(mfand)
target_sources(mfand
  PRIVATE
    mfand.cc
)
target_link_libraries(mfand
  PRIVATE
    rst
    mf
    ext
    core
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
)

################################
# Define mfanc
################################
add_executable(mfanc)
target_sources(mfanc
  PRIVATE
    mfanc.cc
)
target_link_libraries(mfanc
  PRIVATE
    rst
    mf
    ext
    core
)

################################
# Define ssls
################################
add_executable(ssls)
target_sources(ssls
  PRIVATE
    ssls.cc
)
target_link_libraries(ssls
  PRIVATE
    core
    OpenSSL::SSL
    OpenSSL::Crypto
)

################################
# Define sslc
################################
add_executable(sslc)
target_sources(sslc
  PRIVATE
    sslc.cc
)
target_link_libraries(sslc
  PRIVATE
    core
    OpenSSL::SSL
    OpenSSL::Crypto
)

################################
# Define upnptest
################################
add_executable(upnptest)
target_sources(upnptest
  PRIVATE
    upnptest.cc
)
target_link_libraries(upnptest
  PRIVATE
    upnp
    rst
    core
    oauth
    OpenSSL::SSL
    OpenSSL::Crypto
)

################################
# Define idtest
################################
add_executable(idtest)
target_sources(idtest
  PRIVATE
    idtest.cc
    id.cc
    streams.cc
)
target_link_libraries(idtest
  PRIVATE
    rst
    core
)

################################
# Define jwttest
################################
add_executable(jwttest)
target_sources(jwttest
  PRIVATE
    jwttest.cc
    id.cc
    streams.cc
)
target_link_libraries(jwttest
  PRIVATE
    rst
    core
)
