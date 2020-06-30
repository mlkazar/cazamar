#ifndef __SDR_H_ENV__
#define __SDR_H_ENV__ 1

#include <osp/osptypes.h>

class OspMbuf;
struct RsConn;
class RsServerCall;

typedef int bool_t;

enum sdr_encoding {
    SDR_ENCODE = 0,
    SDR_DECODE = 1,
    SDR_FREE = 2
};

class SDR {
 public:
    OspMbuf *x_mbufp;
    OspMbuf *x_tailmbufp;
    sdr_encoding x_op;

    inline OspMbuf *getChain() {
	return x_mbufp;
    }

    void setChain(OspMbuf *chainp);

    void updateTail(OspMbuf *lastTailp);
};

extern void sdr_createFromConn( SDR *sdrp, RsConn *connp, sdr_encoding e);

extern void sdr_createFromCall( SDR *sdrp,
				RsServerCall *callp,
				sdr_encoding e);

extern void sdr_createFromMBuf( SDR *sdrp, OspMbuf *mbufp, sdr_encoding e);

typedef bool_t (*sdrproc_t)(SDR *sdrp, void *datap);

class SdrPipe {
 public:
    SDR *_sdrp;
    int32_t (*_marshallProcp)(void *contextp, SDR *sdrp, SdrPipe *pipep);
    void *_marshallContextp;

    inline void setChain(OspMbuf *mbufp) {
	_sdrp->setChain(mbufp);
    }

    inline OspMbuf *getChain() {
	return _sdrp->getChain();
    }
};

int sdr_SdrPipe( SDR *sdrp, SdrPipe *pipep);
int sdr_char( SDR *sdrp, char *iop);
int sdr_int16_t( SDR *sdrp, int16_t *iop);
int sdr_int32_t( SDR *sdrp, int32_t *iop);
int sdr_uint8_t( SDR *sdrp, uint8_t *iop);
int sdr_uint16_t( SDR *sdrp, uint16_t *iop);
int sdr_uint32_t( SDR *sdrp, uint32_t *iop);
int sdr_uint64_t( SDR *sdrp, uint64_t *iop);
int sdr_int64_t( SDR *sdrp, int64_t *iop);
int sdr_enum( SDR *sdrp, void *iop);
int sdr_string( SDR *sdrp, char **stringpp, int stringLen);
int sdr_bytes( SDR * sdrp, char **addrp, uint32_t *sizep, uint32_t maxsize);
int sdr_opaque( SDR * sdrp, char *addrp, uint32_t size);
int sdr_array( SDR *sdrp,
	       char **valpp,
	       uint32_t *lenp,
	       uint32_t eltSize,
	       uint32_t count,
	       sdrproc_t sdrProcp);

int sdr_vector( SDR * sdrs,
		char *basep,
		uint32_t nelem,
		uint32_t elemsize,
		sdrproc_t sdr_elem);
void sdr_freeMBuf(SDR *sdrp);

#endif /* __SDR_H_ENV__ */
