#ifndef ROUTER
#define ROUTER

#include <inttypes.h>
#include <vector>
#include <deque>
#include <sys/time.h>
#include <stdbool.h>

#define INF UINT16_MAX
#define BUFSIZE 1024

struct router{
    uint16_t id;
    uint16_t rPort;
    uint16_t dPort;
    uint16_t cost;
    uint32_t ip4;
    uint16_t nhop;
    uint16_t timeouts;
    bool crashed;
};

struct timer{
    uint16_t id;
    struct timeval abs;
};

void addTimer(uint16_t id, uint16_t *updateInterval, std::deque<timer> *timers);

void parse_router(int socket, char *buffer, std::vector<router> *routers, struct in_addr *ownIP, uint16_t rPort, uint16_t *selfRef, std::vector<router> *connected);

void send_updates(int socket, std::vector<router> *routers, uint16_t rPort, struct in_addr *ownIP, std::vector<router> *connected);

#endif