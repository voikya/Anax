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
#include "anaxcurses.h"

#define HDR_INITIALIZATION      0x01
#define HDR_NODES               0x02
#define HDR_TIFF                0x03
#define HDR_STATUS_CHANGE       0x04
#define HDR_REQ_EDGE            0x05
#define HDR_SEND_EDGE           0x06
#define HDR_SEND_MIN_MAX        0x07
#define HDR_PNG                 0x08
#define HDR_END                 0x09
#define HDR_UI_UPDATE           0x10

#define PACKET_HAS_DATA         0x01
#define PACKET_HAS_URL          0x02
#define PACKET_IS_EMPTY         0x03

#define ROLE_SENDER             1
#define ROLE_RECEIVER           2



/////
// HEADER STRUCTS
/////

struct header_initialization {
    uint32_t packet_size;
    uint8_t type; // HDR_INITIALIZATION
    uint8_t is_abs;
    uint8_t show_water;
    uint8_t num_colors;
    uint8_t index;
    uint8_t relief;
    uint8_t projection;
    uint8_t fill[5];
    double scale;
    // Followed by an array of compressed_color_t
};
typedef struct header_initialization init_hdr_t;

struct compressed_color {
    int32_t elevation;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t fill;
    double alpha;
};
typedef struct compressed_color compressed_color_t;

struct header_nodes {
    uint32_t packet_size;
    uint8_t type; // HDR_NODES
    uint8_t fill;
    uint16_t num_nodes;
    // Followed by data array (sequences of uint16_t, string)
};
typedef struct header_nodes nodes_hdr_t;

struct header_tiff {
    uint32_t packet_size;
    uint8_t type; // HDR_TIFF
    uint8_t contents; // PACKET_HAS_DATA, PACKET_HAS_URL, or PACKET_IS_EMPTY
    uint16_t string_length;
    uint32_t file_size;
    uint16_t index;
    uint8_t fill[2];
    // Followed by string representing file name or URL
    // Followed by TIFF file (if PACKET_HAS_DATA) or nothing (if PACKET_HAS_URL)
};
typedef struct header_tiff tiff_hdr_t;

struct header_status {
    uint32_t packet_size;
    uint8_t type; // HDR_STATUS_CHANGE
    uint8_t status; // ANAX_STATE_*
    uint16_t job_id;
    uint16_t sender_id;
    uint8_t fill[6];
    double top;
    double bottom;
    double left;
    double right;
};
typedef struct header_status status_change_hdr_t;

struct header_ui_update {
    uint32_t packet_size;
    uint8_t type; // HDR_UI_UPDATE
    uint8_t status; // UI_STATE_*
    uint16_t job_id;
};
typedef struct header_ui_update ui_hdr_t;

struct header_request_edge {
    uint32_t packet_size;
    uint8_t type; // HDR_REQ_EDGE
    uint8_t part; // ANAX_MAP_*
    uint16_t requesting_job_id;
    uint16_t requested_job_id;
    uint8_t fill[6];
};
typedef struct header_request_edge req_edge_hdr_t;

struct header_send_edge {
    uint32_t packet_size;
    uint8_t type; // HDR_SEND_EDGE
    uint8_t part; // ANAX_MAP_*
    uint16_t requesting_job_id;
    uint16_t requested_job_id;
    uint32_t datasize;
    uint8_t fill[2];
    // Followed by data array
};
typedef struct header_send_edge send_edge_hdr_t;

struct header_min_max {
    uint32_t packet_size;
    uint8_t type; // HDR_SEND_MIN_MAX
    uint8_t fill[3];
    int32_t min;
    int32_t max;
};
typedef struct header_min_max min_max_hdr_t;

struct header_png {
    uint32_t packet_size;
    uint8_t type; // HDR_PNG
    uint16_t index;
    uint8_t fill;
    uint32_t img_height;
    uint32_t img_width;
    double top;
    double bottom;
    double left;
    double right;
    // Followed by data array
};
typedef struct header_png png_hdr_t;

struct header_end {
    uint32_t packet_size;
    uint8_t type; // HDR_END
};
typedef struct header_end end_hdr_t;


/////
// PTHREAD ARGUMENT STRUCTS
/////

struct thread_arguments {
    destination_t *dest;
    tilelist_t *tilelist;
    uint8_t *init_pkt;
    int init_pkt_size;
    uint8_t *nodes_pkt;
    int nodes_pkt_size;
    int index;
    uilist_t *uilist;
};
typedef struct thread_arguments threadarg_t;

struct share_arguments {
    destinationlist_t *remotenodes;
    joblist_t *localjobs;
    int *global_max;
    int *global_min;
    int whoami;
    int socket;
};
typedef struct share_arguments threadshare_t;

struct mapframe_thread_arguments {
    int socket;
    int numbytes;
    pthread_mutex_t *lock;
    pthread_mutex_t *lock2;
    send_edge_hdr_t *hdr;
    int16_t *buf;
};
typedef struct mapframe_thread_arguments mapframe_threadarg_t;

struct png_thread_arguments {
    int socket;
    png_hdr_t *hdr;
    FILE *png;
};
typedef struct png_thread_arguments png_threadarg_t;


/////
// FUNCTION DECLARATIONS
/////

void *get_in_addr(struct sockaddr *sa);
int loadDestinationList(char *destfile, destinationlist_t **destinations);
int connectToRemoteHost(destination_t *dest, char *port);
int initRemoteHosts(destinationlist_t *destinationlist, tilelist_t *tilelist, colorscheme_t *colorscheme, double scale, int relief, int projection, uilist_t *uilist);
int distributeJobs(destinationlist_t *destinationlist, joblist_t *joblist);
void *runRemoteNode(void *argt);
void *runRemoteJob(void *argt);
int initRemoteListener(int *socketfd, char *port);
int getInitHeaderData(int outsocket, int *whoami, colorscheme_t **colorscheme, double *scale, int *relief, int *projection);
int getNodesHeaderData(int outsocket, destinationlist_t **remotenodes);
int getGeoTIFF(int outsocket, joblist_t *localjobs);
int getImageFromPrimary(int outsocket, char *filename, char *outfile, uint32_t filesize);
int downloadImage(char *filename, char *outfile);
int sendStatusUpdate(int outsocket, destinationlist_t *remotenodes, anaxjob_t *current_job, int whoami);
int sendUIUpdate(int outsocket, anaxjob_t *current_job, uint8_t status);
int queryForMapFrameLocal(anaxjob_t *current_job, joblist_t *localjobs);
int queryForMapFrame(anaxjob_t *current_job, destinationlist_t *remotenodes);
int requestMapFrame(anaxjob_t *current_job, destination_t *remote, int index, int request);
int sendMinMax(destinationlist_t *remotenodes, int local_min, int local_max, int whoami);
void *spawnShareThread(void *argt);
void *handleSharing(void *argt);
void *sendMapFrame(void *argt);
int returnPNG(int outsocket, anaxjob_t *job);
void *returnPNGthread(void *argt);
int getJobIndex(destination_t *dest, int index);
int finalizeRemoteJobs(destinationlist_t *remodenodes);
int getTermMessage(int socket);

#endif
