#ifndef _PROXMAC_LOG_
#define _PROXMAC_LOG_

#define xstr(s) str(s)
#define str(s) #s

#define pxmc_log(level, format, ...) printf(#level ":" __FILE__ ":" xstr(__LINE__) ": " format "\n", ##__VA_ARGS__)

#endif // _PROXMAC_LOG_
