#include <iostream>
#include <fstream>
#include <string>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include "../include/control_response.h"
#include "../include/router.h"

using namespace std;

struct sockaddr_in controller;
int controllerSFD;
struct sockaddr_in controllerAddr;

uint16_t prevChxSum = -1;
uint16_t chxSum = -1;

/** printControlMsg
 *  @param buffer control message to be printed
 *  @return Prints format: Destination IP > Control Code > Response > Payload Length > Payload */
void printControlMsg(char *buffer){
    // uint32_t IPval = (uint32_t)buffer[0];
    char IP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, buffer, IP, sizeof(IP));
    char ctrlCode = buffer[4];
    char resp = buffer[5];
    uint16_t pLength;
    memcpy((void*)&pLength, (void*)&buffer[6], sizeof(uint16_t));
    pLength = ntohs(pLength);
    printf("%s\t%d\t%d\t%d\n", IP, (int)ctrlCode, (int)resp, pLength);
    if(ctrlCode == 0){
        for(int i=0; i<pLength; i++){
            printf("%c", buffer[i+8]);
        }
    }
    if(ctrlCode == 1){

    }
}

/** checksum
 *  @param buffer Packet to calculate checksum for
 *  @return Checksum value
 */
uint16_t checksum(char *buffer){
    uint16_t retVal = 0;
    for(int i=0; i<sizeof(buffer); i++){
        retVal += (uint16_t)buffer[i];
    }
    return retVal;
}

/** parse_control
 *  @param socket file descriptor for socket being read from
 *  @param inBuffer packet which came in from the socket
 */
void parse_control(int socket, char *inBuffer, vector<router> *routers, uint16_t *updateInterval, uint16_t *selfRef, deque<timer> *timers, vector<router> *connected){
    controllerSFD = socket;
    socklen_t addrSize = sizeof(controllerAddr);
    getpeername(socket, (struct sockaddr *)&controllerAddr, &addrSize);
    prevChxSum = chxSum;
    chxSum = checksum(inBuffer);

    /*Print incoming buffer*/
    printf("Incoming control packet\n");
    // printControlMsg(inBuffer);

    if(chxSum == prevChxSum){
        printf("Same packet received twice. Dropping packet.\n");
        printf("_______________________________________________________\n\n");
        return;
    }
    printf("_______________________________________________________\n\n");
    
    char ctrlCode = inBuffer[4];
    switch(ctrlCode){
        case(0):
            handleAuthor(inBuffer);
            break;
        case(1):
            handleInit(inBuffer, routers, updateInterval, selfRef, timers, connected);
            break;
        case(2):
            handleRoutingTable(routers);
            break;
        case(3):
            handleUpdate(inBuffer, routers);
            break;
        case(4):
            handleCrash();
            break;
        case(5):
            handleSendfile(inBuffer, routers);
            break;
        case(6):
            handleSendfileStats();
            break;
        case(7):
            handleLastDataPacket();
            break;
        case(8):
            handlePenultimateDataPacket();
            break;
    }
}

/** handleAuthor - Sends academic integrity policy message to controller
 *  @param none 
 **/ 
void handleAuthor(char *buffer){
    char sendBuf[BUFSIZE];
    memset(sendBuf, '\0', sizeof(sendBuf));
    uint32_t outAddr = controllerAddr.sin_addr.s_addr;
    memcpy((void*)sendBuf, (void*)&outAddr, sizeof(uint32_t));
    sendBuf[4] = 0; //Control Code 0
    sendBuf[5] = 0; //Response Code 0
    char payload[] = "I, bmbadasz, have read and understood the course academic integrity policy.";
    uint16_t plSize = htons(sizeof(payload));
    memcpy((void*)&sendBuf[6], (void*)&plSize, sizeof(uint16_t));
    memcpy((void*)&sendBuf[8], (void*)payload, sizeof(payload));
    send(controllerSFD, sendBuf, sizeof(sendBuf), 0);
    // printf("Outgoing Packet\n");
    // printControlMsg(sendBuf);
}

/** handleInit - Populates routers vector from controller input
 *  @param buffer Init packet coming in from the controller
 **/
void handleInit(char *buffer, vector<router> *routers, uint16_t *updateInterval, uint16_t *selfRef, deque<timer> *timers, vector<router> *connected){
    uint16_t numRouters;
    numRouters = ntohs(*(uint16_t*)(&buffer[8]));
    *updateInterval = ntohs(*(uint16_t*)(&buffer[10]));
    /*Populate routers vector from info in this buffer*/
    for(int i=0; i<numRouters; i++){
        int rBase = 12 + (i * 12); // 12 bytes for header, 12 bytes per router
        struct router tmp;
        tmp.id = ntohs(*(uint16_t*)(&buffer[rBase]));
        tmp.rPort = ntohs(*(uint16_t*)(&buffer[rBase+2]));
        tmp.dPort = ntohs(*(uint16_t*)(&buffer[rBase+4]));
        tmp.cost = ntohs(*(uint16_t*)(&buffer[rBase+6]));
        tmp.ip4 = *(uint32_t*)(&buffer[rBase+8]);
        tmp.timeouts = 0;
        tmp.crashed = false;
        if(tmp.cost == INF){ // If routers aren't directly connected
            tmp.nhop = INF; // Set nhop as INF
        } else {
            tmp.nhop = tmp.id; // Set nhop as the router itself since directly connected
            if(tmp.cost != 0){ // Connected routers which are not self
                /*Add a timer for this router*/
                addTimer(tmp.id, updateInterval, timers);
                connected->push_back(tmp);
            }
        }
        routers->push_back(tmp);
        if(tmp.cost == 0){ // On self
            *selfRef = i;
        }
    }
    char sendBuf[BUFSIZE];
    memset(sendBuf, '\0', sizeof(sendBuf));
    uint32_t outAddr = controllerAddr.sin_addr.s_addr;
    memcpy((void*)sendBuf, (void*)&outAddr, sizeof(uint32_t));
    sendBuf[4] = 1; //Control Code 1
    sendBuf[5] = 0; //Response Code 0
    send(controllerSFD, sendBuf, sizeof(sendBuf), 0);
    // printf("Outgoing Packet\n");
    // printControlMsg(sendBuf);
}

/** handleRoutingTable
 *  @param None
 *  @return Sends current routing table
 */ 
void handleRoutingTable(vector<router> *routers){
    char sendBuf[BUFSIZE];
    memset(sendBuf, '\0', sizeof(sendBuf));
    uint32_t outAddr = controllerAddr.sin_addr.s_addr;
    memcpy((void*)sendBuf, (void*)&outAddr, sizeof(uint32_t));
    sendBuf[4] = 2; //Control Code 2
    sendBuf[5] = 0; //Response Code 0
    uint16_t plSize = htons(routers->size()*8);
    memcpy((void*)&sendBuf[6], (void*)&plSize, sizeof(uint16_t));
    uint16_t padding = 0;
    for(int i=0; i<routers->size(); i++){
        int rBase = 8 + (i * 8);
        uint16_t tmpID = htons((*routers)[i].id);
        uint16_t tmpNhop = htons((*routers)[i].nhop);
        uint16_t tmpCost = htons((*routers)[i].cost);
        memcpy((void*)&sendBuf[rBase], (void*)&tmpID, sizeof(uint16_t));
        memcpy((void*)&sendBuf[rBase+2], (void*)&padding, sizeof(uint16_t));
        memcpy((void*)&sendBuf[rBase+4], (void*)&tmpNhop, sizeof(uint16_t));
        memcpy((void*)&sendBuf[rBase+6], (void*)&tmpCost, sizeof(uint16_t));
        // printf("Router %d:\t%d\t%d\n", ntohs(tmpID), ntohs(tmpNhop), ntohs(tmpCost));
    }
    send(controllerSFD, sendBuf, sizeof(sendBuf), 0);
}

/** handleUpdate
 *  
 */ 
void handleUpdate(char *buffer, std::vector<router> *routers){
    uint16_t id = ntohs(*(uint16_t*)(&buffer[8]));
    uint16_t cost = ntohs(*(uint16_t*)(&buffer[10]));
    for(int i=0; i<routers->size(); i++){
        if((*routers)[i].id == id){
            (*routers)[i].cost = cost;
            break;
        }
    }
    char sendBuf[BUFSIZE];
    memset(sendBuf, '\0', sizeof(sendBuf));
    uint32_t outAddr = controllerAddr.sin_addr.s_addr;
    memcpy((void*)sendBuf, (void*)&outAddr, sizeof(uint32_t));
    sendBuf[4] = 3; //Control Code 3
    sendBuf[5] = 0; //Response Code 0
    send(controllerSFD, sendBuf, sizeof(sendBuf), 0);
    printf("Updating R%d to cost %d\n", id, cost);
}

void handleCrash(){
    char sendBuf[BUFSIZE];
    memset(sendBuf, '\0', sizeof(sendBuf));
    uint32_t outAddr = controllerAddr.sin_addr.s_addr;
    memcpy((void*)sendBuf, (void*)&outAddr, sizeof(uint32_t));
    sendBuf[4] = 4; //Control Code 4
    sendBuf[5] = 0; //Response Code 0
    send(controllerSFD, sendBuf, sizeof(sendBuf), 0);
    printf("Crashing\n");
    exit(100);
}

void handleSendfile(char *buffer, std::vector<router> *routers){
    // uint16_t plSize = ntohs(*(uint16_t *)(&buffer[6]));
    // uint16_t fileNameSize = plSize - 8; // filename size in bytes
    // // printf("filename size: %d\n", fileNameSize);
    // uint32_t dest_ip4 = *(uint32_t *)(&buffer[8]);
    // uint8_t init_ttl = *(uint8_t *)(&buffer[12]);
    // uint8_t trans_id = *(uint8_t *)(&buffer[13]);
    // uint16_t init_seqnum = ntohs(*(uint16_t *)(&buffer[14]));
    // char filename[256];
    // memcpy(filename, (void*)(&buffer[16]), fileNameSize);
    // filename[fileNameSize] = '\0';

    // /*Print incoming message*/
    // char IP[INET_ADDRSTRLEN];
    // struct in_addr tmpinaddr;
    // tmpinaddr.s_addr = dest_ip4;
    // inet_ntop(AF_INET, &tmpinaddr, IP, sizeof(IP));
    // printf("Sending to %s, init_ttl: %d, trans_id: %d, init_seqnum: %d:\t %s\n", IP, (int)init_ttl, (int) trans_id, init_seqnum, filename);

    // FILE *fp = fopen(filename, "r");
    // if(fp == NULL){
    //     printf("Could not open file!\n");
    //     return;
    // }
    // int fd = fileno(fp);
    // struct stat buf;
    // fstat(fd, &buf);
    // fclose(fp);
    // printf("File size: %ul", buf.st_size);
    // return;
}

void handleSendfileStats(){
    
}

void handleLastDataPacket(){

}

void handlePenultimateDataPacket(){

}