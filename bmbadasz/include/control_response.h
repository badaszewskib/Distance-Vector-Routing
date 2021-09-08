#ifndef CONTROLRESPONSE
#define CONTROLRESPONSE

#include <vector>
#include <deque>
#include "../include/router.h"

void handleAuthor(char *buffer);
void handleInit(char *buffer, std::vector<router> *routers, uint16_t *updateInterval, uint16_t *selfRef, std::deque<timer> *timers, std::vector<router> *connected);
void handleRoutingTable(std::vector<router> *routers);
void handleUpdate(char *buffer, std::vector<router> *routers);
void handleCrash();
void handleSendfile(char *buffer, std::vector<router> *routers);
void handleSendfileStats();
void handleLastDataPacket();
void handlePenultimateDataPacket();
void parse_control(int socket, char *inBuffer, std::vector<router> *routers, uint16_t *updateInterval, uint16_t *selfRef, std::deque<timer> *timers, std::vector<router> *connected);

#endif