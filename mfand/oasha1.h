#ifndef __OASHA1_H_ENV_
#define __OASHA1_H_ENV_ 1

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* header */

#define HASH_LENGTH 20
#define BLOCK_LENGTH 64

union _buffer {
	uint8_t b[BLOCK_LENGTH];
	uint32_t w[BLOCK_LENGTH/4];
};

union _state {
	uint8_t b[HASH_LENGTH];
	uint32_t w[HASH_LENGTH/4];
};

typedef struct sha1nfo {
	union _buffer buffer;
	uint8_t bufferOffset;
	union _state state;
	uint32_t byteCount;
	uint8_t keyBuffer[BLOCK_LENGTH];
	uint8_t innerHash[HASH_LENGTH];
} sha1nfo;

void sha1_init(sha1nfo *s);

void sha1_write(sha1nfo *s, const char *data, size_t len);

uint8_t* sha1_result(sha1nfo *s);

#ifdef __cplusplus
}       /* extern "C" */
#endif  /* __cplusplus */

#endif /* __OASHA1_H_ENV_ */
