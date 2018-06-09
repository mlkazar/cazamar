#ifndef __RADIOS_H_ENV_

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <sys/socketvar.h>

#ifndef __linux__
#include <netinet/in_pcb.h>
#include <netinet/tcp_var.h>
#else
#include <string.h>
#endif

#endif /* RADIOS_H_ENV */
