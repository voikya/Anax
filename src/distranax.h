#ifndef DISTRANAX_H
#define DISTRANAX_H

#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

struct destination {
	char addr[128];
	int socketfd;
};
typedef struct destination destination_t;

struct destination_file {
	int num_destinations;
	destination_t *destinations;
};
typedef struct destination_file destinationlist_t;

void *get_in_addr(struct sockaddr *sa);
int loadDestinationList(char *destfile, destinationlist_t **destinations);
int connectToRemoteHost(destination_t *dest);

#endif
