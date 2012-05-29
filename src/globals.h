#ifndef GLOBALS_H
#define GLOBALS_H

#include <pthread.h>

#define ANAX_ERR_INVALID_INVOCATION					-1
#define ANAX_ERR_FILE_DOES_NOT_EXIST				-2
#define ANAX_ERR_NO_MEMORY							-3
#define ANAX_ERR_TIFF_SCANLINE						-4
#define ANAX_ERR_PNG_STRUCT_FAILURE					-5
#define ANAX_ERR_INVALID_COLOR_FILE					-6
#define ANAX_ERR_COULD_NOT_RESOLVE_ADDR				-7
#define ANAX_ERR_COULD_NOT_CONNECT					-8
#define ANAX_ERR_NO_MAP                             -9
#define ANAX_ERR_INVALID_HEADER                     -10

#define ANAX_RELATIVE_COLORS						0
#define ANAX_ABSOLUTE_COLORS						1

#define ANAX_STATE_PENDING							1
#define ANAX_STATE_INPROGRESS						2
#define ANAX_STATE_LOADED                           3
#define ANAX_STATE_RENDERING                        4
#define ANAX_STATE_COMPLETE							5
#define ANAX_STATE_NOJOB							6
#define ANAX_STATE_LOST								7

#define UI_STATE_PENDING                            0
#define UI_STATE_RECEIVING                          1
#define UI_STATE_PROCESSING                         2
#define UI_STATE_LOCALCHK                           3
#define UI_STATE_REMOTECHK                          4
#define UI_STATE_PREPARING                          5
#define UI_STATE_RENDERING                          6
#define UI_STATE_SENDING                            7
#define UI_STATE_COMPLETE                           8

#define ANAX_MAP_NORTH                              1
#define ANAX_MAP_SOUTH                              2
#define ANAX_MAP_WEST                               3
#define ANAX_MAP_EAST                               4
#define ANAX_MAP_NORTHWEST                          5
#define ANAX_MAP_NORTHEAST                          6
#define ANAX_MAP_SOUTHWEST                          7
#define ANAX_MAP_SOUTHEAST                          8

#define PROJ_EQUIRECTANGULAR                        0
#define PROJ_MERCATOR                               1

#define BUFSIZE										1024
#define REMOTE_PORT									"51777"
#define COMM_PORT                                   "51778"
#define MAPFRAME                                    100

pthread_mutex_t ready_mutex;
pthread_cond_t ready_cond;
pthread_mutex_t send_lock;
pthread_mutex_t curses_lock;

struct frame_coords {
    int N_set;
    int S_set;
    int E_set;
    int W_set;
    int NE_set;
    int SE_set;
    int SW_set;
    int NW_set;
    double north_lat;
    double south_lat;
    double mid_lat;
    double east_lon;
    double west_lon;
    double mid_lon;
};
typedef struct frame_coords frame_coords_t;

struct anaxjob {
	char *name;
	char *tmpfile;
    char *outfile;
	int index;
	int status;
	pthread_mutex_t file_mutex;
	double top_lat;
	double bottom_lat;
	double right_lon;
	double left_lon;
	int img_height;
	int img_width;
	frame_coords_t frame_coordinates;
};
typedef struct anaxjob anaxjob_t;

struct joblist {
	int num_jobs;
	anaxjob_t *jobs;
};
typedef struct joblist joblist_t;

struct destination {
	char addr[128];
	int socketfd;
	int status;
	int num_jobs;
	pthread_t thread;
	pthread_mutex_t ready_mutex;
	pthread_cond_t ready_cond;
	int ready;
	int complete;
	anaxjob_t **jobs;
};
typedef struct destination destination_t;

struct destination_file {
	int num_destinations;
	destination_t *destinations;
};
typedef struct destination_file destinationlist_t;

#endif

