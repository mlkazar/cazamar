set(XDR_X "${CMAKE_CURRENT_SOURCE_DIR}/nfsv41.x")
set(XDR_H "${CMAKE_CURRENT_BINARY_DIR}/nfsv41.h")
set(XDR_C "${CMAKE_CURRENT_BINARY_DIR}/nfsv41.c")
set(XDR_SVC "${CMAKE_CURRENT_BINARY_DIR}/nfsv4_svc.c")
set(XDR_CLN "${CMAKE_CURRENT_BINARY_DIR}/nfsv4_cln.c")

add_custom_command(
  OUTPUT ${XDR_H} ${XDR_C} ${XDR_CLN} ${XDR_SVC}
  COMMAND rpcgen -h -C -o ${XDR_H} ${XDR_X}
  COMMAND rpcgen -c -C -o ${XDR_C} ${XDR_X}
  COMMAND rpcgen -m -C -o ${XDR_SVC} ${XDR_X}
  COMMAND rpcgen -l -C -o ${XDR_CLN} ${XDR_X}
  DEPENDS "${XDR_X}"
)

include_directories("${CMAKE_CURRENT_BINARY_DIR}")

add_library(nfssvc STATIC)
target_compile_options(nfssvc PRIVATE -Wno-deprecated-non-prototype)
target_sources(nfssvc
  PRIVATE
    ${XDR_C}
    ${XDR_SVC}

  PUBLIC
    FILE_SET nfssvcHeaders
    TYPE HEADERS
    BASE_DIRS
      ${CMAKE_CURRENT_BINARY_DIR}
      .
    FILES
      ${XDR_H}
)
