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
		
		(*destinationlist)->destinations[c-1].socketfd = -1;
		(*destinationlist)->destinations[c-1].status = ANAX_STATE_NOJOB;
		(*destinationlist)->destinations[c-1].num_jobs = 0;
		(*destinationlist)->destinations[c-1].jobs = NULL;
		pthread_mutex_init(&((*destinationlist)->destinations[c-1].ready_mutex), NULL);
		pthread_cond_init(&((*destinationlist)->destinations[c-1].ready_cond), NULL);
		(*destinationlist)->destinations[c-1].ready = 0;
		(*destinationlist)->destinations[c-1].complete = 0;
	}

	(*destinationlist)->num_destinations = c;

	fclose(fp);

	return 0;
}

int connectToRemoteHost(destination_t *dest, char *port) {
	int socketfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if((rv = getaddrinfo(dest->addr, port, &hints, &servinfo)) != 0) {
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

int initRemoteHosts(destinationlist_t *destinationlist, colorscheme_t *colorscheme, double scale) {
    printf("Packing initialization header... ");
    
    // Allocate and pack an initialization header
    int packetsize = sizeof(init_hdr_t) + (sizeof(compressed_color_t) * colorscheme->num_stops);
    uint8_t *packet = calloc(packetsize, sizeof(uint8_t));
    init_hdr_t *hdr = (init_hdr_t *)packet;
    hdr->packet_size = (uint32_t)packetsize;
    hdr->type = HDR_INITIALIZATION;
    hdr->is_abs = (uint8_t)(colorscheme->isAbsolute);
    hdr->num_colors = (uint8_t)(colorscheme->num_stops);
    hdr->scale = scale;
    
    // Pack the colorscheme after the header
    for(int i = 1; i <= colorscheme->num_stops; i++) {
        compressed_color_t *comp_c = (compressed_color_t *)(packet + sizeof(init_hdr_t) + ((i - 1) * sizeof(compressed_color_t)));
        comp_c->elevation = (int32_t)(colorscheme->colors[i].elevation);
        comp_c->red = (uint8_t)(colorscheme->colors[i].color.r);
        comp_c->green = (uint8_t)(colorscheme->colors[i].color.g);
        comp_c->blue = (uint8_t)(colorscheme->colors[i].color.b);
        comp_c->alpha = (double)(colorscheme->colors[i].color.a);
    }
    
    // Determine the size needed for a nodes packet
    packetsize = sizeof(nodes_hdr_t);
    for(int i = 0; i < destinationlist->num_destinations; i++) {
        packetsize += 2 + strlen(destinationlist->destinations[i].addr);
    }
    
    // Allocate and pack a nodes header
    uint8_t *packet2 = calloc(packetsize, sizeof(uint8_t));
    nodes_hdr_t *hdr2 = (nodes_hdr_t *)packet2;
    hdr2->packet_size = (uint32_t)packetsize;
    hdr2->type = HDR_NODES;
    hdr2->num_nodes = (uint16_t)(destinationlist->num_destinations);
    
    // Pack the node addresses after the header
    uint8_t *cur_pos = packet + sizeof(nodes_hdr_t);
    for(int i = 0; i < destinationlist->num_destinations; i++) {
        uint16_t len = strlen(destinationlist->destinations[i].addr);
        memcpy(cur_pos, &len, 2);
        memcpy(cur_pos + 2, &(destinationlist->destinations[i].addr), strlen(destinationlist->destinations[i].addr));
        cur_pos += 2 + strlen(destinationlist->destinations[i].addr);
    }
    
    printf("Done\n");
    printf("Initializing remote nodes...");
    
    // Launch a new thread for each remote node
    for(int i = 0; i < destinationlist->num_destinations; i++) {
        if(destinationlist->destinations[i].status == ANAX_STATE_NOJOB) {
            hdr->index = (uint8_t)i;
            threadarg_t *argt = malloc(sizeof(threadarg_t));
            argt->dest = &(destinationlist->destinations[i]);
            argt->init_pkt = packet;
            argt->init_pkt_size = (int)(hdr->packet_size);
            argt->nodes_pkt = packet2;
            argt->nodes_pkt_size = (int)(hdr2->packet_size);
            
            pthread_create(&(destinationlist->destinations[i].thread), NULL, runRemoteNode, argt);
        }
    }
    
    printf("Done\n");
    
    return 0;
}

int distributeJobs(destinationlist_t *destinationlist, joblist_t *joblist) {
    for(int i = 0; i < destinationlist->num_destinations; i++) {
        if(destinationlist->destinations[i].status == ANAX_STATE_NOJOB) {
            for(int j = 0; j < joblist->num_jobs; j++) {
                if(joblist->jobs[j].status != ANAX_STATE_PENDING)
                    continue;
                joblist->jobs[j].status = ANAX_STATE_INPROGRESS;
                destinationlist->destinations[i].status = ANAX_STATE_INPROGRESS;
                
                pthread_mutex_lock(&(destinationlist->destinations[i].ready_mutex));
                destinationlist->destinations[i].num_jobs++;
                destinationlist->destinations[i].jobs = realloc(destinationlist->destinations[i].jobs, destinationlist->destinations[i].num_jobs * sizeof(anaxjob_t *));
                destinationlist->destinations[i].jobs[destinationlist->destinations[i].num_jobs - 1] = &(joblist->jobs[j]);
                destinationlist->destinations[i].ready = 1;
                pthread_cond_signal(&(destinationlist->destinations[i].ready_cond));
                pthread_mutex_unlock(&(destinationlist->destinations[i].ready_mutex));
                
                printf("Assigning %s to %s.\n", joblist->jobs[j].name, destinationlist->destinations[i].addr);
                
                break;
            }
            
            // If there are no more jobs available, let the thread know it is done
            if(destinationlist->destinations[i].status == ANAX_STATE_NOJOB) {
                printf("Sending termination message to %s.\n", destinationlist->destinations[i].addr);
                pthread_mutex_lock(&(destinationlist->destinations[i].ready_mutex));
                destinationlist->destinations[i].complete = 1;
                destinationlist->destinations[i].ready = 1;
                pthread_cond_signal(&(destinationlist->destinations[i].ready_cond));
                pthread_mutex_unlock(&(destinationlist->destinations[i].ready_mutex));
            }
        }
    }
        
    return 0;
}

void *runRemoteNode(void *argt) {
    // Unpack thread argument struct
    destination_t *destination = ((threadarg_t *)argt)->dest;
    uint8_t *init_pkt = ((threadarg_t *)argt)->init_pkt;
    int init_pkt_size = ((threadarg_t *)argt)->init_pkt_size;
    uint8_t *nodes_pkt = ((threadarg_t *)argt)->nodes_pkt;
    int nodes_pkt_size = ((threadarg_t *)argt)->nodes_pkt_size;

    int bytes_sent = 0;
    int num_bytes = 0;

    // Send out initialization header
    while(bytes_sent < init_pkt_size) {
        bytes_sent += send(destination->socketfd, init_pkt + bytes_sent, init_pkt_size - bytes_sent, 0);
    }
    
    // Send out nodes header
    bytes_sent = 0;
    while(bytes_sent < nodes_pkt_size) {
        bytes_sent += send(destination->socketfd, nodes_pkt + bytes_sent, nodes_pkt_size - bytes_sent, 0);
    }
    
    // Send out jobs whenever the remote node is jobless
    while(!destination->complete) {
        // Block until a job has been assigned
        pthread_mutex_lock(&(destination->ready_mutex));
        while(!destination->ready) {
            pthread_cond_wait(&(destination->ready_cond), &(destination->ready_mutex));
        }
        pthread_mutex_unlock(&(destination->ready_mutex));
        destination->ready = 0;
        
        if(destination->complete)
            continue;
        
        // Get the job
        anaxjob_t *newjob = destination->jobs[destination->num_jobs - 1];
        
        // Identify whether a local GeoTIFF needs to be transferred to the remote host
        // or if the host can download it off a third-party server, then pack the
        // appropriate headers and payloads
        uint8_t *outbuf;
        
        if(strstr(newjob->name, "http://")) {
            // Only need to send URL

            // Allocate space for the packet
            num_bytes = sizeof(tiff_hdr_t) + strlen(newjob->name);
            outbuf = calloc(num_bytes, sizeof(uint8_t));
            
            // Pack the header and payload
            tiff_hdr_t *hdr = (tiff_hdr_t *)outbuf;
            hdr->packet_size = (uint32_t)num_bytes;
            hdr->type = HDR_TIFF;
            hdr->contents = PACKET_HAS_URL;
            hdr->string_length = (uint16_t)strlen(newjob->name);
            hdr->index = (uint16_t)(newjob->index);
            memcpy(outbuf + sizeof(tiff_hdr_t), newjob->name, strlen(newjob->name));
            
            // Send the packet to the remote node
            bytes_sent = 0;
            while(bytes_sent < num_bytes) {
                bytes_sent += send(destination->socketfd, outbuf + bytes_sent, num_bytes - bytes_sent, 0);
            }
        } else {
            // Need to send the GeoTIFF
            
            // Open the file
            FILE *fp = fopen(newjob->name, "r");
            
            // Get the file size
            fseek(fp, 0L, SEEK_END);
            int filesize = ftell(fp);
            rewind(fp);
            
            // Allocate space for the packet
            num_bytes = sizeof(tiff_hdr_t) + strlen(newjob->name);
            outbuf = calloc(num_bytes, sizeof(uint8_t));
            
            // Pack the header and payload
            tiff_hdr_t *hdr = (tiff_hdr_t *)outbuf;
            hdr->packet_size = (uint32_t)num_bytes;
            hdr->type = HDR_TIFF;
            hdr->contents = PACKET_HAS_DATA;
            hdr->string_length = (uint16_t)strlen(newjob->name);
            hdr->file_size = (uint32_t)filesize;
            hdr->index = (uint16_t)(newjob->index);
            memcpy(outbuf + sizeof(tiff_hdr_t), newjob->name, strlen(newjob->name));
            
            // Send the packet to the remote node
            bytes_sent = 0;
            while(bytes_sent < num_bytes) {
                bytes_sent += send(destination->socketfd, outbuf + bytes_sent, num_bytes - bytes_sent, 0);
            }
            
            // Send the file
            bytes_sent = 0;
            uint8_t buf[8192];
            while(bytes_sent < filesize) {
                int bytes_sent_tmp = 0;
                int bytes_to_send = fread(buf, sizeof(uint8_t), 8192, fp);
                while(bytes_sent_tmp < bytes_to_send) {
                    bytes_sent_tmp += send(destination->socketfd, buf, bytes_to_send - bytes_sent_tmp, 0);
                }
                bytes_sent += bytes_sent_tmp;
            }
            
            fclose(fp);
        }

        // Update local variables and signal the main thread that a new job is needed
        pthread_mutex_lock(&ready_mutex);
        destination->status = ANAX_STATE_NOJOB;
        pthread_cond_signal(&ready_cond);
        pthread_mutex_unlock(&ready_mutex);
        
        free(outbuf);
    }
        
    // Send an empty tiff header to alert the remote node that there are no more tiffs
    tiff_hdr_t *hdr = calloc(sizeof(tiff_hdr_t), sizeof(uint8_t));
    hdr->packet_size = (uint32_t)sizeof(tiff_hdr_t);
    hdr->type = HDR_TIFF;
    hdr->contents = PACKET_IS_EMPTY;
    bytes_sent = 0;
    while(bytes_sent < sizeof(tiff_hdr_t)) {
        bytes_sent += send(destination->socketfd, hdr, sizeof(tiff_hdr_t) - bytes_sent, 0);
    }
    
    // Handle LOADED, COMPLETE, and NOJOB status updates from remote
    int has_job = 1;
    while(has_job) {
        status_change_hdr_t buf;
        int bytes_rcvd = 0;
        while(bytes_rcvd < sizeof(status_change_hdr_t)) {
            bytes_rcvd += recv(destination->socketfd, &buf + bytes_rcvd, sizeof(status_change_hdr_t) - bytes_rcvd, 0);
        }
        if(buf.type == HDR_STATUS_CHANGE) {
            switch(buf.status) {
                case ANAX_STATE_LOADED:
                    destination->status = ANAX_STATE_LOADED;
                    //newjob->status = ANAX_STATE_LOADED;
                    break;
                case ANAX_STATE_COMPLETE:
                    destination->status = ANAX_STATE_COMPLETE;
                    //newjob->status = ANAX_STATE_COMPLETE;
                    break;
                case ANAX_STATE_NOJOB:
                    has_job = 0;
                    break;
            }
        }
    }
    
    return 0;
}

/*
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
    
    // Receive information on corner coordinates from remote machine
    double doublebuf[4];
    int bytes_rcvd = 0;
    while(bytes_rcvd < 4 * sizeof(double)) {
        bytes_rcvd += recv(destination->socketfd, &doublebuf + bytes_rcvd, 4 * sizeof(double) - bytes_rcvd, 0);
    }
    job->top_lat = doublebuf[0];
    job->bottom_lat = doublebuf[1];
    job->left_lon = doublebuf[2];
    job->right_lon = doublebuf[3];
    
    // Receive progress updates from remote machine
    
    // Receive output file from remote machine
    char outfile[FILENAME_MAX];
    sprintf(outfile, "/tmp/map%i.png", job->index);
    FILE *fp = fopen(outfile, "w+");
    
    uint32_t filesize = 0;
    bytes_rcvd = 0;
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
*/

int initRemoteListener(int *socketfd, char *port) {
    // Get socket
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    
    getaddrinfo(NULL, port, &hints, &res);
    
    int yes = 1;
    for(struct addrinfo *a = res; a; a = a->ai_next) {
        *socketfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if(*socketfd < 0)
            continue;
        if(setsockopt(*socketfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0)
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

int getInitHeaderData(int outsocket, int *whoami, colorscheme_t **colorscheme, double *scale) {
    int bytes_rcvd = 0;
    uint32_t packet_size;
    
    // Get packet size
    while(bytes_rcvd < sizeof(uint32_t)) {
        bytes_rcvd += recv(outsocket, &packet_size, sizeof(uint32_t), 0);
    }
    
    // Allocate a buffer
    uint8_t *buf = calloc(packet_size, sizeof(uint8_t));
    
    // Read in the rest of the packet
    while(bytes_rcvd < packet_size) {
        bytes_rcvd += recv(outsocket, buf + 4, packet_size - 4, 0);
    }
    init_hdr_t *hdr = (init_hdr_t *)buf;
    
    // Store the data
    if(hdr->type == HDR_INITIALIZATION) {
        *scale = hdr->scale;
        *whoami = (int)hdr->index;
        
        *colorscheme = malloc(sizeof(colorscheme_t));
        (*colorscheme)->isAbsolute = (int)(hdr->is_abs);
        (*colorscheme)->num_stops = (int)(hdr->num_colors);
        (*colorscheme)->colors = calloc(hdr->num_colors + 2, sizeof(colorstop_t));
        
        uint8_t *coloroffset = buf + sizeof(init_hdr_t);
        for(int i = 1; i <= hdr->num_colors; i++) {
            compressed_color_t *color = (compressed_color_t *)coloroffset + ((i - 1) * sizeof(compressed_color_t));
            (*colorscheme)->colors[i].elevation = (int16_t)(color->elevation);
            (*colorscheme)->colors[i].color.r = (int)(color->red);
            (*colorscheme)->colors[i].color.g = (int)(color->green);
            (*colorscheme)->colors[i].color.b = (int)(color->blue);
        }
        memcpy(&((*colorscheme)->colors[0]), &((*colorscheme)->colors[1]), sizeof(colorstop_t));
        memcpy(&((*colorscheme)->colors[(*colorscheme)->num_stops + 1]), &((*colorscheme)->colors[(*colorscheme)->num_stops]), sizeof(colorstop_t));
    }
    
    return 0;
}

int getNodesHeaderData(int outsocket, destinationlist_t **remotenodes) {
    int bytes_rcvd = 0;
    uint32_t packet_size;
    
    // Get packet size
    while(bytes_rcvd < sizeof(uint32_t)) {
        bytes_rcvd += recv(outsocket, &packet_size, sizeof(uint32_t), 0);
    }
    
    // Allocate a buffer
    uint8_t *buf = calloc(packet_size, sizeof(uint8_t));
    
    // Read in the rest of the packet
    while(bytes_rcvd < packet_size) {
        bytes_rcvd += recv(outsocket, buf + 4, packet_size - 4, 0);
    }
    nodes_hdr_t *hdr = (nodes_hdr_t *)buf;
    
    // Store the data
    if(hdr->type == HDR_NODES) {
        *remotenodes = malloc(sizeof(destinationlist_t));
        (*remotenodes)->num_destinations = (int)(hdr->num_nodes);
        (*remotenodes)->destinations = calloc(hdr->num_nodes, sizeof(destination_t));
        
        uint8_t *offset = buf + sizeof(nodes_hdr_t);
        for(int i = 0; i < hdr->num_nodes; i++) {
            uint16_t length = *((uint16_t *)offset);
            strncpy((*remotenodes)->destinations[i].addr, (char *)(offset + 2), length);
            (*remotenodes)->destinations[i].status = ANAX_STATE_NOJOB;
            (*remotenodes)->destinations[i].jobs = NULL;
            (*remotenodes)->destinations[i].socketfd = -1;
            offset += 2 + length;
        }
    }
    
    free(buf);
    
    return 0;
}

int getGeoTIFF(int outsocket, joblist_t *localjobs) {
    int bytes_rcvd = 0;
    uint32_t packet_size;
    
    // Get packet size
    while(bytes_rcvd < sizeof(uint32_t)) {
        bytes_rcvd += recv(outsocket, &packet_size, sizeof(uint32_t), 0);
    }
    
    // Allocate a buffer
    uint8_t *buf = calloc(packet_size, sizeof(uint8_t));
    
    // Read in the rest of the packet
    while(bytes_rcvd < packet_size) {
        bytes_rcvd += recv(outsocket, buf + 4, packet_size - 4, 0);
    }
    tiff_hdr_t *hdr = (tiff_hdr_t *)buf;
    
    if(hdr->type == HDR_TIFF) {
        if(hdr->contents == PACKET_IS_EMPTY) {
            return ANAX_ERR_NO_MAP;
        }
    
        // Set up local information struct
        localjobs->num_jobs++;
        localjobs->jobs = realloc(localjobs->jobs, localjobs->num_jobs * sizeof(anaxjob_t));
        anaxjob_t *current_job = &(localjobs->jobs[localjobs->num_jobs - 1]);
        current_job->name = calloc(hdr->string_length + 1, sizeof(char));
        strncpy(current_job->name, (char *)(buf + sizeof(tiff_hdr_t)), hdr->string_length);
        current_job->index = hdr->index;
        current_job->status = ANAX_STATE_PENDING;

        // Get and store what will be the file's local location
        char *filename_without_path = strrchr(current_job->name, '/');
        filename_without_path = (filename_without_path == NULL) ? current_job->name : filename_without_path + 1;
        current_job->outfile = calloc(strlen(filename_without_path) + 6, sizeof(char));
        sprintf(current_job->outfile, "/tmp/%s", filename_without_path);
        
        // Get file size
        uint32_t file_size = hdr->file_size;

        // Get image data
        if(hdr->contents == PACKET_HAS_DATA) {
            // Local file, must request transfer from originating node            
            getImageFromPrimary(outsocket, current_job->name, current_job->outfile, file_size);
        } else {
            // Remote file, must be downloaded
            downloadImage(current_job->name, current_job->outfile);
        }
    } else {
        return ANAX_ERR_INVALID_HEADER;
    }

    return 0;
}

int getImageFromPrimary(int outsocket, char *filename, char *outfile, uint32_t filesize) {
    int bytes_rcvd = 0;

    FILE *fp = fopen(outfile, "w+");
    
    printf("File size: %i\n", (int)filesize);

    printf("Receiving image from primary node... ");

    uint8_t buf[8192]; // 8 kilobytes
    while(bytes_rcvd < filesize) {
        int bytes_rcvd_tmp = recv(outsocket, buf, 8192, 0);
        fwrite(buf, sizeof(uint8_t), bytes_rcvd_tmp, fp);
        bytes_rcvd += bytes_rcvd_tmp;
    }
    
    printf("Done.\n");
    
    fclose(fp);
    
    return 0;
}

int downloadImage(char *filename, char *outfile) {
    printf("Downloading image... ");

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
    
    printf("Done.\n");
    
    fclose(fp);
    
    return 0;
}

int sendStatusUpdate(int outsocket, destinationlist_t *remotenodes, anaxjob_t *current_job, int whoami) {
    // Allocate status update header
    status_change_hdr_t *hdr = malloc(sizeof(status_change_hdr_t));
    
    // Pack status update header
    if(current_job) {
        // Send a status update for a single job
        hdr->packet_size = (uint32_t)sizeof(status_change_hdr_t);
        hdr->type = HDR_STATUS_CHANGE;
        hdr->status = current_job->status;
        hdr->job_id = current_job->index;
        hdr->sender_id = whoami;
        hdr->top = current_job->top_lat;
        hdr->bottom = current_job->bottom_lat;
        hdr->left = current_job->left_lon;
        hdr->right = current_job->right_lon;
    } else {
        // Inform all nodes that all of this node's GeoTIFFs have been loaded
        hdr->packet_size = (uint32_t)sizeof(status_change_hdr_t);
        hdr->type = HDR_STATUS_CHANGE;
        hdr->status = ANAX_STATE_RENDERING;
        hdr->job_id = 0;
        hdr->sender_id = whoami;
        hdr->top = 0;
        hdr->bottom = 0;
        hdr->left = 0;
        hdr->right = 0;
    }
    
    // Send update to all nodes
    for(int i = 0; i < remotenodes->num_destinations; i++) {
        if(i != whoami) {
            // Open a socket if one has not yet been created
            if(remotenodes->destinations[i].socketfd == -1) {
                connectToRemoteHost(&(remotenodes->destinations[i]), COMM_PORT);
            }
            
            // Send the update
            int bytes_sent = 0;
            while(bytes_sent < sizeof(status_change_hdr_t)) {
                bytes_sent += send(remotenodes->destinations[i].socketfd, hdr, sizeof(status_change_hdr_t) - bytes_sent, 0);
            }
        }
    }
    
    // Send update to primary node
    if(current_job) {
        int bytes_sent = 0;
        while(bytes_sent < sizeof(status_change_hdr_t)) {
            bytes_sent += send(outsocket, hdr, sizeof(status_change_hdr_t) - bytes_sent, 0);
        }
    }
    
    return 0;
}

int queryForMapFrameLocal(anaxjob_t *current_job, joblist_t *localjobs) {
    geotiffmap_t *current_map = NULL;
    geotiffmap_t *other_map = NULL;
    readMapData(current_job, &current_map);
    
    // North
    if(!current_job->frame_coordinates.N_set) {
        for(int i = 0; i < localjobs->num_jobs; i++) {
            if(current_job->frame_coordinates.north_lat > localjobs->jobs[i].bottom_lat && 
               current_job->frame_coordinates.north_lat < localjobs->jobs[i].top_lat &&
               current_job->frame_coordinates.mid_lon > localjobs->jobs[i].left_lon &&
               current_job->frame_coordinates.mid_lon < localjobs->jobs[i].right_lon) {
                readMapData(&(localjobs->jobs[i]), &other_map);
                for(int j = 0; j < MAPFRAME; j++) {
                    for(int k = MAPFRAME; k < current_map->width + MAPFRAME; k++) {
                        current_map->data[j][k].elevation = other_map->data[other_map->height + MAPFRAME + j][k].elevation;
                    }
                }
                freeMap(other_map);
                current_job->frame_coordinates.N_set = 1;
                break;
            }
        }
    }
    
    // South
    if(!current_job->frame_coordinates.S_set) {
        for(int i = 0; i < localjobs->num_jobs; i++) {
            if(current_job->frame_coordinates.south_lat < localjobs->jobs[i].top_lat &&
               current_job->frame_coordinates.south_lat > localjobs->jobs[i].bottom_lat &&
               current_job->frame_coordinates.mid_lon > localjobs->jobs[i].left_lon &&
               current_job->frame_coordinates.mid_lon < localjobs->jobs[i].right_lon) {
                readMapData(&(localjobs->jobs[i]), &other_map);
                for(int j = current_map->height + MAPFRAME; j < current_map->height + (2 * MAPFRAME); j++) {
                    for(int k = MAPFRAME; k < current_map->width + MAPFRAME; k++) {
                        current_map->data[j][k].elevation = other_map->data[j - other_map->height - MAPFRAME][k].elevation;
                    }
                }
                freeMap(other_map);
                current_job->frame_coordinates.S_set = 1;
                break;
            }
        }
    }
    
    // East
    if(!current_job->frame_coordinates.E_set) {
        for(int i = 0; i < localjobs->num_jobs; i++) {
            if(current_job->frame_coordinates.east_lon > localjobs->jobs[i].left_lon &&
               current_job->frame_coordinates.east_lon < localjobs->jobs[i].right_lon &&
               current_job->frame_coordinates.mid_lat > localjobs->jobs[i].bottom_lat &&
               current_job->frame_coordinates.mid_lat < localjobs->jobs[i].top_lat) {
                readMapData(&(localjobs->jobs[i]), &other_map);
                for(int j = MAPFRAME; j < current_map->height + MAPFRAME; j++) {
                    for(int k = current_map->width + MAPFRAME; k < current_map->width + (2 * MAPFRAME); k++) {
                        current_map->data[j][k].elevation = other_map->data[j][k - other_map->width - MAPFRAME].elevation;
                    }
                }
                freeMap(other_map);
                current_job->frame_coordinates.E_set = 1;
                break;
            }
        }
    }
    
    // West
    if(!current_job->frame_coordinates.W_set) {
        for(int i = 0; i < localjobs->num_jobs; i++) {
            if(current_job->frame_coordinates.west_lon < localjobs->jobs[i].left_lon &&
               current_job->frame_coordinates.west_lon > localjobs->jobs[i].right_lon &&
               current_job->frame_coordinates.mid_lat > localjobs->jobs[i].bottom_lat &&
               current_job->frame_coordinates.mid_lat < localjobs->jobs[i].top_lat) {
                readMapData(&(localjobs->jobs[i]), &other_map);
                for(int j = MAPFRAME; j < current_map->height + MAPFRAME; j++) {
                    for(int k = 0; k < MAPFRAME; k++) {
                        current_map->data[j][k].elevation = other_map->data[j][other_map->width + MAPFRAME + k].elevation;
                    }
                }
                freeMap(other_map);
                current_job->frame_coordinates.W_set = 1;
                break;
            }
        }
    }
    
    // Northeast
    if(!current_job->frame_coordinates.NE_set) {
        for(int i = 0; i < localjobs->num_jobs; i++) {
            if(current_job->frame_coordinates.east_lon > localjobs->jobs[i].left_lon &&
               current_job->frame_coordinates.east_lon < localjobs->jobs[i].right_lon &&
               current_job->frame_coordinates.north_lat > localjobs->jobs[i].bottom_lat &&
               current_job->frame_coordinates.north_lat < localjobs->jobs[i].top_lat) {
                readMapData(&(localjobs->jobs[i]), &other_map);
                for(int j = 0; j < MAPFRAME; j++) {
                    for(int k = current_map->width + MAPFRAME; k < current_map->width + (2 * MAPFRAME); k++) {
                        current_map->data[j][k].elevation = other_map->data[other_map->height + MAPFRAME + j][k - other_map->width - MAPFRAME].elevation;
                    }
                }
                freeMap(other_map);
                current_job->frame_coordinates.NE_set = 1;
                break;
            }
        }
    }
    
    // Southeast
    if(!current_job->frame_coordinates.SE_set) {
        for(int i = 0; i < localjobs->num_jobs; i++) {
            if(current_job->frame_coordinates.east_lon > localjobs->jobs[i].left_lon &&
               current_job->frame_coordinates.east_lon < localjobs->jobs[i].right_lon &&
               current_job->frame_coordinates.south_lat < localjobs->jobs[i].bottom_lat &&
               current_job->frame_coordinates.south_lat > localjobs->jobs[i].top_lat) {
                readMapData(&(localjobs->jobs[i]), &other_map);
                for(int j = current_map->height + MAPFRAME; j < current_map->height + (2 * MAPFRAME); j++) {
                    for(int k = current_map->width + MAPFRAME; k < current_map->width + (2 * MAPFRAME); k++) {
                        current_map->data[j][k].elevation = other_map->data[j - other_map->height - MAPFRAME][k - other_map->width - MAPFRAME].elevation;
                    }
                }
                freeMap(other_map);
                current_job->frame_coordinates.SE_set = 1;
                break;
            }
        }
    }
    
    // Southwest
    if(!current_job->frame_coordinates.SW_set) {
        for(int i = 0; i < localjobs->num_jobs; i++) {
            if(current_job->frame_coordinates.west_lon < localjobs->jobs[i].right_lon &&
               current_job->frame_coordinates.west_lon > localjobs->jobs[i].left_lon &&
               current_job->frame_coordinates.south_lat < localjobs->jobs[i].bottom_lat &&
               current_job->frame_coordinates.south_lat > localjobs->jobs[i].top_lat) {
                readMapData(&(localjobs->jobs[i]), &other_map);
                for(int j = current_map->height + MAPFRAME; j < current_map->height + (2 * MAPFRAME); j++) {
                    for(int k = 0; k < MAPFRAME; k++) {
                        current_map->data[j][k].elevation = other_map->data[j - other_map->height - MAPFRAME][other_map->width + MAPFRAME + k].elevation;
                    }
                }
                freeMap(other_map);
                current_job->frame_coordinates.SW_set = 1;
                break;
            }
        }
    }
    
    // Northwest
    if(!current_job->frame_coordinates.NW_set) {
        for(int i = 0; i < localjobs->num_jobs; i++) {
            if(current_job->frame_coordinates.west_lon < localjobs->jobs[i].right_lon &&
               current_job->frame_coordinates.west_lon > localjobs->jobs[i].left_lon &&
               current_job->frame_coordinates.north_lat > localjobs->jobs[i].bottom_lat &&
               current_job->frame_coordinates.north_lat < localjobs->jobs[i].top_lat) {
                readMapData(&(localjobs->jobs[i]), &other_map);
                for(int j = 0; j < MAPFRAME; j++) {
                    for(int k = 0; k < MAPFRAME; k++) {
                        current_map->data[j][k].elevation = other_map->data[other_map->height + MAPFRAME + j][other_map->width + MAPFRAME + k].elevation;
                    }
                }
                freeMap(other_map);
                current_job->frame_coordinates.NW_set = 1;
                break;
            }
        }
    }
    
    // Check if map is now complete
    if(current_job->frame_coordinates.N_set &&
       current_job->frame_coordinates.S_set &&
       current_job->frame_coordinates.E_set &&
       current_job->frame_coordinates.W_set &&
       current_job->frame_coordinates.NE_set &&
       current_job->frame_coordinates.SE_set &&
       current_job->frame_coordinates.SW_set &&
       current_job->frame_coordinates.NW_set) {
        current_job->status = ANAX_STATE_RENDERING;
    }
    
    // Write the map
    writeMapData(current_job, current_map);
    
    return 0;
}

int queryForMapFrame(anaxjob_t *current_job, destinationlist_t *remotenodes) {
    // North
    if(!current_job->frame_coordinates.N_set) {
        for(int i = 0; i < remotenodes->num_destinations; i++) {
            for(int j = 0; j < remotenodes->destinations[i].num_jobs; j++) {
                if(current_job->frame_coordinates.north_lat > remotenodes->destinations[i].jobs[j]->bottom_lat &&
                   current_job->frame_coordinates.north_lat < remotenodes->destinations[i].jobs[j]->top_lat &&
                   current_job->frame_coordinates.mid_lon > remotenodes->destinations[i].jobs[j]->left_lon &&
                   current_job->frame_coordinates.mid_lon < remotenodes->destinations[i].jobs[j]->right_lon) {
                    requestMapFrame(current_job, &(remotenodes->destinations[i]), j, ANAX_MAP_SOUTH);
                    current_job->frame_coordinates.N_set = 2; // Requested; do not re-request
                }
            }
        }
    }
    
    // South
    if(!current_job->frame_coordinates.S_set) {
        for(int i = 0; i < remotenodes->num_destinations; i++) {
            for(int j = 0; j < remotenodes->destinations[i].num_jobs; j++) {
                if(current_job->frame_coordinates.south_lat < remotenodes->destinations[i].jobs[j]->top_lat &&
                   current_job->frame_coordinates.south_lat > remotenodes->destinations[i].jobs[j]->bottom_lat &&
                   current_job->frame_coordinates.mid_lon > remotenodes->destinations[i].jobs[j]->left_lon &&
                   current_job->frame_coordinates.mid_lon < remotenodes->destinations[i].jobs[j]->right_lon) {
                    requestMapFrame(current_job, &(remotenodes->destinations[i]), j, ANAX_MAP_NORTH);
                    current_job->frame_coordinates.S_set = 2; // Requested; do not re-request
                }
            }
        }
    }

    // East
    if(!current_job->frame_coordinates.E_set) {
        for(int i = 0; i < remotenodes->num_destinations; i++) {
            for(int j = 0; j < remotenodes->destinations[i].num_jobs; j++) {
                if(current_job->frame_coordinates.east_lon > remotenodes->destinations[i].jobs[j]->left_lon &&
                   current_job->frame_coordinates.east_lon < remotenodes->destinations[i].jobs[j]->right_lon &&
                   current_job->frame_coordinates.mid_lat > remotenodes->destinations[i].jobs[j]->bottom_lat &&
                   current_job->frame_coordinates.mid_lat < remotenodes->destinations[i].jobs[j]->top_lat) {
                    requestMapFrame(current_job, &(remotenodes->destinations[i]), j, ANAX_MAP_WEST);
                    current_job->frame_coordinates.E_set = 2; // Requested; do not re-request
                }
            }
        }
    }
    
    // West
    if(!current_job->frame_coordinates.W_set) {
        for(int i = 0; i < remotenodes->num_destinations; i++) {
            for(int j = 0; j < remotenodes->destinations[i].num_jobs; j++) {
                if(current_job->frame_coordinates.west_lon < remotenodes->destinations[i].jobs[j]->left_lon &&
                   current_job->frame_coordinates.west_lon > remotenodes->destinations[i].jobs[j]->right_lon &&
                   current_job->frame_coordinates.mid_lat > remotenodes->destinations[i].jobs[j]->bottom_lat &&
                   current_job->frame_coordinates.mid_lat < remotenodes->destinations[i].jobs[j]->top_lat) {
                    requestMapFrame(current_job, &(remotenodes->destinations[i]), j, ANAX_MAP_EAST);
                    current_job->frame_coordinates.W_set = 2; // Requested; do not re-request
                }
            }
        }
    }
    
    // Northeast
    if(!current_job->frame_coordinates.NE_set) {
        for(int i = 0; i < remotenodes->num_destinations; i++) {
            for(int j = 0; j < remotenodes->destinations[i].num_jobs; j++) {
                if(current_job->frame_coordinates.east_lon > remotenodes->destinations[i].jobs[j]->left_lon &&
                   current_job->frame_coordinates.east_lon < remotenodes->destinations[i].jobs[j]->right_lon &&
                   current_job->frame_coordinates.north_lat > remotenodes->destinations[i].jobs[j]->bottom_lat &&
                   current_job->frame_coordinates.north_lat < remotenodes->destinations[i].jobs[j]->top_lat) {
                    requestMapFrame(current_job, &(remotenodes->destinations[i]), j, ANAX_MAP_SOUTHWEST);
                    current_job->frame_coordinates.NE_set = 2; // Requested; do not re-request
                }
            }
        }
    }

    // Southeast
    if(!current_job->frame_coordinates.SE_set) {
        for(int i = 0; i < remotenodes->num_destinations; i++) {
            for(int j = 0; j < remotenodes->destinations[i].num_jobs; j++) {
                if(current_job->frame_coordinates.east_lon > remotenodes->destinations[i].jobs[j]->left_lon &&
                   current_job->frame_coordinates.east_lon < remotenodes->destinations[i].jobs[j]->right_lon &&
                   current_job->frame_coordinates.south_lat < remotenodes->destinations[i].jobs[j]->bottom_lat &&
                   current_job->frame_coordinates.south_lat > remotenodes->destinations[i].jobs[j]->top_lat) {
                    requestMapFrame(current_job, &(remotenodes->destinations[i]), j, ANAX_MAP_NORTHWEST);
                    current_job->frame_coordinates.SE_set = 2; // Requested; do not re-request
                }
            }
        }
    }

    // Southwest
    if(!current_job->frame_coordinates.SW_set) {
        for(int i = 0; i < remotenodes->num_destinations; i++) {
            for(int j = 0; j < remotenodes->destinations[i].num_jobs; j++) {
                if(current_job->frame_coordinates.west_lon < remotenodes->destinations[i].jobs[j]->right_lon &&
                   current_job->frame_coordinates.west_lon > remotenodes->destinations[i].jobs[j]->left_lon &&
                   current_job->frame_coordinates.south_lat < remotenodes->destinations[i].jobs[j]->bottom_lat &&
                   current_job->frame_coordinates.south_lat > remotenodes->destinations[i].jobs[j]->top_lat) {
                    requestMapFrame(current_job, &(remotenodes->destinations[i]), j, ANAX_MAP_NORTHEAST);
                    current_job->frame_coordinates.SW_set = 2; // Requested; do not re-request
                }
            }
        }
    }

    // Northwest
    if(!current_job->frame_coordinates.NW_set) {
        for(int i = 0; i < remotenodes->num_destinations; i++) {
            for(int j = 0; j < remotenodes->destinations[i].num_jobs; j++) {
                if(current_job->frame_coordinates.west_lon < remotenodes->destinations[i].jobs[j]->right_lon &&
                   current_job->frame_coordinates.west_lon > remotenodes->destinations[i].jobs[j]->left_lon &&
                   current_job->frame_coordinates.north_lat > remotenodes->destinations[i].jobs[j]->bottom_lat &&
                   current_job->frame_coordinates.north_lat < remotenodes->destinations[i].jobs[j]->top_lat) {
                    requestMapFrame(current_job, &(remotenodes->destinations[i]), j, ANAX_MAP_SOUTHEAST);
                    current_job->frame_coordinates.NW_set = 2; // Requested; do not re-request
                }
            }
        }
    }
    
    return 0;
}

int requestMapFrame(anaxjob_t *current_job, destination_t *remote, int index, int request) {
    // Allocate a frame request header
    req_edge_hdr_t *hdr = malloc(sizeof(req_edge_hdr_t));
    
    // Pack the header
    hdr->packet_size = (uint32_t)sizeof(req_edge_hdr_t);
    hdr->type = HDR_REQ_EDGE;
    hdr->part = (uint8)request;
    hdr->requesting_job_id = (uint16_t)(current_job->index);
    hdr->requested_job_id = (uint16_t)index;
    
    // Send the request
    int bytes_sent = 0;
    while(bytes_sent < sizeof(req_edge_hdr_t)) {
        bytes_sent += send(remote->socketfd, hdr + bytes_sent, sizeof(req_edge_hdr_t) - bytes_sent, 0);
    }
    
    free(hdr);
    
    return 0;
}

int sendMinMax(destinationlist_t *remotenodes, int local_min, int local_max, int whoami) {
    // Allocate a min/max header
    min_max_hdr_t *hdr = malloc(sizeof(min_max_hdr_t));
    
    // Pack the header
    hdr->packet_size = (uint32_t)sizeof(min_max_hdr_t);
    hdr->type = HDR_SEND_MIN_MAX;
    hdr->min = (int32_t)local_min;
    hdr->max = (int32_t)local_max;

    // Send
    for(int i = 0; i < remotenodes->num_destinations; i++) {
        if(i != whoami) {
            int bytes_sent = 0;
            while(bytes_sent < sizeof(min_max_hdr_t)) {
                bytes_sent += send(remotenodes->destinations[i].socketfd, hdr + bytes_sent, sizeof(min_max_hdr_t) - bytes_sent, 0);
            }
        }
    }
    
    free(hdr);
    
    return 0;
}

void *spawnShareThread(void *argt) {
    int sharesocketfd, shareoutsocketfd;
    int err = initRemoteListener(&sharesocketfd, COMM_PORT);
    while(1) {
        struct sockaddr_in clientAddr;
        socklen_t sinSize = sizeof(struct sockaddr_in);
        shareoutsocketfd = accept(sharesocketfd, (struct sockaddr *)&clientAddr, &sinSize);
        threadshare_t *new_argt = malloc(sizeof(threadshare_t));
        memcpy(new_argt, argt, sizeof(threadshare_t));
        new_argt->socket = shareoutsocketfd;
        pthread_t sharethread;
        pthread_create(&sharethread, NULL, handleSharing, new_argt);
    }
}

void *handleSharing(void *argt) {
    // Unpack thread argument struct
    destinationlist_t *remotenodes = ((threadshare_t *)argt)->remotenodes;
    joblist_t *localjobs = ((threadshare_t *)argt)->localjobs;
    int *global_max = ((threadshare_t *)argt)->global_max;
    int *global_min = ((threadshare_t *)argt)->global_min;
    int whoami = ((threadshare_t *)argt)->whoami;
    int socket = ((threadshare_t *)argt)->socket;
    
    // Handle incoming requests
    while(1) {
        int bytes_rcvd = 0;
        uint32_t packet_size;
        
        // Get packet size
        while(bytes_rcvd < sizeof(uint32_t)) {
            bytes_rcvd += recv(socket, &packet_size, sizeof(uint32_t), 0);
        }
        
        // Allocate a buffer
        uint8_t *buf = calloc(packet_size, sizeof(uint8_t));
        
        // Read in the rest of the packet
        while(bytes_rcvd < packet_size) {
            bytes_rcvd += recv(socket, buf + 4, packet_size - 4, 0);
        }
        
        // Handle different packet types
        switch(buf[4]) {
            case HDR_STATUS_CHANGE:
            {
                status_change_hdr_t *hdr = (status_change_hdr_t *)buf;
                remotenodes->destinations[hdr->sender_id].jobs[hdr->job_id]->status = hdr->status;
                remotenodes->destinations[hdr->sender_id].jobs[hdr->job_id]->top_lat = hdr->top;
                remotenodes->destinations[hdr->sender_id].jobs[hdr->job_id]->bottom_lat = hdr->bottom;
                remotenodes->destinations[hdr->sender_id].jobs[hdr->job_id]->left_lon = hdr->left;
                remotenodes->destinations[hdr->sender_id].jobs[hdr->job_id]->right_lon = hdr->right;
                break;
            }
            case HDR_REQ_EDGE:
            {
                req_edge_hdr_t *hdr = (req_edge_hdr_t *)buf;
                
                // Load requested map from memory
                geotiffmap_t *map;
                readMapData(&(localjobs->jobs[hdr->requested_job_id]), &map);
                
                // Identify and pack the desired data
                int nrows, ncols;
                int16_t *buf;
                int pos = 0;
                switch(hdr->part) {
                    case ANAX_MAP_NORTH:
                        nrows = MAPFRAME;
                        ncols = map->width;
                        buf = calloc(nrows * ncols, sizeof(int16_t));
                        for(int i = 0; i < nrows; i++) {
                            for(int j = MAPFRAME; j < ncols + MAPFRAME; j++) {
                                buf[pos] = map->data[i][j].elevation;
                                pos++;
                            }
                        }
                        break;
                    case ANAX_MAP_SOUTH:
                        nrows = MAPFRAME;
                        ncols = map->width;
                        buf = calloc(nrows * ncols, sizeof(int16_t));
                        for(int i = MAPFRAME + nrows; i < (MAPFRAME * 2) + nrows; i++) {
                            for(int j = MAPFRAME; j < ncols + MAPFRAME; j++) {
                                buf[pos] = map->data[i][j].elevation;
                                pos++;
                            }
                        }
                        break;
                    case ANAX_MAP_EAST:
                        nrows = map->height;
                        ncols = MAPFRAME;
                        buf = calloc(nrows * ncols, sizeof(int16_t));
                        for(int i = MAPFRAME; i < MAPFRAME + nrows; i++) {
                            for(int j = MAPFRAME + ncols; j < (MAPFRAME * 2) + ncols; j++) {
                                buf[pos] = map->data[i][j].elevation;
                                pos++;
                            }
                        }
                        break;
                    case ANAX_MAP_WEST:
                        nrows = map->height;
                        ncols = MAPFRAME;
                        buf = calloc(nrows * ncols, sizeof(int16_t));
                        for(int i = MAPFRAME; i < MAPFRAME + nrows; i++) {
                            for(int j = 0; j < ncols; j++) {
                                buf[pos] = map->data[i][j].elevation;
                                pos++;
                            }
                        }
                        break;
                    case ANAX_MAP_NORTHEAST:
                        nrows = MAPFRAME;
                        ncols = MAPFRAME;
                        buf = calloc(nrows * ncols, sizeof(int16_t));
                        for(int i = 0; i < nrows; i++) {
                            for(int j = MAPFRAME + map->width; j < MAPFRAME + map->width + ncols; j++) {
                                buf[pos] = map->data[i][j].elevation;
                                pos++;
                            }
                        }
                        break;
                    case ANAX_MAP_SOUTHEAST:
                        nrows = MAPFRAME;
                        ncols = MAPFRAME;
                        buf = calloc(nrows * ncols, sizeof(int16_t));
                        for(int i = MAPFRAME + map->height; i < MAPFRAME + map->height + nrows; i++) {
                            for(int j = MAPFRAME + map->width; j < MAPFRAME + map->width + ncols; j++) {
                                buf[pos] = map->data[i][j].elevation;
                                pos++;
                            }
                        }
                        break;
                    case ANAX_MAP_SOUTHWEST:
                        nrows = MAPFRAME;
                        ncols = MAPFRAME;
                        buf = calloc(nrows * ncols, sizeof(int16_t));
                        for(int i = MAPFRAME + map->height; i < MAPFRAME + map->height + nrows; i++) {
                            for(int j = 0; j < ncols; j++) {
                                buf[pos] = map->data[i][j].elevation;
                                pos++;
                            }
                        }
                        break;
                    case ANAX_MAP_NORTHWEST:
                        nrows = MAPFRAME;
                        ncols = MAPFRAME;
                        buf = calloc(nrows * ncols, sizeof(int16_t));
                        for(int i = 0; i < nrows; i++) {
                            for(int j = 0; j < ncols; j++) {
                                buf[pos] = map->data[i][j].elevation;
                                pos++;
                            }
                        }
                        break;
                }
                
                // Allocate a response header
                send_edge_hdr_t *outhdr = malloc(sizeof(send_edge_hdr_t));
                
                // Pack the response header
                outhdr->packet_size = (uint32_t)sizeof(send_edge_hdr_t);
                outhdr->type = HDR_SEND_EDGE;
                outhdr->part = hdr->part;
                outhdr->datasize = (uint16_t)(nrows * ncols);
                outhdr->requesting_job_id = hdr->requesting_job_id;
                outhdr->requested_job_id = hdr->requested_job_id;
                
                // Send the response and data
                int bytes_sent = 0;
                while(bytes_sent < sizeof(send_edge_hdr_t)) {
                    bytes_sent += send(socket, outhdr, sizeof(send_edge_hdr_t) - bytes_sent, 0);
                }
                bytes_sent = 0;
                while(bytes_sent < nrows * ncols) {
                    bytes_sent += send(socket, buf, (nrows * ncols) - bytes_sent, 0);
                }
                
                freeMap(map);
                break;
            }
            case HDR_SEND_EDGE:
            {
                // Get the header
                int bytes_rcvd = 0;
                send_edge_hdr_t *hdr = malloc(sizeof(send_edge_hdr_t));
                while(bytes_rcvd < sizeof(send_edge_hdr_t)) {
                    bytes_rcvd += recv(socket, hdr, sizeof(send_edge_hdr_t) - bytes_rcvd, 0);
                }
                
                // Get the data
                uint8_t *databuf = calloc(hdr->datasize, sizeof(uint8_t));
                bytes_rcvd = 0;
                while(bytes_rcvd < hdr->datasize) {
                    bytes_rcvd += recv(socket, databuf, hdr->datasize - bytes_rcvd, 0);
                }
                
                // Load the map
                // TODO: there need to be locks here
                geotiffmap_t *map;
                anaxjob_t *current_job = &(localjobs->jobs[hdr->requesting_job_id]);
                readMapData(current_job, &map);
                
                // Add the new data
                int pos = 0;
                switch(hdr->part) {
                    case ANAX_MAP_NORTH:
                        if(current_job->frame_coordinates.S_set == 2) {
                            // North side of original map -> add to south of current map
                            for(int i = MAPFRAME + map->height; i < (2 * MAPFRAME) + map->height; i++) {
                                for(int j = MAPFRAME; j < MAPFRAME + map->width; j++) {
                                    map->data[i][j].elevation = databuf[pos++];
                                }
                            }
                            // Set flags
                            current_job->frame_coordinates.S_set = 1;
                        }
                        break;
                    case ANAX_MAP_SOUTH:
                        if(current_job->frame_coordinates.N_set == 2) {
                            // South side of original map -> add to north of current map
                            for(int i = 0; i < MAPFRAME; i++) {
                                for(int j = MAPFRAME; j < MAPFRAME + map->width; j++) {
                                    map->data[i][j].elevation = databuf[pos++];
                                }
                            }
                            // Set flags
                            current_job->frame_coordinates.N_set = 1;
                        }
                        break;
                    case ANAX_MAP_EAST:
                        if(current_job->frame_coordinates.W_set == 2) {
                            // East side of original map -> add to west of current map
                            for(int i = MAPFRAME; i < MAPFRAME + map->height; i++) {
                                for(int j = 0; j < MAPFRAME; j++) {
                                    map->data[i][j].elevation = databuf[pos++];
                                }
                            }
                            // Set flags
                            current_job->frame_coordinates.W_set = 1;
                        }
                        break;
                    case ANAX_MAP_WEST:
                        if(current_job->frame_coordinates.E_set == 2) {
                            // West side of original map -> add to east of current map
                            for(int i = MAPFRAME; i < MAPFRAME + map->height; i++) {
                                for(int j = MAPFRAME + map->width; j < (2 * MAPFRAME) + map->width; j++) {
                                    map->data[i][j].elevation = databuf[pos++];
                                }
                            }
                            // Set flags
                            current_job->frame_coordinates.E_set = 1;
                        }
                        break;
                    case ANAX_MAP_NORTHEAST:
                        if(current_job->frame_coordinates.SW_set == 2) {
                            // Northeast side of original map -> add to southwest of current map
                            for(int i = MAPFRAME + map->height; i < (2 * MAPFRAME) + map->height; i++) {
                                for(int j = 0; j < MAPFRAME; j++) {
                                    map->data[i][j].elevation = databuf[pos++];
                                }
                            }
                            // Set flags
                            current_job->frame_coordinates.SW_set = 1;
                        }
                        break;
                    case ANAX_MAP_SOUTHEAST:
                        if(current_job->frame_coordinates.NW_set == 2) {
                            // Southeast side of original map -> add to northwest of current map
                            for(int i = 0; i < MAPFRAME; i++) {
                                for(int j = 0; j < MAPFRAME; j++) {
                                    map->data[i][j].elevation = databuf[pos++];
                                }
                            }
                            // Set flags
                            current_job->frame_coordinates.NW_set = 1;
                        }
                        break;
                    case ANAX_MAP_SOUTHWEST:
                        if(current_job->frame_coordinates.NE_set == 2) {
                            // Southwest side of original map -> add to northeast of current map
                            for(int i = 0; i < MAPFRAME; i++) {
                                for(int j = MAPFRAME + map->width; j < (2 * MAPFRAME) + map->width; j++) {
                                    map->data[i][j].elevation = databuf[pos++];
                                }
                            }
                            // Set flags
                            current_job->frame_coordinates.NE_set = 1;
                        }
                        break;
                    case ANAX_MAP_NORTHWEST:
                        if(current_job->frame_coordinates.SE_set == 2) {
                            // Northwest side of original map -> add to southeast of current map
                            for(int i = MAPFRAME + map->height; i < (2 * MAPFRAME) + map->height; i++) {
                                for(int j = MAPFRAME + map->width; j < (2 * MAPFRAME) + map->width; j++) {
                                    map->data[i][j].elevation = databuf[pos++];
                                }
                            }
                            // Set flags
                            current_job->frame_coordinates.SE_set = 1;
                        }
                        break;
                }
                
                // Write the map
                writeMapData(current_job, map);
                
                // Check if map is now complete
                if(current_job->frame_coordinates.N_set &&
                  current_job->frame_coordinates.S_set &&
                  current_job->frame_coordinates.E_set &&
                  current_job->frame_coordinates.W_set &&
                  current_job->frame_coordinates.NE_set &&
                  current_job->frame_coordinates.SE_set &&
                  current_job->frame_coordinates.SW_set &&
                  current_job->frame_coordinates.NW_set) {
                   current_job->status = ANAX_STATE_RENDERING;
                }
                break;
            }
            case HDR_SEND_MIN_MAX:
            {
                min_max_hdr_t *hdr = (min_max_hdr_t *)buf;
                *global_max = (*global_max > hdr->max) ? *global_max : hdr->max;
                *global_min = (*global_min > hdr->min) ? *global_min : hdr->min;
                break;
            }
        }
        
        free(buf);
    }
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

int countJobless(destinationlist_t *dests) {
    int count = 0;
    for(int i = 0; i < dests->num_destinations; i++) {
        if(dests->destinations[i].status == ANAX_STATE_NOJOB)
            count++;
    }
    
    return count;
}

int sendCorners(int outsocket, double top, double bottom, double left, double right) {
    double buf[4];
    buf[0] = top;
    buf[1] = bottom;
    buf[2] = left;
    buf[3] = right;
    
    int bytes_sent = 0;
    while(bytes_sent < 4 * sizeof(double)) {
        bytes_sent += send(outsocket, &buf, 4, 0);
    }
    
    return 0;
}



