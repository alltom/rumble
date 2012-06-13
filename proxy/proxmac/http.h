#ifndef __PROXMAC_HTTP_H__
#define __PROXMAC_HTTP_H__

typedef struct pxmc_http_req_ {
    char *phr_cmd;
    char *phr_orig_cmd;
    char *phr_method;
    char *phr_url;
    char *phr_version;
    int phr_status;
    char *phr_host;
    int phr_port;
    char *phr_path;
    char *phr_hostport;
    int phr_ssl;
    char *phr_host_ip_addr_str;
} pxmc_http_req_t;

int pxmc_http_parse_request(const char *, pxmc_http_req_t *);
int pxmc_http_parse_url(const char *, pxmc_http_req_t *, int);

#endif // __PROXMAC_HTTP_H__
