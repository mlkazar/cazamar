#include "ssls.h"

class Radios {
public:
    const SSL_METHOD *_sslMethodp;
    SSL_CTX *_sslCtxp;
    int _fd;
    int _listenFd;
    struct sockaddr_in _serverAddr;

    Radios() {
        SSL_library_init();

        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();

        _sslMethodp = NULL;
        _sslCtxp = NULL;
    }

    int32_t init() {
        int32_t code;
        int opt;

        _listenFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef __linux__
        _sslMethodp = TLS_server_method();
#else
        _sslMethodp = TLSv1_2_server_method();
#endif
        _sslCtxp = SSL_CTX_new(_sslMethodp);

        if (SSL_CTX_load_verify_locations(_sslCtxp, "test_cert.pem", "test_key.pem") != 1) {
            printf("failing location test\n");
            ERR_print_errors_fp(stdout);
            return -1;
        }
        if (SSL_CTX_set_default_verify_paths(_sslCtxp) != 1) {
            printf("failing path verification\n");
            ERR_print_errors_fp(stdout);
            return -1;
        }
        if (SSL_CTX_use_certificate_file(_sslCtxp, "test_cert.pem", SSL_FILETYPE_PEM) <= 0) {
            printf("failing use cert\n");
            ERR_print_errors_fp(stdout);
            return -1;
        }
        if (SSL_CTX_use_PrivateKey_file(_sslCtxp, "test_key.pem", SSL_FILETYPE_PEM) <= 0) {
            printf("failing use key\n");
            ERR_print_errors_fp(stdout);
            return -1;
        }
        if (!SSL_CTX_check_private_key(_sslCtxp)) {
            printf("failing final check\n");
            ERR_print_errors_fp(stdout);
            return -1;
        }

        opt = 1;
        code = setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        if (code < 0) {
            printf("Rpc: reuseaddr code %d failed\n", errno);
            return -1;
        }

        memset(&_serverAddr, 0, sizeof(_serverAddr));
        _serverAddr.sin_family = AF_INET;
        _serverAddr.sin_port = htons(8234);
        _serverAddr.sin_addr.s_addr = INADDR_ANY;
        code = bind(_listenFd, (struct sockaddr *) &_serverAddr, sizeof(_serverAddr));
        if (code < 0) {
            perror("bind");
            return -1;
        }

        if (_listenFd < 0) {
            perror("socket");
            return -1;
        }

        return 0;
    }

    void ShowCerts(SSL* ssl)
    {   X509 *cert;
        char *line;

        cert = SSL_get_peer_certificate(ssl); /* Get certificates (if available) */
        if ( cert != NULL )
            {
                printf("Server certificates:\n");
                line = X509_NAME_oneline(X509_get_subject_name(cert), 0, 0);
                printf("Subject: %s\n", line);
                free(line);
                line = X509_NAME_oneline(X509_get_issuer_name(cert), 0, 0);
                printf("Issuer: %s\n", line);
                free(line);
                X509_free(cert);
            }
        else
            printf("No certificates.\n");
    }

    int32_t acceptLoop() {
        int32_t code;
        SSL *sslp;
        char tbuffer[100];
        const char *outBufferp;
        while(1) {
            code = listen(_listenFd, 10);
            if (code < 0) {
                perror("listen");
                return -1;
            }

            _fd = accept(_listenFd, 0, 0);
            if (_fd < 0) {
                perror("accept");
                break;
            }

            sslp = SSL_new(_sslCtxp);
            SSL_set_fd(sslp, _fd);
            SSL_accept(sslp);

            ShowCerts(sslp);

            while(1) {
                code = SSL_read(sslp, tbuffer, sizeof(tbuffer));
                if (code > 0) {
                    printf("ssl read code=%d, data=%s\n", code, tbuffer);
                    break;
                }
                else {
                    break;
                }
            }
            printf("sending response\n");
            outBufferp = "<html><body>Hello from secure server!</body></html>\n\n";
            code = SSL_write(sslp, outBufferp, strlen(outBufferp));
            printf("write code=%d\n", code);
            SSL_free(sslp);
            close(_fd);
        }
        return 0;
    }
};

int
main(int argc, char **argv)
{
    Radios radioServer;

    radioServer.init();
    radioServer.acceptLoop();
    printf("done\n");
    return 0;
}
