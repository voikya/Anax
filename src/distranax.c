#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include "distranax.h"
#include "globals.h"

/* DEBUGGING FUNCTIONS */

void SHOW_DESTINATION_LIST(destinationlist_t *destinationlist) {
	for(int i = 0; i < destinationlist->num_destinations; i++) {
		printf("#%i: %s\n", i, destinationlist->destinations[i].addr);
	}
}

/* END DEBUGGING FUNCTIONS */


void *get_in_addr(struct sockaddr *sa) {
	if(sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in *) sa)->sin_addr);
	}

	return &(((struct sockaddr_in6 *) sa)->sin6_addr);
}

int loadDestinationList(char *destfile, destinationlist_t **destinationlist) {
	FILE *fp = fopen(destfile, "r");
	if(!fp)
		return ANAX_ERR_FILE_DOES_NOT_EXIST;

	*destinationlist = malloc(sizeof(destinationlist_t));
	(*destinationlist)->destinations = NULL;

	char buf[BUFSIZE];
	int c = 0;
	while(fgets(buf, BUFSIZE, fp)) {
		if(buf[0] == '#' || buf[0] == '\n' || buf[0] == ' ')
			continue;
		(*destinationlist)->destinations = realloc((*destinationlist)->destinations, ++c * sizeof(destination_t));
		strncpy((*destinationlist)->destinations[c-1].addr, buf, strlen(buf));

		if((*destinationlist)->destinations[c-1].addr[strlen(buf) - 1] == '\n') {
			(*destinationlist)->destinations[c-1].addr[strlen(buf) - 1] = 0;
		}
	}

	(*destinationlist)->num_destinations = c;

	fclose(fp);

	return 0;
}

int connectToRemoteHost(destination_t *dest) {
	int socketfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if((rv = getaddrinfo(dest->addr, REMOTE_PORT, &hints, &servinfo)) != 0) {
		return ANAX_ERR_COULD_NOT_RESOLVE_ADDR;
	}

	for(p = servinfo; p != NULL; p = p->ai_next) {
		if((socketfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
			continue;

		if(connect(socketfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(socketfd);
			continue;
		}

		break;
	}

	if(!p) {
		return ANAX_ERR_COULD_NOT_CONNECT;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof(s));

	freeaddrinfo(servinfo);

	dest->socketfd = socketfd;

	return 0;
}

int distributeJobs(destinationlist_t *destinationlist, joblist_t *joblist, colorscheme_t *colorscheme) {
    for(int i = 0; i < destinationlist->num_destinations; i++) {
        for(int j = 0; j < joblist->num_jobs; j++) {
            if(joblist->jobs[j].status != ANAX_STATE_PENDING)
                continue;
            joblist->jobs[j].status = ANAX_STATE_INPROGRESS;
            //connectToRemoteHost(&(destinationlist->destinations[i]));
            destinationlist->destinations[i].status = ANAX_STATE_INPROGRESS;
            
            threadarg_t *argt = malloc(sizeof(threadarg_t));
            argt->dest = &(destinationlist->destinations[i]);
            argt->job = &(joblist->jobs[j]);
            argt->colorscheme = colorscheme;
            pthread_create(&(joblist->jobs[j].thread), NULL, runRemoteJob, argt);
        }
    }
        
    return 0;
}

void *runRemoteJob(void *argt) {
    destination_t *destination = ((threadarg_t *)argt)->dest;
    anaxjob_t *job = ((threadarg_t *)argt)->job;
    colorscheme_t *colorscheme = ((threadarg_t *)argt)->colorscheme;
    
    // Send data to remote machine for initial setup
    // 2 - packet size
    // 2 - str size
    // 2 - num colors
    // 1 - isAbs
    // 1 - 000
    // * - name
    // * - colors (E: 2, R: 1, G: 1, B: 1, A: 8)
    int num_bytes = 8 + strlen(job->name) + (colorscheme->num_stops * 13);
    uint8_t *outbuf = calloc(num_bytes, sizeof(uint8_t));
    struct header *hdr = (struct header *)outbuf;
    hdr->packet_size = (uint16_t)num_bytes;
    hdr->str_size = (uint16_t)strlen(job->name);
    hdr->num_colors = (uint16_t)(colorscheme->num_stops);
    hdr->is_abs = (uint8_t)(colorscheme->isAbsolute);
    memcpy(outbuf + 8, job->name, strlen(job->name));
    for(int i = 1; i <= colorscheme->num_stops; i++) {
        struct compressed_color *comp_c = (struct compressed_color *)(outbuf + 8 + strlen(job->name) + ((i - 1) * 13));
        comp_c->elevation = (int16_t)(colorscheme->colors[i].elevation);
        comp_c->red = (uint8_t)(colorscheme->colors[i].color.r);
        comp_c->green = (uint8_t)(colorscheme->colors[i].color.g);
        comp_c->blue = (uint8_t)(colorscheme->colors[i].color.b);
        comp_c->alpha = (double)(colorscheme->colors[i].color.a);
    }
    
    int bytes_sent = 0;
    while(bytes_sent < num_bytes) {
        bytes_sent += send(destination->socketfd, outbuf + bytes_sent, num_bytes, 0);
    }
    
    // Transmit the GeoTIFF, if necessary
    
    // Receive progress updates from remote machine
    
    // Receive output file from remote machine
    
    // Update local variables
    
    free(argt);
    return NULL;
}

int initRemoteListener(int *socketfd) {
    // Get socket
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    
    getaddrinfo(NULL, REMOTE_PORT, &hints, &res);
    
    for(struct addrinfo *a = res; a; a = a->ai_next) {
        *socketfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if(*socketfd < 0)
            continue;
        if(bind(*socketfd, res->ai_addr, res->ai_addrlen) < 0) {
            close(*socketfd);
            return ANAX_ERR_COULD_NOT_CONNECT;
        }
        break;
    }
    
    freeaddrinfo(res);
    
    // Listen
    listen(*socketfd, 5);

    return 0;
}

int getHeaderData(int outsocket, char **filename, colorscheme_t **colorscheme) {
    int bytes_rcvd = 0;
    uint16_t packet_size;

        printf("Rcvd: %i, Packetsize: %i\n", bytes_rcvd, (int)packet_size);
    
    while(bytes_rcvd < sizeof(uint16_t)) {
        bytes_rcvd += recv(outsocket, &packet_size, sizeof(uint16_t), 0);
    }
        printf("Rcvd: %i, Packetsize: %i\n", bytes_rcvd, (int)packet_size);
    
    uint8_t buf[packet_size - 2];
    
    while(bytes_rcvd < packet_size - 2) {
        bytes_rcvd += recv(outsocket, &buf, packet_size - bytes_rcvd, 0);
    }
    
    printf("Got header\n");

    return 0;
}







