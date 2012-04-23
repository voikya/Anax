#ifndef DISTRANAX_H
#define DISTRANAX_H

#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "globals.h"

struct thread_arguments {
    destination_t *dest;
    anaxjob_t *job;
};
typedef struct thread_arguments threadarg_t;

void *get_in_addr(struct sockaddr *sa);
int loadDestinationList(char *destfile, destinationlist_t **destinations);
int connectToRemoteHost(destination_t *dest);
int distributeJobs(destinationlist_t *destinationlist, joblist_t *joblist);
void *runRemoteJob(void *argt);

#endif
