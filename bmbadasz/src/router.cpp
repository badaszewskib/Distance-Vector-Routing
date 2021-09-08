#include <iostream>
#include <string>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <deque>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include "../include/router.h"

using namespace std;


/** addTimer
 *  @param id ID of router timer is for
 *  @param updateInterval Interval between expected updates
 *  @param timers Pointer to list of timers
 */
void addTimer(uint16_t id, uint16_t *updateInterval, deque<timer> *timers){
    struct timer tba;
    struct timeval abs, tmp;
    tmp.tv_sec = (uint32_t)(*updateInterval);
    tmp.tv_usec = 0;
    gettimeofday(&abs, NULL); // Get initial time
    timeradd(&abs, &tmp, &abs); // Calculate absolute timeout, init + updateInterval
    tba.abs = abs;
    tba.id = id;
    timers->push_back(tba);
}

/** printDV_buffer
 *  @param buf Buffer to be printed
 *  @return Prints formatted buffer
*/
void printDV_buffer(char *buffer){
    char printIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, (void *)&buffer[4], printIP, sizeof(printIP));
    uint16_t numFields = ntohs(*(uint16_t *)(&buffer[0]));
    uint16_t srcPort = ntohs(*(uint16_t *)(&buffer[2]));
    printf("-------------------------------------------------------\n|                      DV BUFFER                      |\n| SRC_IP: %12s\t | FIELDS: %3d\t| PORT: %5d |\n-------------------------------------------------------\n", printIP, numFields, srcPort);
    for(int i=0; i<numFields; i++){
        int rBase = 8 + (i * 12);
        struct in_addr tmpinaddr;
        tmpinaddr.s_addr = htonl(*(uint32_t *)(&buffer[rBase]));
        inet_ntop(AF_INET, (void *)&tmpinaddr, printIP, sizeof(printIP));
        uint16_t pn = ntohs(*(uint16_t *)(&buffer[rBase+4]));
        uint16_t id = ntohs(*(uint16_t *)(&buffer[rBase+8]));
        uint16_t cst = ntohs(*(uint16_t *)(&buffer[rBase+10]));
        uint16_t crashed = *(uint16_t *)(&buffer[rBase+6]);
        printf("| R%d | IP: %12s | PORT: %5d\t| COST: %5d |", id, printIP, pn, cst);
        if(crashed == 1){
            printf("!");
        }
        printf("\n");
    }
    printf("-------------------------------------------------------\n");
}

/** parse_router
 *  @param socket Socket which recieved packet
 *  @param buffer Incoming packet
 *  @param routers Pointer to list of routers
 *  @param ownIP Struct holding own IP info
 *  @param rPort Own router port
 */
void parse_router(int socket, char *buffer, vector<router> *routers, struct in_addr *ownIP, uint16_t rPort, uint16_t *selfRef, vector<router> *connected){
    printf("Recieved UDP packet\n");
    printDV_buffer(buffer);
    
    /*Get ID of router we just recieved from*/
    bool updates, newCost = false;
    uint32_t rem_ip4 = htonl(*(uint32_t *)(&buffer[4]));
    uint16_t rem_idx;
    uint16_t rem_id;
    for(int b=0; b<routers->size(); b++){
        struct router curr = (*routers)[b];
        if(htonl(curr.ip4) == rem_ip4){
            rem_idx = b;
            rem_id = curr.id;
            printf("Recieved from R%d\n", rem_id);
            break;
        }
    }
    uint16_t rem_cost; // Cost of the neighbor sending
    uint16_t numFields = ntohs(*(uint16_t *)(&buffer[0]));
    for(int a=0; a<numFields; a++){
        int rBase = 8 + (a * 12);
        uint32_t in_id = ntohs(*(uint16_t *)(&buffer[rBase+8]));
        if(in_id == (*routers)[*selfRef].id){
            /*Finding info about link between self and neighbor*/
            rem_cost = ntohs(*(uint16_t *)(&buffer[rBase+10]));
            uint16_t crashed = *(uint16_t *)(&buffer[rBase+6]);
            printf("Current cost between R%d and R%d: %d\n", rem_id, in_id, (*routers)[rem_idx].cost);
            printf("Incoming cost between R%d and R%d: %d\n", rem_id, in_id, rem_cost);
            if((rem_cost != (*routers)[rem_idx].cost) && ((*routers)[rem_idx].nhop == rem_id) && (crashed != 1)){
                /*Update our records to reflect this*/
                printf("Updating cost between R%d and R%d to %d\n", rem_id, in_id, rem_cost);
                (*routers)[rem_idx].cost = rem_cost;
                updates = true;
                newCost = true;
            }
        }
    }

    /*If there was an update to cost, reset nodes with neighbor in path*/
    if(newCost){
        for(int c=0; c<routers->size(); c++){
            struct router curr = (*routers)[c];
            if((curr.id != rem_id) && (curr.nhop == rem_id)){
                /*Not neighbor but has neighbor in path*/
                printf("Resetting cost for R%d\n", (*routers)[c].id);
                (*routers)[c].cost = INF;
                (*routers)[c].nhop = INF;
                updates = true;
            }
            if((curr.crashed) && (curr.id == rem_id)){
                /*If we recieved a packet from neighbor then it isn't crashed*/
                printf("Uncrashing R%d\n", rem_id);
                (*routers)[c].crashed = false;
                (*routers)[c].cost = rem_cost;
                updates = true;
            }
        }
    }



    /*Parse incoming data and update fields as needed*/
    for(int i=0; i<numFields; i++){ // For every item in packet
        int rBase = 8 + (i * 12);
        uint16_t in_id = ntohs(*(uint16_t *)(&buffer[rBase+8]));
        uint16_t in_cost = ntohs(*(uint16_t *)(&buffer[rBase+10])); // Cost being told to you
        uint16_t crashed = *(uint16_t *)(&buffer[rBase+6]);
        uint16_t potential_cost;
        uint32_t gti = (uint32_t)(in_cost + rem_cost);
        uint32_t ttbi = (uint32_t)(INF);
        if(gti >= ttbi){ // Potential cost would be more than INF
            potential_cost = INF;
        } else { // Else calculate potential cost normally
            potential_cost = rem_cost + in_cost;
        }
        for(int j=0; j<routers->size(); j++){ // For every router in routers
            struct router curr = (*routers)[j];
            if((in_id == curr.id) && (!curr.crashed)){
                if(potential_cost < curr.cost){
                    // Wait until sees link cost change to send DV
                    printf("Changing R%d cost from %d to %d\n", curr.id, (*routers)[j].cost, potential_cost);
                    (*routers)[j].cost = potential_cost;
                    (*routers)[j].nhop = rem_id;
                    updates = true;
                }
                if((crashed == 1) && (j != *selfRef)){
                    (*routers)[j].crashed = true;
                    (*routers)[j].cost = INF;
                    updates = true;
                }
            }
        }
    } // End for
    if(updates){
        printf("Updates recieved. Notifying neighbors\n");
        /*Tittes*/
        send_updates(socket, routers, rPort, ownIP, connected);
    }
    printf("_______________________________________________________\n\n");
}

/** send_updates
 *  @param socket UDP Socket to send from
 *  @param routers Pointer to list of routers
 *  @param rPort Router port of this router
 *  @param ownIP Struct containing info about this router
 */ 
void send_updates(int socket, vector<router> *routers, uint16_t rPort, struct in_addr *ownIP, vector<router> *connected){
    printf("Sending DV updates\n");
    uint16_t crPort = htons(rPort);
    uint16_t numFields = htons(routers->size());
    
    char sendBuf[BUFSIZE];
    memset(sendBuf, '\0', sizeof(sendBuf));
    memcpy((void*)sendBuf, (void*)&numFields, sizeof(uint16_t));
    memcpy((void*)&sendBuf[2], (void*)&crPort, sizeof(uint16_t));
    memcpy((void*)&sendBuf[4], (void*)&(ownIP->s_addr), sizeof(uint32_t));

    uint16_t padding = 0;
    uint16_t crashed = 1;

    /*Build sendbuf*/
    for(int i=0; i<routers->size(); i++){
        struct router curr = (*routers)[i];
        uint32_t tmpip4 = htonl(curr.ip4);
        uint16_t tmprPort = htons(curr.rPort);
        uint16_t tmpid = htons(curr.id);
        uint16_t tmpcost = htons(curr.cost);
        int rBase = 8 + (i * 12);
        memcpy((void*)&sendBuf[rBase], (void*)&(tmpip4), sizeof(uint32_t));
        memcpy((void*)&sendBuf[rBase+4], (void*)&(tmprPort), sizeof(uint16_t));
        if(curr.crashed){
            memcpy((void*)&sendBuf[rBase+6], (void*)&(crashed), sizeof(uint16_t));
        } else {
            memcpy((void*)&sendBuf[rBase+6], (void*)&(padding), sizeof(uint16_t));
        }
        memcpy((void*)&sendBuf[rBase+8], (void*)&(tmpid), sizeof(uint16_t));
        memcpy((void*)&sendBuf[rBase+10], (void*)&(tmpcost), sizeof(uint16_t));
    }

    printDV_buffer(sendBuf);

    /*Send to neighbors*/
    for(int i=0; i<connected->size(); i++){
        struct router curr = (*connected)[i];
        struct sockaddr_in destAddr;
        memset(&destAddr, '\0', sizeof(destAddr));
        destAddr.sin_addr.s_addr = curr.ip4;
        destAddr.sin_port = htons(curr.rPort);
        destAddr.sin_family = AF_INET;
        char tmpBuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &destAddr.sin_addr, tmpBuf, sizeof(tmpBuf));
        printf("Sending to R%d: %12s:%d\n", curr.id, tmpBuf, ntohs(destAddr.sin_port));
        sendto(socket, (void*)sendBuf, sizeof(sendBuf), 0, (struct sockaddr *)&destAddr, sizeof(destAddr));
    }
    printf("_______________________________________________________\n\n");
}
