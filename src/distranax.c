#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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











