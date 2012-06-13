#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <unistd.h>

#include "http.h"
#include "log.h"

// page size
#define BUFFER_SIZE 4096

struct pxmc_client_;

typedef struct pxmc_proxy_ {
    int px_fd;
    struct pxmc_client_ *px_clients;
    bool px_loop;
} pxmc_proxy_t;

typedef struct pxmc_client_ {
    struct pxmc_client_ *cl_next;
    int cl_fd;
    pxmc_proxy_t *cl_server;
    struct sockaddr cl_addr;
    pthread_t cl_thread;
    pthread_attr_t cl_thread_attrs;
} pxmc_client_t;

int setnonblock(int fd);
int bind_proxy(const char *host, const char *port, int *fd);
void pxmc_proxy_loop();
int pxmc_client_create(int fd);
void* pxmc_client_thread(void *cl);
void pxmc_proxy_finish();
int pxmc_client_parse_req(pxmc_client_t *c, const char *);
void pxmc_exit(int sig);

pxmc_proxy_t the_proxy;

int
main(int argc, char **argv)
{
    int ret;
    // set up signal handlers
    (void) signal(SIGINT, pxmc_exit);
    (void) signal(SIGQUIT, pxmc_exit);
    (void) signal(SIGKILL, pxmc_exit);

    memset(&the_proxy, 0, sizeof(the_proxy));
    pxmc_log(LOG_INFO, "Booting proxmac...");

    // TODO read binding host/port from config through guile

    ret = bind_proxy("127.0.0.1", "8008", &the_proxy.px_fd);
    if (0 != ret) {
        pxmc_log(LOG_CRIT, "Failed to bind port!");
        return ret;
    }

    pxmc_log(LOG_DEBUG, "Starting loop on %d", the_proxy.px_fd);
    pxmc_proxy_loop();

    pxmc_proxy_finish();

    return 0;
}

void
pxmc_exit(int sig)
{
    pxmc_log(LOG_INFO, "Exiting from %d...", sig);
    pxmc_proxy_finish();
    exit(sig);
}

int
bind_proxy(const char *hostname, const char *port, int *pfd)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int ret;
    int fd;
    int one;

    memset(&hints, 0, sizeof(struct addrinfo));
    if (hostname == NULL) {
      /*
       * XXX: This is a hack. The right thing to do
       * would be to bind to both AF_INET and AF_INET6.
       * This will also fail if there is no AF_INET
       * version available.
       */
        hints.ai_family = AF_INET;
    } else {
        hints.ai_family = AF_UNSPEC;
    }

    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0; /* Really any stream protocol or TCP only */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    if ((ret = getaddrinfo(hostname, port, &hints, &result))) {
        pxmc_log(LOG_DEBUG, "Can't resolve...");
        pxmc_log(LOG_ERR,
                  "Can not resolve %s: %s", hostname, gai_strerror(ret));
        return -2;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        pxmc_log(LOG_DEBUG, "Creating socket on %s:%s", hostname, port);
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            pxmc_log(LOG_DEBUG, "Error creating socket");
            continue;
        }

        /*
         * On UNIX, we assume the user is sensible enough not
         * to start Privoxy multiple times on the same IP.
         * Without this, stopping and restarting Privoxy
         * from a script fails.
         * Note: SO_REUSEADDR is meant to only take over
         * sockets which are *not* in listen state in Linux,
         * e.g. sockets in TIME_WAIT. YMMV.
         */
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one));

        if (bind(fd, rp->ai_addr, rp->ai_addrlen) < 0) {

            if (errno == EADDRINUSE) {
                pxmc_log(LOG_DEBUG, "Error binding socket");
                freeaddrinfo(result);
                close(fd);
                return(-3);

            } else {
                close(fd);
            }
        } else {

            /* bind() succeeded, escape from for-loop */
            /*
             * XXX: Support multiple listening sockets (e.g. localhost
             * resolves to AF_INET and AF_INET6, but only the first address
             * is used
             */
            pxmc_log(LOG_DEBUG, "Bind success");
            break;
        }
    }

    freeaddrinfo(result);
    if (rp == NULL) {
        /* All bind()s failed */
        pxmc_log(LOG_DEBUG, "All binds failed");
        return(-1);
    }

    while (listen(fd, 128) == -1) {
        if (errno != EINTR) {
            return(-1);
        }
        pxmc_log(LOG_DEBUG, "Tryin' to listen");
    }

    pxmc_log(LOG_DEBUG, "This is our fd: %d", fd);
    *pfd = fd;
    return 0;
}

void
pxmc_proxy_loop()
{
    int ret;
    fd_set fds;

    // begin our loop
    the_proxy.px_loop = true;

    while(the_proxy.px_loop) {
        FD_ZERO(&fds);
        FD_SET(the_proxy.px_fd, &fds);

        pxmc_log(LOG_DEBUG, "Trying to select...");
        ret = select(the_proxy.px_fd + 1, &fds, NULL, NULL, NULL);
        pxmc_log(LOG_DEBUG, "select ret = %d", ret);
        if (ret > 0) {
            pxmc_log(LOG_DEBUG, "Checking if server fd");
            if (FD_ISSET(the_proxy.px_fd, &fds)) {
                // yay connection
                ret = pxmc_client_create(the_proxy.px_fd);
                if (ret != 0) {
                    pxmc_log(LOG_DEBUG, "Oh no! %d", ret);
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        pxmc_log(LOG_ERR, "Accepting connection failed, %s", strerror(errno));
                        exit(errno);
                    }
                    break;
                }
            }
        }
    }
}

int
pxmc_client_create(int fd)
{
    pxmc_client_t *cl;
    int cfd;
    struct sockaddr addr;
    socklen_t addr_len;

    cfd = accept(fd, &addr, &addr_len);
    pxmc_log(LOG_DEBUG, "Connection accepted, %d", cfd);

    cl = malloc(sizeof(*cl));
    if (!cl) {
        pxmc_log(LOG_DEBUG, "No memory");
        return -1;
    }

    pxmc_log(LOG_DEBUG, "Making client thread");
    cl->cl_next = NULL;
    cl->cl_fd = cfd;
    cl->cl_server = &the_proxy;
    memcpy(&cl->cl_addr, &addr, sizeof(cl->cl_addr));
    setnonblock(cl->cl_fd);

    // append to server
    cl->cl_next = the_proxy.px_clients;
    the_proxy.px_clients = cl;

    pthread_attr_init(&cl->cl_thread_attrs);
    pthread_attr_setdetachstate(&cl->cl_thread_attrs, PTHREAD_CREATE_DETACHED);
    (void) pthread_create(&cl->cl_thread, &cl->cl_thread_attrs, pxmc_client_thread, (void*)cl);

    return 0;
}

void*
pxmc_client_thread(void *args)
{
    char buff[BUFFER_SIZE];
    pxmc_client_t *cl = (pxmc_client_t *)args;
    pxmc_http_req_t http_req;
    pxmc_http_resp_t http_resp;
    int ret;

    if (cl == NULL) {
        return NULL;
    }

    pxmc_log(LOG_DEBUG, "client thread");
    ret = read(cl->cl_fd, buff, sizeof(buff));
    if (ret > BUFFER_SIZE) {
        goto finish;
    }

    ret = pxmc_http_parse_request(buff, &http);

    ret = pxmc_http_send_request(&http, &resp);

    pxmc_log(LOG_DEBUG, "ret[%d] len[%d] buff[%s]", ret, (int)strlen(buff), buff);

finish:
    close(cl->cl_fd);
    pthread_exit(NULL);

    return NULL;
}

void
pxmc_proxy_finish()
{
    pxmc_client_t *cl;

    while(the_proxy.px_clients != NULL) {
        cl = the_proxy.px_clients;
        the_proxy.px_clients = the_proxy.px_clients->cl_next;
        pthread_join(cl->cl_thread, NULL);
        close(cl->cl_fd);
        free(cl);
    }

    close(the_proxy.px_fd);
}

int
setnonblock(int fd)
{
    int flags;
    flags = fcntl(fd, F_GETFL);
    flags |= O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
}
