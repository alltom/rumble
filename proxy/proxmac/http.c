#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"
#include "log.h"
#include "util.h"

int
pxmc_http_parse_request(const char *req, pxmc_http_req_t *http)
{
    char *buf;
    char *tokctx;
    char *v[10]; /* XXX: Why 10? We should only need three. */
    int i;
    int err;
    char delim[] = " \r\n";


    memset(http, '\0', sizeof(*http));

    buf = strdup(req);
    if (buf == NULL) {
        return -1;
    }
    pxmc_log(LOG_DEBUG, "buf[%p], req[%p]", buf, req);

    i = 0;
    v[0] = strtok_r(buf, delim, &tokctx);
    while (v[i] != NULL && i != 3) {
        v[++i] = strtok_r(NULL, delim, &tokctx);
        pxmc_log(LOG_DEBUG, "tok'd v[%d] = %s", i, v[i]);
    }

    if (i < 2) {
        pxmc_log(LOG_ERROR, "Could not parse headers, bailing!");
        return -47;
    }

    /*
     * Fail in case of unknown methods
     * which we might not handle correctly.
     *
     * XXX: There should be a config option
     * to forward requests with unknown methods
     * anyway. Most of them don't need special
     * steps.
     */
    // fuck checking methods
    // reads illogically, but is correct
    // i.e.: (!(strcmpic() == 0)) == strcmpic()
    if (strcmpic(v[2], "HTTP/1.1") && strcmpic(v[2], "HTTP/1.0")) {
        pxmc_log(LOG_ERROR, "%s, %s, %s, %s", v[0], v[1], v[2], v[3]);
        pxmc_log(LOG_ERROR, "The only supported HTTP "
                 "versions are 1.0 and 1.1. This rules out: %s", v[2]);
        return -2;
    }

    http->phr_ssl = !strcmpic(v[0], "CONNECT");

    err = pxmc_http_parse_url(v[1], http, !http->phr_ssl);
    if (err) {
        return err;
    }

    /*
     * Copy the details into the structure
     */
    http->phr_cmd = strdup(req);
    http->phr_method = strdup(v[0]);
    http->phr_version = strdup(v[2]);

    if ((http->phr_cmd == NULL)
        || (http->phr_method == NULL)
        || (http->phr_version == NULL) ) {
        return -4;
    }

    return 0;
}

int
pxmc_http_parse_url(const char *url, pxmc_http_req_t *http, int require_protocol)
{
    int host_available = 1; /* A proxy can dream. */

    /*
     * Save our initial URL
     */
    http->phr_url = strdup(url);
    if (http->phr_url == NULL) {
        return -5;
    }


    /*
     * Check for * URI. If found, we're done.
     */
    if (*http->phr_url == '*') {
        if (NULL == (http->phr_path = strdup("*"))
              || NULL == (http->phr_hostport = strdup("")) ) {
            // no mem
            return -6;
        }
        if (http->phr_url[1] != '\0') {
            return -7;
        }
        return -8;
    }


    /*
     * Split URL into protocol,hostport,path.
     */
    {
        char *buf;
        char *url_noproto;
        char *url_path;

        buf = strdup(url);
        if (buf == NULL) {
            //jb_err_mem
            return -9;
        }

        /* Find the start of the URL in our scratch space */
        url_noproto = buf;
        if (strncmpic(url_noproto, "http://",  7) == 0) {
            url_noproto += 7;
        } else if (strncmpic(url_noproto, "https://", 8) == 0) {
            /*
             * Should only happen when called from cgi_show_url_info().
             */
            url_noproto += 8;
            http->phr_ssl = 1;
        } else if (*url_noproto == '/') {
            /*
             * Short request line without protocol and host.
             * Most likely because the client's request
             * was intercepted and redirected into Privoxy.
             */
            http->phr_host = NULL;
            host_available = 0;
        } else if (require_protocol) {
            //return JB_ERR_PARSE;
            return -10;
        }

        url_path = strchr(url_noproto, '/');
        if (url_path != NULL) {
            /*
             * Got a path.
             *
             * NOTE: The following line ignores the path for HTTPS URLS.
             * This means that you get consistent behaviour if you type a
             * https URL in and it's parsed by the function.  (When the
             * URL is actually retrieved, SSL hides the path part).
             */
            http->phr_path = strdup(http->phr_ssl ? "/" : url_path);
            *url_path = '\0';
            http->phr_hostport = strdup(url_noproto);
        } else {
            /*
             * Repair broken HTTP requests that don't contain a path,
             * or CONNECT requests
             */
            http->phr_path = strdup("/");
            http->phr_hostport = strdup(url_noproto);
        }


        if ((http->phr_path == NULL)
             || (http->phr_hostport == NULL)) {
            //return JB_ERR_MEMORY;
            return -11;
        }
    }

    if (!host_available) {
        /* Without host, there is nothing left to do here */
        return 0;
    }

    /*
     * Split hostport into user/password (ignored), host, port.
     */
    {
        char *buf;
        char *host;
        char *port;

        buf = strdup(http->phr_hostport);
        if (buf == NULL) {
            //return JB_ERR_MEMORY;
            return -12;
        }

        /* check if url contains username and/or password */
        host = strchr(buf, '@');
        if (host != NULL) {
            /* Contains username/password, skip it and the @ sign. */
            host++;
        } else {
            /* No username or password. */
            host = buf;
        }

        /* Move after hostname before port number */
        if (*host == '[') {
            /* Numeric IPv6 address delimited by brackets */
            host++;
            port = strchr(host, ']');

            if (port == NULL) {
                /* Missing closing bracket */
                //return JB_ERR_PARSE;
                return -13;
            }

            *port++ = '\0';

            if (*port == '\0') {
                port = NULL;
            } else if (*port != ':') {
                /* Garbage after closing bracket */
                //return JB_ERR_PARSE;
                return -14;
            }
        } else {
            /* Plain non-escaped hostname */
            port = strchr(host, ':');
        }

        /* check if url contains port */
        if (port != NULL) {
            /* Contains port */
            /* Terminate hostname and point to start of port string */
            *port++ = '\0';
            http->phr_port = atoi(port);
        } else {
            /* No port specified. */
            http->phr_port = (http->phr_ssl ? 443 : 80);
        }

        http->phr_host = strdup(host);

        if (http->phr_host == NULL) {
            //return JB_ERR_MEMORY;
            return -15;
        }
    }

    return 0;
}
