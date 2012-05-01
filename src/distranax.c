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

int distributeJobs(destinationlist_t *destinationlist, joblist_t *joblist, colorscheme_t *colorscheme, double scale) {
    printf("Distributing...\n");
    for(int i = 0; i < destinationlist->num_destinations; i++) {
        if(destinationlist->destinations[i].status == ANAX_STATE_NOJOB) {
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
                argt->scale = scale;
                pthread_create(&(joblist->jobs[j].thread), NULL, runRemoteJob, argt);
                break;
            }
        }
    }
        
    return 0;
}

void *runRemoteJob(void *argt) {
    destination_t *destination = ((threadarg_t *)argt)->dest;
    anaxjob_t *job = ((threadarg_t *)argt)->job;
    colorscheme_t *colorscheme = ((threadarg_t *)argt)->colorscheme;
    double scale = ((threadarg_t *)argt)->scale;
    
    // Send data to remote machine for initial setup
    // 2 - packet size
    // 2 - str size
    // 2 - num colors
    // 1 - isAbs
    // 1 - 000
    // 8 - scale
    // * - name
    // * - colors (E: 2, R: 1, G: 1, B: 1, A: 8)
    int num_bytes = 16 + strlen(job->name) + (colorscheme->num_stops * 13);
    uint8_t *outbuf = calloc(num_bytes, sizeof(uint8_t));
    struct header *hdr = (struct header *)outbuf;
    hdr->packet_size = (uint16_t)num_bytes;
    hdr->str_size = (uint16_t)strlen(job->name);
    hdr->num_colors = (uint16_t)(colorscheme->num_stops);
    hdr->is_abs = (uint8_t)(colorscheme->isAbsolute);
    hdr->scale = scale;
    memcpy(outbuf + 16, job->name, strlen(job->name));
    for(int i = 1; i <= colorscheme->num_stops; i++) {
        struct compressed_color *comp_c = (struct compressed_color *)(outbuf + 16 + strlen(job->name) + ((i - 1) * 13));
        comp_c->elevation = (int16_t)(colorscheme->colors[i].elevation);
        comp_c->red = (uint8_t)(colorscheme->colors[i].color.r);
        comp_c->green = (uint8_t)(colorscheme->colors[i].color.g);
        comp_c->blue = (uint8_t)(colorscheme->colors[i].color.b);
        comp_c->alpha = (double)(colorscheme->colors[i].color.a);
    }
    
    int bytes_sent = 0;
    while(bytes_sent < num_bytes) {
        bytes_sent += send(destination->socketfd, outbuf + bytes_sent, num_bytes - bytes_sent, 0);
    }
    
    // Transmit the GeoTIFF, if necessary
    if(!strstr(job->name, "http://")) {
        // Get the file size
        FILE *fp = fopen(job->name, "r");
        fseek(fp, 0L, SEEK_END);
        num_bytes = ftell(fp);
        rewind(fp);
        
        // Send the file size
        bytes_sent = 0;
        while(bytes_sent < sizeof(uint32_t)) {
            bytes_sent += send(destination->socketfd, &num_bytes, 4, 0);
        }
        
        // Send the file
        bytes_sent = 0;
        uint8_t buf[8192];
        while(bytes_sent < num_bytes) {
            int bytes_sent_tmp = 0;
            int bytes_to_send = fread(buf, sizeof(uint8_t), 8192, fp);
            while(bytes_sent_tmp < bytes_to_send) {
                bytes_sent_tmp += send(destination->socketfd, buf, bytes_to_send - bytes_sent_tmp, 0);
            }
            bytes_sent += bytes_sent_tmp;
        }
    }
    
    // Receive progress updates from remote machine
    
    // Receive output file from remote machine
    char outfile[FILENAME_MAX];
    sprintf(outfile, "/tmp/map%i.png", job->index);
    FILE *fp = fopen(outfile, "w+");
    
    uint32_t filesize = 0;
    int bytes_rcvd = 0;
    while(bytes_rcvd < 4) {
        bytes_rcvd += recv(destination->socketfd, &filesize, 4 - bytes_rcvd, 0);
    }

    uint8_t buf[8192]; // 8 kilobytes
    bytes_rcvd = 0;
    while(bytes_rcvd < filesize) {
        int bytes_rcvd_tmp = recv(destination->socketfd, buf, 8192, 0);
        fwrite(buf, sizeof(uint8_t), bytes_rcvd_tmp, fp);
        bytes_rcvd += bytes_rcvd_tmp;
    }
    
    fclose(fp);

    // Update local variables and signal main thread
    pthread_mutex_lock(&ready_mutex);
    destination->status = ANAX_STATE_NOJOB;
    job->status = ANAX_STATE_COMPLETE;
    pthread_cond_signal(&ready_cond);
    pthread_mutex_unlock(&ready_mutex);
    
    //free(argt);
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

int getHeaderData(int outsocket, char **filename, colorscheme_t **colorscheme, double *scale) {
    int bytes_rcvd = 0;
    uint16_t packet_size;
    
    while(bytes_rcvd < sizeof(uint16_t)) {
        bytes_rcvd += recv(outsocket, &packet_size, sizeof(uint16_t), 0);
    }
    
    uint8_t buf[packet_size - 2];
    
    while(bytes_rcvd < packet_size - 2) {
        bytes_rcvd += recv(outsocket, &buf, packet_size - bytes_rcvd, 0);
    }
    uint16_t strlength = *(uint16_t *)buf;
    uint16_t numcolors = *(uint16_t *)(buf + 2);
    uint8_t isAbs = *(uint8_t *)(buf + 4);
    *scale = *(double *)(buf + 6);
    *filename = calloc(strlength + 1, sizeof(char));
    memcpy(*filename, buf + 14, strlength);
    
    printf("Received %i bytes\n", bytes_rcvd);
    printf("Packet size: %i\n", (int)packet_size);
    printf("String length: %i\n", (int)strlength);
    printf("Number of colors: %i\n", (int)numcolors);
    printf("Is Absolute: %i\n", (int)isAbs);
    printf("Scale: %f\n", *scale);
    printf("File: %s\n", *filename);
    
    *colorscheme = malloc(sizeof(colorscheme_t));
    (*colorscheme)->isAbsolute = (int)isAbs;
    (*colorscheme)->num_stops = (int)numcolors;
    (*colorscheme)->colors = calloc(numcolors + 2, sizeof(colorstop_t));
    
    int coloroffset = 14 + strlength;
    for(int i = 1; i <= numcolors; i++) {
        (*colorscheme)->colors[i].elevation = *(uint16_t *)(buf + coloroffset);
        (*colorscheme)->colors[i].color.r = *(uint8_t *)(buf + coloroffset + 2);
        (*colorscheme)->colors[i].color.g = *(uint8_t *)(buf + coloroffset + 3);
        (*colorscheme)->colors[i].color.b = *(uint8_t *)(buf + coloroffset + 4);
        (*colorscheme)->colors[i].color.a = *(double *)(buf + coloroffset + 5);
        coloroffset += 13;
    }
    memcpy(&((*colorscheme)->colors[0]), &((*colorscheme)->colors[1]), sizeof(colorstop_t));
    memcpy(&((*colorscheme)->colors[numcolors + 1]), &((*colorscheme)->colors[numcolors]), sizeof(colorstop_t));
    return 0;
}

int getImageFromPrimary(int outsocket, char *filename) {
    int bytes_rcvd = 0;

    char *filename_without_path = strrchr(filename, '/');
    filename_without_path = (filename_without_path == NULL) ? 0 : filename_without_path + 1;
    char outfile[strlen(filename_without_path) + 6];
    sprintf(outfile, "/tmp/%s", filename_without_path);
    FILE *fp = fopen(outfile, "w+");
    
    uint32_t filesize = 0;
    while(bytes_rcvd < 4) {
        bytes_rcvd += recv(outsocket, &filesize, 4 - bytes_rcvd, 0);
    }
    printf("File size: %i\n", (int)filesize);

    printf("Receiving image from primary node...");

    uint8_t buf[8192]; // 8 kilobytes
    bytes_rcvd = 0;
    while(bytes_rcvd < filesize) {
        int bytes_rcvd_tmp = recv(outsocket, buf, 8192, 0);
        fwrite(buf, sizeof(uint8_t), bytes_rcvd_tmp, fp);
        bytes_rcvd += bytes_rcvd_tmp;
    }
    
    printf("  Done.\n");
    
    fclose(fp);
    
    return 0;
}

int downloadImage(char *filename) {
    printf("Downloading image...");
    
    char *filename_without_path = strrchr(filename, '/');
    filename_without_path = (filename_without_path == NULL) ? 0 : filename_without_path + 1;
    char outfile[strlen(filename_without_path) + 6];
    sprintf(outfile, "/tmp/%s", filename_without_path);
    FILE *fp = fopen(outfile, "w+");
    
    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_URL, filename);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    
    printf("  Done.\n");
    
    fclose(fp);
    
    return 0;
}

int returnPNG(int outsocket, char *filename) {
    // Get the file size
    FILE *fp = fopen(filename, "r");
    fseek(fp, 0L, SEEK_END);
    int num_bytes = ftell(fp);
    rewind(fp);
    
    // Send the file size
    int bytes_sent = 0;
    while(bytes_sent < sizeof(uint32_t)) {
        bytes_sent += send(outsocket, &num_bytes, 4, 0);
    }
    
    // Send the file
    bytes_sent = 0;
    uint8_t buf[8192];
    while(bytes_sent < num_bytes) {
        int bytes_sent_tmp = 0;
        int bytes_to_send = fread(buf, sizeof(uint8_t), 8192, fp);
        while(bytes_sent_tmp < bytes_to_send) {
            bytes_sent_tmp += send(outsocket, buf, bytes_to_send - bytes_sent_tmp, 0);
        }
        bytes_sent += bytes_sent_tmp;
    }
    
    fclose(fp);
    
    return 0;
}

int countComplete(joblist_t *joblist) {
    int count = 0;
    for(int i = 0; i < joblist->num_jobs; i++) {
        if(joblist->jobs[i].status == ANAX_STATE_COMPLETE)
            count++;
    }
    
    return count;
}



