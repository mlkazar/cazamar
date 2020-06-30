#include <osp/osp_register.h>
#include <osp/ospnet.h>
#include <rs/rs.h>
#include <rs/sdr.h>

#ifdef DEBUG
#define SDR_DEBUG   1
#else
#define SDR_DEBUG   0
#endif

MBUF_ALLOC_TAG(MB_SDR_TAG,"sdr",__FILE__);
MBUF_ALLOC_TAG(MB_SDR_COPYBACK_TAG,"sdr copyback",__FILE__);

void SDR::setChain(OspMbuf *chainp)
{
    x_mbufp = chainp;
    updateTail(x_mbufp);
}

void SDR::updateTail(OspMbuf *lastTailp)
{
    // Set x_tailmbufp to the last mbuf in the chain
    // that actually had some data.
    // This makes it possible to, for example, preallocate
    // a large chain of mbufs (because the user knows the
    // approximate size of the request).
    // The tail will be the place where the next bit of data
    // is likely to go.
    x_tailmbufp = lastTailp;
    if(lastTailp != NULL) {
        while (lastTailp) {
            if (!lastTailp->len()) x_tailmbufp = lastTailp;
            lastTailp = lastTailp->getNext();
        }

        // if the last mbuf with data is full and there's a next buffer,
        // then switch to the next buffer.
        if ((x_tailmbufp->availableTail() == 0) && x_tailmbufp->getNext()) {
            x_tailmbufp = x_tailmbufp->getNext();
        }
    }
}

int sdr_string( SDR *sdrp, char **stringpp, int stringLen)
{
    int tlen;
    int plen;
    char *tp;
    OspNetMBuf *mbufp;

    if (sdrp->x_op == SDR_DECODE) {
    
	/* decode # of bytes transmitted; note that stringLen includes
	 * the null terminator as well.
	 */
	if (!sdr_int32_t(sdrp, &tlen) || (tlen > stringLen && stringLen != ~0)) {
            #if SDR_DEBUG
            osp_assert(0);
            #endif
            return 0;
        }

	*stringpp = tp = (char *) osp_alloc(tlen);

	/* copy tlen bytes into the string, popping the buffer as
	 * we go.
	 */
        mbufp = sdrp->x_mbufp;        
	while(tlen > 0) {
	    if (!mbufp) {
                #if SDR_DEBUG
                osp_assert(0);
                #endif
		sdrp->x_mbufp = mbufp;
		return 0;
	    }
	    plen = mbufp->len();
	    if (tlen < plen)
		plen = tlen;
	    memcpy(tp, mbufp->data(), plen);
	    mbufp = mbufp->pop(plen);
	    tlen -= plen;
            tp += plen;
	}
	sdrp->x_mbufp = mbufp;	/* pop may have freed some mbufs */
	return 1;
    }
    else if (sdrp->x_op == SDR_ENCODE) {
        mbufp = sdrp->x_tailmbufp;
	tlen = strlen(*stringpp);
	if (tlen > stringLen && stringLen != ~0) {
            #if SDR_DEBUG
            osp_assert(0);
            #endif
            return 0;
        }
	tlen++;
	if ( !sdr_int32_t(sdrp, &tlen)) {
            #if SDR_DEBUG
            osp_assert(0);
            #endif
            return 0;
        }
	uint32_t bytesWritten = 0;
	OspMbuf::copyback(tlen, mbufp, *stringpp, &bytesWritten,MB_SDR_COPYBACK_TAG);
	sdrp->updateTail(sdrp->x_tailmbufp);
	if ((int)bytesWritten != tlen) {
            #if SDR_DEBUG
            osp_assert(0);
            #endif
            return 0;
        }
    }
    else if (sdrp->x_op == SDR_FREE) {
	if ((tp = *stringpp) != NULL) {
	    osp_free(tp);
	    *stringpp = NULL;
	}
    }
    return 1;
}

void
sdr_createFromMBuf( SDR *sdrp, OspNetMBuf *mbufp, sdr_encoding enc)
{
    sdrp->x_mbufp = mbufp;
    sdrp->x_op = enc;
    sdrp->updateTail(mbufp);
}

void
sdr_createFromConn( SDR *sdrp, RsConn *connp, sdr_encoding enc)
{
    sdrp->x_mbufp = sdrp->x_tailmbufp = connp->_vlanp->get(0);
    sdrp->x_op = enc;
}

void
sdr_createFromCall( SDR *sdrp, RsServerCall *callp, sdr_encoding enc)
{
    sdrp->x_mbufp = sdrp->x_tailmbufp = callp->_connp->_vlanp->get(0);
    sdrp->x_op = enc;
}

template <class C>
inline int sdr_temp( SDR *sdrp, C *iop) {

    C *tvalp;

    if (sdrp->x_op == SDR_ENCODE) {
        if(sdrp->x_tailmbufp != NULL) {
            tvalp = (C *) sdrp->x_tailmbufp->append(sizeof(C), MB_SDR_TAG,true);
            *tvalp = *iop;
            sdrp->updateTail(sdrp->x_tailmbufp);
        } else {
            #if SDR_DEBUG
            osp_assert(0);
            #endif
            return (0);
        }
    }
    else if (sdrp->x_op == SDR_DECODE) {
        OspNetMBuf *mbufp = sdrp->x_mbufp;        
        if(mbufp != NULL) {
            C tval;
            tvalp = (C *) mbufp->pullUp(sizeof(C), (char *)&tval);
            *iop = *tvalp;
            sdrp->x_mbufp = mbufp->pop(sizeof(C));
        } else {
            #if SDR_DEBUG
            osp_assert(0);
            #endif
            return (0);
        }
    }
    return 1;
}

int sdr_char( SDR *sdrp, char *iop)
{
    return sdr_temp<char>(sdrp, iop);
}

int sdr_uint8_t( SDR *sdrp, uint8_t *iop)
{
    return sdr_temp<uint8_t>(sdrp, iop);
}

int sdr_int16_t( SDR *sdrp, int16_t *iop)
{
    return sdr_temp<int16_t>(sdrp, iop);
}

int sdr_uint16_t( SDR *sdrp, uint16_t *iop)
{
    return sdr_temp<uint16_t>(sdrp, iop);
}

int sdr_uint32_t( SDR *sdrp, uint32_t *iop)
{
    return sdr_temp<uint32_t>(sdrp, iop);
}

int sdr_uint64_t( SDR *sdrp, uint64_t *iop)
{
    return sdr_temp<uint64_t>(sdrp, iop);
}

int sdr_int64_t( SDR *sdrp, int64_t *iop)
{
    return sdr_temp<int64_t>(sdrp, iop);
}

int sdr_enum( SDR *sdrp, void *iop)
{
    return sdr_temp<uint32_t>(sdrp, (uint32_t*)iop);
}

int sdr_int32_t( SDR *sdrp, int32_t *iop)
{
    return sdr_temp<int32_t>(sdrp, iop);
}

int
sdr_array( SDR * sdrs, char **addrp, uint32_t *sizep, uint32_t maxsize,
	   uint32_t elsize, sdrproc_t elproc)
{
    OSP_REGISTER uint32_t i;
    OSP_REGISTER char * target = *addrp;
    OSP_REGISTER uint32_t c;		/* the actual element count */
    OSP_REGISTER bool_t stat = 1;
    OSP_REGISTER uint32_t nodesize;

    /* FIXME: this does not look correct: MSVC 6 computes -1 / elsize here */
    i = ((~0) >> 1) / elsize;
    if (maxsize > i)
	maxsize = i;

    /* like strings, arrays are really counted arrays */
    if (!sdr_int32_t(sdrs, (int *) sizep)) {
        #if SDR_DEBUG
        osp_assert(0);
        #endif
	return (0);
    }
    c = *sizep;
    if ((c > maxsize) && (sdrs->x_op != SDR_FREE)) {
        #if SDR_DEBUG
        osp_assert(0);
        #endif
	return (0);
    }
    nodesize = c * elsize;

    /*
     * if we are deserializing, we may need to allocate an array.
     * We also save time by checking for a null array if we are freeing.
     */
    switch(sdrs->x_op) {
	case SDR_DECODE:
	    *addrp = target = (char *)osp_alloc(nodesize);
	    if (target == NULL) {
                #if SDR_DEBUG
                osp_assert(0);
                #endif
		return (0);
	    }
	    memset(target, 0, (uint32_t) nodesize);
	    break;

	case SDR_FREE:
	    if (target == NULL) return 1;
	    break;

	default:
	    break;
    }

    /*
     * now we xdr each element of array
     */
    /* Optimization to marshal/unmarshal byte-arrays faster than calling
       the individual element proc on each byte */
    if((elproc == (sdrproc_t)sdr_char) || (elproc == (sdrproc_t)sdr_uint8_t)) {
    
        OspNetMBuf *mbufp;
    
        switch(sdrs->x_op) {
            case SDR_ENCODE:
            {
                mbufp = sdrs->x_tailmbufp;
                uint32_t bytesWritten = 0;
                OspMbuf::copyback(c, mbufp, target, &bytesWritten,MB_SDR_COPYBACK_TAG);
		sdrs->updateTail(mbufp);
                if (bytesWritten != c) {
                    #if SDR_DEBUG
                    osp_assert(0);
                    #endif
                    return 0;
                }
            }
            break;
            
            case SDR_DECODE:
            {
                uint32_t plen;
            
                /* copy tlen bytes into the string, popping the buffer as we go.
                 */
                mbufp = sdrs->x_mbufp;        
                while(c > 0) {
                    if (!mbufp) {
                        sdrs->x_mbufp = mbufp;
                        #if SDR_DEBUG
                        osp_assert(0);
                        #endif
                        return 0;
                    }
                    plen = mbufp->len();
                    if (c < plen)
                        plen = c;
                    memcpy(target, mbufp->data(), plen);
                    mbufp = mbufp->pop(plen);
                    c -= plen;
                    target += plen;
                }
                sdrs->x_mbufp = mbufp;	/* pop may have freed some mbufs */
            }
            break;
                
            default:
                break;
        }
    
    } else {
        for (i = 0; (i < c) && stat; i++) {
            stat = (*elproc) (sdrs, target);
            target += elsize;
        }
    }

    /*
     * the array may need freeing
     */
    if (sdrs->x_op == SDR_FREE) {
	osp_free(*addrp);
	*addrp = NULL;
    }
    
    #if SDR_DEBUG
    osp_assert(stat != 0);
    #endif
    return (stat);
}

/* variable # of bytes */
int
sdr_bytes( SDR * sdrp, char **addrp, uint32_t *sizep, uint32_t maxsize)
{
    OSP_REGISTER char *target = *addrp;
    OSP_REGISTER uint32_t c;		/* the actual element count */
    OSP_REGISTER uint32_t nodesize;
    OspNetMBuf *mbufp;
    uint32_t plen;

    /* like strings, byte arrays are really counted arrays */
    if (!sdr_int32_t(sdrp, (int *) sizep)) {
        #if SDR_DEBUG
        osp_assert(0);
        #endif
	return (0);
    }
    nodesize = c = *sizep;
    if ((c > maxsize) && (sdrp->x_op != SDR_FREE)) {
        #if SDR_DEBUG
        osp_assert(0);
        #endif
	return (0);
    }

    /*
     * if we are deserializing, we may need to allocate an array.
     * We also save time by checking for a null array if we are freeing.
     */
    switch(sdrp->x_op) {
	case SDR_DECODE:
	    *addrp = target = (char *)osp_alloc(nodesize);
	    if (target == NULL) {
                #if SDR_DEBUG
                osp_assert(0);
                #endif
		return (0);
	    }
	    break;

	case SDR_FREE:
	    if (target == NULL) return 1;
	    break;

	default:
	    break;
    }

    if (sdrp->x_op == SDR_DECODE) {
	while(c>0) {
            if((mbufp = sdrp->x_mbufp) != NULL) {
                plen = mbufp->len();
                if (plen > c) plen = c;
                memcpy(target, mbufp->data(), plen);
                sdrp->x_mbufp = mbufp->pop(plen);
                c -= plen;
                target += plen;
            } else {
                #if SDR_DEBUG
                osp_assert(0);
                #endif
                return 0;
            }
	}
    }
    else if (sdrp->x_op == SDR_ENCODE) {
	mbufp = sdrp->x_tailmbufp;
	uint32_t bytesWritten = 0;
	OspMbuf::copyback(c, mbufp, target, &bytesWritten,MB_SDR_COPYBACK_TAG);
	sdrp->updateTail(mbufp);
	if (bytesWritten != c) {
            #if SDR_DEBUG
            osp_assert(0);
            #endif
            return 0;
        }
    }
    else if (sdrp->x_op == SDR_FREE) {
	/* the array may need freeing */
	osp_free(*addrp);
	*addrp = NULL;
    }
    return (1);
}

/* fixed # of bytes */
int
sdr_opaque( SDR * sdrp, char *addrp, uint32_t size)
{
    OspNetMBuf *mbufp;
    uint32_t plen;

    if (sdrp->x_op == SDR_DECODE) {
	while(size>0) {
            if((mbufp = sdrp->x_mbufp) != NULL) {
                plen = mbufp->len();
                if (plen > size)  plen = size;
                memcpy(addrp, mbufp->data(), plen);
                sdrp->x_mbufp = mbufp->pop(plen);
                size -= plen;
                addrp += plen;
            } else {
                #if SDR_DEBUG
                osp_assert(0);
                #endif
                return 0;
            }
	}
    }
    else if (sdrp->x_op == SDR_ENCODE) {
	uint32_t bytesWritten = 0;
	OspMbuf::copyback(size, sdrp->x_tailmbufp, addrp, &bytesWritten,MB_SDR_COPYBACK_TAG);
	sdrp->updateTail(sdrp->x_tailmbufp);
	if (bytesWritten != size) {
            #if SDR_DEBUG
            osp_assert(0);
            #endif
            return 0;
        }
    }
    return (1);
}

int
sdr_vector( SDR * sdrs, char *basep, uint32_t nelem,
	    uint32_t elemsize, sdrproc_t sdr_elem)
{
    OSP_REGISTER uint32_t i;
    OSP_REGISTER char *elptr;

    elptr = basep;
    for (i = 0; i < nelem; i++) {
	if (!(*sdr_elem) (sdrs, elptr)) {
            #if SDR_DEBUG
            osp_assert(0);
            #endif
	    return (0);
	}
	elptr += elemsize;
    }
    return (1);
}

int
sdr_SdrPipe( SDR *sdrp, SdrPipe *pipep)
{
    pipep->_sdrp = sdrp;
    if (sdrp->x_op == SDR_ENCODE) {
	/* call the marshall procedure */
	if (pipep->_marshallProcp) {
	    return pipep->_marshallProcp( pipep->_marshallContextp,
					  sdrp,
					  pipep);
        } else {
            return 1;
        }
    }

    /* free and decode just return */
    return 1;
}

void
sdr_freeMBuf(SDR *sdrp)
{
    OspNetMBuf *mbufp;
    if ((mbufp = sdrp->x_mbufp) != NULL) {
	mbufp->mfreem();
	sdrp->x_mbufp = sdrp->x_tailmbufp = NULL;
    }
}
