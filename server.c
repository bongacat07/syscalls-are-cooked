#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>   
#include <stdlib.h>

#define log_warn(fmt, ...) \
    fprintf(stderr, "WARN: " fmt "\n", ##__VA_ARGS__)

#define log_fatal(fmt, ...)            \
    do {                               \
        fprintf(stderr, "FATAL: " fmt "\n", ##__VA_ARGS__); \
        exit(EXIT_FAILURE);            \
    } while (0)


#define LISTEN_BACKLOG 10
#define RECVBUFF_SIZE 1024
#define MAX_SOCKET_RETRIES 3
#define BACKOFF_US 100000
#define SEND_BUFF_SIZE (RECVBUFF_SIZE + 10)

int main(int argc, char *argv[])
{
    int sockfd, connfd, bindfd, listenfd;

    for (uint32_t attempt = 0; attempt < MAX_SOCKET_RETRIES; attempt++) {

        sockfd = socket(AF_INET, SOCK_STREAM, 0);

        if (sockfd >= 0) break;

        switch (errno) {

        case EMFILE:
        case ENFILE:
        case ENOMEM:
        case ENOBUFS:
            log_warn("socket() resource exhausted (attempt %u): %s",
                     attempt, strerror(errno));

            usleep(BACKOFF_US << attempt);
            continue;

        case EACCES:
        case EAFNOSUPPORT:
        case EPROTONOSUPPORT:

            log_fatal("socket() config error: %s", strerror(errno));

        default:

            log_fatal("socket() unexpected errno=%d: %s",
                      errno, strerror(errno));
        }
    }

    
    if (sockfd < 0) {
        log_fatal("socket() failed after retries");
    }

    
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv;

    memset(&serv, 0, sizeof(serv));

    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = INADDR_ANY;
    serv.sin_port = htons(8080);

    bindfd = bind(sockfd, (struct sockaddr *)&serv, sizeof(serv));

    if (bindfd == -1) {

        switch (errno) {

        case EBADF:
        case ENOTSOCK:
        case EINVAL:
        case EFAULT:
            perror("bind: programming error");
            close(sockfd);
            exit(EXIT_FAILURE);

        case EACCES:
        case EROFS:
            perror("bind: permission denied");
            close(sockfd);
            exit(EXIT_FAILURE);

        case ENOMEM:
            perror("bind: out of memory");
            close(sockfd);
            exit(EXIT_FAILURE);

        case EADDRINUSE:
            perror("bind: address in use");
            close(sockfd);
            exit(EXIT_FAILURE);

        case EADDRNOTAVAIL:
            perror("bind: address not available");
            close(sockfd);
            exit(EXIT_FAILURE);

        default:
            perror("bind: unknown error");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    listenfd = listen(sockfd, LISTEN_BACKLOG);

    if (listenfd == -1) {

        switch (errno) {

        case EBADF:
        case ENOTSOCK:
        case EINVAL:
        case EOPNOTSUPP:
            perror("listen: programming error");
            close(sockfd);
            exit(EXIT_FAILURE);

        case ENOMEM:
            perror("listen: out of memory");
            close(sockfd);
            exit(EXIT_FAILURE);

        case EADDRINUSE:
            perror("listen: address in use");
            close(sockfd);
            exit(EXIT_FAILURE);

        default:
            perror("listen: unknown error");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    struct sockaddr_in client;

    for (;;) {

        socklen_t len = sizeof(client);

        connfd = accept(sockfd,
                        (struct sockaddr *)&client,
                        &len);

        if (connfd == -1) {

            switch (errno) {

            /* Transient / retry */
            case EINTR:
            case ECONNABORTED:
            case EAGAIN:
            
                continue;

            /* Resource */
            case EMFILE:
            case ENFILE:
            case ENOBUFS:
            case ENOMEM:
                perror("accept: resource exhausted");
                sleep(1);
                continue;

            /* Programming / fatal */
            case EBADF:
            case ENOTSOCK:
            case EINVAL:
                perror("accept: programming error");
                close(sockfd);
                exit(EXIT_FAILURE);

            default:
                perror("accept: unknown error");
                continue;
            }
        }

        char buff[RECVBUFF_SIZE];
        ssize_t n;

        while ((n = recv(connfd, buff, sizeof(buff), 0)) > 0) {

            if (n > 0 && buff[n - 1] == '\n') {
                n--;
            }

            const char suffix[] = " Received\n";
            size_t m = sizeof(suffix) - 1;

            char out[SEND_BUFF_SIZE];

            memcpy(out, buff, n);
            memcpy(out + n, suffix, m);

            printf("String: %.*s\n", (int)(n + m), out);

            ssize_t sent = send(connfd, out, n + m, 0);

            if (sent == -1) {

                switch (errno) {

                /* Transient / retry */
                case EINTR:
                case EAGAIN:
                    /* In production, should retry send() */
                    log_warn("send: transient error: %s", strerror(errno));
                    continue;

                /* Connection issues */
                case ECONNRESET:
                case EPIPE:
                case ENOTCONN:
                    perror("send: connection broken");
                    break;

                /* Resource exhaustion */
                case ENOBUFS:
                case ENOMEM:
                    perror("send: resource exhausted");
                    break;

                /* Programming errors */
                case EBADF:
                case ENOTSOCK:
                case EINVAL:
                case EFAULT:
                    perror("send: programming error");
                    close(connfd);
                    close(sockfd);
                    exit(EXIT_FAILURE);

                default:
                    perror("send: unknown error");
                    break;
                }

                break; /* Exit recv loop on send failure */
            }

            /* Handle partial send */
            if (sent < (ssize_t)(n + m)) {
                log_warn("send: partial write (%zd/%zu bytes)", sent, n + m);
                /* In production, should loop to send remaining bytes */
            }
        }

        if (n == 0)
            printf("client disconnected\n");
        else
            perror("recv");

        close(connfd);
    }

    close(sockfd);
    return 0;
}