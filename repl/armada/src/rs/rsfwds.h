/* -*- mode: c++ -*-
 *
 * Copyright (c) 2008 Arriad, Inc.  All Rights Reserved.
 *
 */

/** \file Typing information for classes / objects defined in rs/rs.h
 */

#if !defined(__RSFWDS_H__)
#define __RSFWDS_H__

#include <osp/ospnet_types.h>

/* class declarations */
class RsHeader;
class Rs;
struct RsConn;
class RsServerCall;
class RsClientCall;

/* type of callback from a call */
typedef void RsResponseProc ( void (*dispatchProcp)(),
			      void *contextp,
			      OspMbuf *respDatap,
			      int32_t code);

typedef void RsServerProc( void *contextp,
			   RsServerCall *scallp,
			   OspMbuf *callDatap);

#endif /* __RSFWDS_H__ */
