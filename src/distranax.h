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
#include "libanax.h"

struct thread_arguments {
    destination_t *dest;
    anaxjob_t *job;
    colorscheme_t *colorscheme;
};
typedef struct thread_arguments threadarg_t;

struct header {
    uint16_t packet_size;
    uint16_t str_size;
    uint16_t num_colors;
    uint8_t is_abs;
};

struct compressed_color {
    int16_t elevation;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    double alpha;
};

void *get_in_addr(struct sockaddr *sa);
int loadDestinationList(char *destfile, destinationlist_t **destinations);
int connectToRemoteHost(destination_t *dest);
int distributeJobs(destinationlist_t *destinationlist, joblist_t *joblist, colorscheme_t *colorscheme);
void *runRemoteJob(void *argt);

#endif
