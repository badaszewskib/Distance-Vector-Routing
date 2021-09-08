#include <iostream>
#include <string>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <deque>
#include <sys/time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <net/if.h>
#include <ifaddrs.h>

#include "../include/control_response.h"
#include "../include/router.h"

/**
 * @bmbadasz_assignment3
 * @author  Benjamin Badaszewski <bmbadasz@buffalo.edu>
 * @version 1.0
 *
 * @section LICENSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details at
 * http://www.gnu.org/copyleft/gpl.html
 *
 * @section DESCRIPTION
 *
 * This contains the main function. Add further description here....
 */

using namespace std;

void incrementTimeout(vector<router> *routers, uint16_t id, uint16_t *updateInterval, deque<timer> *timers){
    for(int i=0; i<routers->size(); i++){
        if((*routers)[i].id == id){
            (*routers)[i].timeouts += 1;
            printf("R%d: %d timeouts\n", (*routers)[i].id, (*routers)[i].timeouts);
            if((*routers)[i].timeouts > 2){
                /*Router hasn't heard from neighbor in 3 intervals*/
                printf("R%d has died!\n", (*routers)[i].id);
                (*routers)[i].cost = INF;
                (*routers)[i].crashed = true;
                timers->pop_front();
                return;
            }
        }
    }
    addTimer(id, updateInterval, timers);
    timers->pop_front();
}

void reset_timer(vector<router> *routers, deque<timer> *timers, uint32_t in_ip, uint16_t *updateInterval){
    /*Get ID of router in question*/
    uint16_t id;
    for(int i=0; i<routers->size(); i++){
        if((*routers)[i].ip4 == in_ip){
            id = (*routers)[i].id;
            printf("Resetting R%d timeouts\n", id);
            (*routers)[i].timeouts = 0;
        }
    }

    /*Kill current timer and make a new one*/
    for(int i=0; i<timers->size(); i++){
        if((*timers)[i].id == id){
            deque<timer>::iterator it;
            it = timers->begin() + i;
            timers->erase(it);
        }
    }
    addTimer(id, updateInterval, timers);
}

/**
 * main function
 *
 * @param  argc Number of arguments
 * @param  argv The argument list
 * @return 0 EXIT_SUCCESS
 */
int main(int argc, char **argv)
{
    // Totally not global global variables
    printf("\nProgram start\n_______________________________________________________\n\n");
    vector<router> *routers = new vector<router>();
    std::vector<router> *connected = new vector<router>();
    deque<timer> *timers = new deque<timer>();
    uint16_t *selfRef = new uint16_t; // Index at which self resides in routers
    *selfRef = INF;
    bool udp_enabled = false;
    uint16_t *updateInterval = new uint16_t;
    *updateInterval = INF;
    struct timeval * tv = NULL;

	/*Check for proper number of arguments*/
	if(argc != 2){
		cout<<"USAGE: ./router <CONTROLLER PORT>\n";
		return 1;
	}
	/*Check for a valid controller portnum*/
	uint16_t ctrlPort = atoi(argv[1]);
	if(!(ctrlPort>=1024 && ctrlPort<=65535)){
		cout<<"<CONTROLLER PORT> must be value between 1024 and 65535\n";
		return 2;	
	}

	/*Set up the different variables needed*/
    struct sockaddr_in server, rPort;
    int listener, listenerUDP, newfd, fdmax, nbytes;
    int controller = -1;
    char buffer[1024];  
    fd_set read_fds; //temp fd list
    int yes = 1;
    socklen_t addrlen;

    addrlen = sizeof(server);

    /*Set up stuff for select function*/
    FD_ZERO(&read_fds);

    /*Set up the server socket*/
    listener = socket(PF_INET, SOCK_STREAM, 0);
    memset(&server, '\0', sizeof(server));
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_family = AF_INET;
    server.sin_port = htons(ctrlPort);
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
    bind(listener, (struct sockaddr*)&server, sizeof(server));
    listen(listener, 10);
    
    /*Get own address for UDP stuff*/
    char hostBuf[256];
    struct hostent *host_info;
    gethostname(hostBuf, sizeof(hostBuf));
    host_info = gethostbyname(hostBuf);
    struct in_addr *ownIP = (struct in_addr *)host_info->h_addr_list[0];

    /*Set up UDP socket*/
    listenerUDP = socket(PF_INET, SOCK_DGRAM, 0);
    setsockopt(listenerUDP, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
    memset(&rPort, '\0', sizeof(rPort));
    rPort.sin_addr.s_addr = htonl(INADDR_ANY);
    rPort.sin_family = AF_INET;

    while(true){
        // printf("Number of  timers: %d\n", timers->size());

        if(*selfRef != INF && !udp_enabled){
            rPort.sin_port = htons((*routers)[*selfRef].rPort);
            bind(listenerUDP, (struct sockaddr*)&rPort, sizeof(rPort));
            printf("Listening on UDP %d\n_______________________________________________________\n\n", (*routers)[*selfRef].rPort);
            udp_enabled = true;
        }
        
        FD_ZERO(&read_fds);
        FD_SET(listener, &read_fds);
        FD_SET(listenerUDP, &read_fds);
        fdmax = max(listener, listenerUDP) + 1;

        if(*updateInterval != INF){
            struct timeval now;
            gettimeofday(&now, NULL);
            tv = new struct timeval();
            if(!timers->empty()){
                if(timercmp(&now, &(timers->front().abs), <)){
                    /*Front timer hasn't gone off yet*/
                    timersub(&(timers->front().abs), &now, tv);
                    printf("Setting timer for R%d: %ul:%ul\n", timers->front().id, tv->tv_sec, tv->tv_usec);
                } else {
                    /*Front timer has gone off*/
                    printf("Outdated timers\n");
                    while(timercmp(&now, &(timers->front().abs), >)){
                        incrementTimeout(routers, timers->front().id, updateInterval, timers);
                        send_updates(listenerUDP, routers, (*routers)[*selfRef].rPort, ownIP, connected);
                    }
                    /*Timer is now after current time*/
                    timersub(&(timers->front().abs), &now, tv);
                    printf("Setting timer for R%d: %ul:%ul\n", timers->front().id, tv->tv_sec, tv->tv_usec);
                }
            } else {
                    printf("No timers\n");
                    tv = NULL;
            }
            printf("_______________________________________________________\n\n");
        }

        int selVal = select(fdmax+1, &read_fds, NULL, NULL, tv);
        if(selVal == -1){
            printf("Select error\n");
            return 3;
        } else if(selVal == 0){
            /*Handle timer interrupt*/
            printf("Timer interrupt\n");
            incrementTimeout(routers, timers->front().id, updateInterval, timers);
            send_updates(listenerUDP, routers, (*routers)[*selfRef].rPort, ownIP, connected);
        } else {
            /*Handle packets in sockets*/
            if(FD_ISSET(listener, &read_fds)){
                /*Handle any packets coming in on TCP socket*/
                controller = accept(listener, (struct sockaddr *)&server, &addrlen);
                nbytes = read(controller, buffer, sizeof(buffer));
                parse_control(controller, buffer, routers, updateInterval, selfRef, timers, connected);
            }
            if(FD_ISSET(listenerUDP, &read_fds)){
                /*Handle incoming UDP*/
                nbytes = recv(listenerUDP, buffer, sizeof(buffer), 0);
                uint32_t in_ip = *(uint32_t*)(&buffer[4]);
                parse_router(listenerUDP, buffer, routers, ownIP, (*routers)[*selfRef].rPort, selfRef, connected);
                reset_timer(routers, timers, in_ip, updateInterval);
            }
        }


    }
	return 0;
}
