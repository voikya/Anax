#include <stdio.h>
#include <stdlib.h>
#include <tiffio.h>
#include <xtiffio.h>
#include <geotiff.h>
#include <unistd.h>
#include "globals.h"
#include "libanax.h"
#include "distranax.h"
#include "anaxcurses.h"

void usage() {
	fprintf(stderr, "Usage: geotiff [-cdloqrsw] [SRC PATH]\n");
	fprintf(stderr, "    Flags:\n");
	fprintf(stderr, "    -c [FILEPATH]: Apply the color scheme in FILEPATH instead of the default color scheme\n");
    fprintf(stderr, "    -d [FILEPATH]: Run in distributed mode, with FILEPATH containing a list of addresses to other machines\n");
    fprintf(stderr, "    -l : Run in listening mode, waiting for a connection from an instance running in distributed mode\n");
	fprintf(stderr, "    -o [FILEPATH]: Save the output file to FILEPATH\n");
	fprintf(stderr, "    -p [PROJECTION]: Use projection PROJECTION. Options are EQUIRECTANGULAR, MERCATOR. Default is EQUIRECTANGULAR\n");
	fprintf(stderr, "    -q : Suppress output to stdout\n");
	fprintf(stderr, "    -r [SOURCE]: Draw relief shading using light originating in the direction of SOURCE (one of N, S, E, W, NE, SE, NW, SW)\n");
	fprintf(stderr, "    -s [SCALE]: Scale the output file by a factor of SCALE\n");
	fprintf(stderr, "    -w : Try to identify bodies of water\n");
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);

	// Argument flags
	int c;
	int cflag = 0;
    int dflag = 0;
	int lflag = 0;
	int oflag = 0;
	int pflag = 0;
	int qflag = 0;
	int sflag = 0;
	int rflag = 0;
	int wflag = 0;
	char *outfile = NULL;
	char *colorfile = NULL;
	char *addrfile = NULL;
	double scale = 1.0;
	int relief = 0;
	int projection = 0;

	int err;

	while((c = getopt(argc, argv, "c:d:lo:p:qr:s:w")) != -1) {
		switch(c) {
			case 'c':
				cflag = 1;
				colorfile = optarg;
				break;
			case 'd':
				dflag = 1;
				addrfile = optarg;
				break;
			case 'l':
				lflag = 1;
				break;
			case 'o':
				oflag = 1;
				outfile = optarg;
				break;
			case 'p':
			    pflag = 1;
			    if(!strcmp(optarg, "EQUIRECTANGULAR"))
			        projection = PROJ_EQUIRECTANGULAR;
			    else if(!strcmp(optarg, "MERCATOR"))
			        projection = PROJ_MERCATOR;
			    else {
			        fprintf(stderr, "Error: %s is not a recognized projection\n", optarg);
			        usage();
			        exit(ANAX_ERR_INVALID_INVOCATION);
			    }
			    break;
			case 'q':
				qflag = 1;
				break;
			case 'r':
			    rflag = 1;
			    if(!strcmp(optarg, "N"))
			        relief = ANAX_MAP_NORTH;
			    else if(!strcmp(optarg, "S"))
			        relief = ANAX_MAP_SOUTH;
			    else if(!strcmp(optarg, "E"))
			        relief = ANAX_MAP_EAST;
			    else if(!strcmp(optarg, "W"))
			        relief = ANAX_MAP_WEST;
			    else if(!strcmp(optarg, "NE"))
			        relief = ANAX_MAP_NORTHEAST;
			    else if(!strcmp(optarg, "SE"))
			        relief = ANAX_MAP_SOUTHEAST;
			    else if(!strcmp(optarg, "SW"))
			        relief = ANAX_MAP_SOUTHWEST;
			    else if(!strcmp(optarg, "NW"))
			        relief = ANAX_MAP_NORTHWEST;
			    else {
			        fprintf(stderr, "Error: %s is not a valid argument to -w\n", optarg);
			        usage();
			        exit(ANAX_ERR_INVALID_INVOCATION);
			    }
			    break;
			case 's':
				sflag = 1;
				scale = atof(optarg);
				break;
			case 'w':
			    wflag = 1;
			    break;
			case ':':
				fprintf(stderr, "Error: Flag is missing argument\n");
				usage();
				exit(ANAX_ERR_INVALID_INVOCATION);
			default:
				fprintf(stderr, "Error: Unrecognized flag\n");
				usage();
				exit(ANAX_ERR_INVALID_INVOCATION);
				break;
		}
	}

	joblist_t *joblist = malloc(sizeof(joblist_t));
	joblist->jobs = NULL;
	joblist->num_jobs = 0;
	int jindex = 0;
	for(int i = optind; i < argc; i++) {
		joblist->num_jobs++;
		joblist->jobs = realloc(joblist->jobs, joblist->num_jobs * sizeof(anaxjob_t));
		memset(&(joblist->jobs[i]), 0, sizeof(anaxjob_t));
		joblist->jobs[joblist->num_jobs - 1].name = calloc(strlen(argv[i] + 1), sizeof(char));
		strcpy(joblist->jobs[joblist->num_jobs - 1].name, argv[i]);
		joblist->jobs[joblist->num_jobs - 1].tmpfile = NULL;
		joblist->jobs[joblist->num_jobs - 1].outfile = NULL;
		joblist->jobs[joblist->num_jobs - 1].index = jindex++;
		joblist->jobs[joblist->num_jobs - 1].status = ANAX_STATE_PENDING;
	}
	if(joblist->num_jobs == 0 && !lflag) {
		fprintf(stderr, "Error: Source file(s) must be specified\n");
		usage();
		exit(ANAX_ERR_INVALID_INVOCATION);
	}

    uilist_t *uilist = NULL;
	if(!qflag && !lflag) {
	    initUIList(&uilist, joblist, outfile);
	    initWindows(uilist);
	}

/*
	for(int i = optind; i < argc; i++) {
		if(srcfile != NULL) {
			usage();
			exit(ANAX_ERR_INVALID_INVOCATION);
		}
		srcfile = argv[i];
	}
	if(srcfile == NULL) {
		fprintf(stderr, "Error: Source file must be specified\n");
		usage();
		exit(ANAX_ERR_INVALID_INVOCATION);
	}
*/

	if(outfile == NULL) {
		char cwd[FILENAME_MAX];
		getcwd(cwd, FILENAME_MAX);
		outfile = calloc(FILENAME_MAX, sizeof(char));
		sprintf(outfile, "%s%s", cwd, "/out.png");
	}

	if(dflag) {
	    // Handle distributed rendering
	    
	    // Set up locks
	    pthread_mutex_init(&ready_mutex, NULL);
	    pthread_cond_init(&ready_cond, NULL);
	    
	    // Load the destinations array
	    destinationlist_t *destinationlist;
	    err = loadDestinationList(addrfile, &destinationlist);
	    
	    // Initialize connections to each remote host
	    for(int i = 0; i < destinationlist->num_destinations; i++) {
	        err = connectToRemoteHost(&(destinationlist->destinations[i]), REMOTE_PORT);
	    }
	    
	    // Initialize color scheme
	    colorscheme_t *colorscheme;
	    if(cflag) {
	        loadColorScheme(NULL, &colorscheme, colorfile, wflag);
	    } else {
	        setDefaultColors(NULL, &colorscheme, ANAX_RELATIVE_COLORS);
	    }
	    
	    // Initialize the tile list for receiving incoming renders
	    tilelist_t *tilelist = malloc(sizeof(tilelist_t));
	    tilelist->num_tiles = 0;
	    tilelist->tiles = NULL;
	    tilelist->north_lim = -DBL_MAX;
	    tilelist->south_lim = DBL_MAX;
	    tilelist->east_lim = -DBL_MAX;
	    tilelist->west_lim = DBL_MAX;
	    pthread_mutex_init(&(tilelist->lock), NULL);
	    
	    // Send each remote node the colorscheme, scale, and remote node list
	    err = initRemoteHosts(destinationlist, tilelist, colorscheme, scale, relief, projection, uilist);
	    
	    // Send out initial jobs
	    err = distributeJobs(destinationlist, joblist);
	    
		// Send out later jobs as remote nodes free up
		while(tilelist->num_tiles < joblist->num_jobs) {
    		pthread_mutex_lock(&ready_mutex);
		    pthread_cond_wait(&ready_cond, &ready_mutex);
		    pthread_mutex_unlock(&ready_mutex);
		    
		    err = distributeJobs(destinationlist, joblist);
		}
		
		// Wait for all threads to join
		for(int i = 0; i < destinationlist->num_destinations; i++) {
		    pthread_join(destinationlist->destinations[i].thread, NULL);
		}
		
		pthread_mutex_destroy(&ready_mutex);
		pthread_cond_destroy(&ready_cond);
		
		// Clean up local and remote memory, and terminate remote processes
		finalizeRemoteJobs(destinationlist);
		finalizeLocalJobs(joblist);
		
        // Stitch together the received images
        stitch(tilelist, outfile, uilist);
		
    } else if(lflag) {
        // Handle receipt of distributed rendering job
        
        qflag = 0;
        
        // Network setup
        int socketfd, outsocketfd;
        err = initRemoteListener(&socketfd, REMOTE_PORT);
        struct sockaddr_in clientAddr;
        socklen_t sinSize = sizeof(struct sockaddr_in);
        outsocketfd = accept(socketfd, (struct sockaddr *)&clientAddr, &sinSize);  
	    pthread_mutex_init(&send_lock, NULL);
        
        // Receive and set up colorscheme and scale
        int whoami;
        int local_max = INT16_MIN;
        int local_min = INT16_MAX;
        int global_max = INT16_MIN;
        int global_min = INT16_MAX;
        colorscheme_t *colorscheme;
        double scale;
        int relief, projection;
        getInitHeaderData(outsocketfd, &whoami, &colorscheme, &scale, &relief, &projection);
        
        SHOW_COLOR_SCHEME(colorscheme);
        
        // Receive and set up a list of all remote nodes
        destinationlist_t *remotenodes;
        getNodesHeaderData(outsocketfd, &remotenodes);

        // Set up a list for local jobs
        joblist_t *localjobs = malloc(sizeof(joblist_t));
        localjobs->num_jobs = 0;
        localjobs->jobs = NULL;

        // Set up data exchange thread spawner
        threadshare_t *argt = malloc(sizeof(threadshare_t));
        argt->remotenodes = remotenodes;
        argt->localjobs = localjobs;
        argt->global_max = &global_max;
        argt->global_min = &global_min;
        argt->whoami = whoami;
        argt->socket = -1;
        pthread_t sharethread;
        pthread_create(&sharethread, NULL, spawnShareThread, argt);
        
        // Download and process GeoTIFF files
        while(1) {
            // Create a local copy of the GeoTIFF
            err = getGeoTIFF(outsocketfd, localjobs);
            if(err == ANAX_ERR_INVALID_HEADER)
                continue;
            if(err == ANAX_ERR_NO_MAP)
                break;
            anaxjob_t *current_job = &(localjobs->jobs[localjobs->num_jobs - 1]);
            sendUIUpdate(outsocketfd, current_job, UI_STATE_PROCESSING);
            
            // Open the file
            TIFF *srctiff = XTIFFOpen(current_job->outfile, "r");
            if(srctiff == NULL) {
                fprintf(stderr, "Error: No such file: %s\n", current_job->outfile);
                exit(ANAX_ERR_FILE_DOES_NOT_EXIST);
            }
            
            // Set the new name for the outfile (PNG) and tempfile (TMP)
            current_job->outfile = realloc(current_job->outfile, 32);
            current_job->tmpfile = malloc(32);
            sprintf(current_job->tmpfile, "/tmp/map%i.tmp", current_job->index);
            sprintf(current_job->outfile, "/tmp/map%i.png", current_job->index);
            
            // Load data from GeoTIFF
            geotiffmap_t *map;
            frame_coords_t *frame = malloc(sizeof(frame_coords_t));
            err = initMap(&map, srctiff, current_job->name, 0, frame);
            if(err)
                exit(err);
            XTIFFClose(srctiff);
            
            // Store frame coordinates (to be used when requesting frame data from other nodes)
            memcpy(&(current_job->frame_coordinates), frame, sizeof(frame_coords_t));
            
            // Get periphery
            getCorners(map, &(current_job->top_lat), &(current_job->bottom_lat), &(current_job->left_lon), &(current_job->right_lon));

            // Change projections
            if(projection) {
                printf("  Applying new projection\n");
                applyProjection(&map, projection);
            }

            // Set LOADED status and alert other nodes
            current_job->status = ANAX_STATE_LOADED;
            sendStatusUpdate(outsocketfd, remotenodes, current_job, whoami);
            
            // Update local elevation extreme variables
            local_max = (map->max_elevation > local_max) ? map->max_elevation : local_max;
            local_min = (map->min_elevation < local_min) ? map->min_elevation : local_min;
            
            // Write the map data to a temporary file
            writeMapData(current_job, map);
            
            // Free the map
            freeMap(map);
        }
        
        // Alert all other nodes that this node has received all files
        sendStatusUpdate(outsocketfd, remotenodes, NULL, whoami);
        
        // If the colorscheme is relative, alert other nodes of this node's min and max
        if(colorscheme->isAbsolute == ANAX_RELATIVE_COLORS) {
            sendMinMax(remotenodes, local_min, local_max, whoami);
        }
        
        // Check for neighboring images amongst local tiles
        printf("Performing local map query...\n");
        for(int i = 0; i < localjobs->num_jobs; i++) {
            printf("... Examining job %i of %i\n", i + 1, localjobs->num_jobs);
            queryForMapFrameLocal(&(localjobs->jobs[i]), localjobs);
            sendUIUpdate(outsocketfd, &(localjobs->jobs[i]), UI_STATE_REMOTECHK);
        }
        
        // Query other nodes for frame information
        printf("Performing remote map query...\n");
        int done = 0;
        while(!done) {
            for(int i = 0; i < localjobs->num_jobs; i++) {
                if(localjobs->jobs[i].status == ANAX_STATE_LOADED) {
                    printf("Querying job %i of %i\n", i + 1, localjobs->num_jobs);
                    queryForMapFrame(&(localjobs->jobs[i]), remotenodes);
                }
                
                // Check if all remote jobs have received all the jobs they are going to get
                // at the start of this loop; if so, break out of the while loop at the
                // conclusion of this for loop
                if(i == 0) {
                    int count = 0;
                    for(int c = 0; c < remotenodes->num_destinations; c++) {
                        count = (remotenodes->destinations[c].status == ANAX_STATE_RENDERING) ? count + 1 : count;
                    }
                    done = (count == remotenodes->num_destinations - 1) ? 1 : 0;
                    printf("Done: %i\n", done);
                }
            }
            
            printf("Sleeping\n");
            sleep(2);
        }

        // Set any remaining jobs' status to rendering
        for(int i = 0; i < localjobs->num_jobs; i++) {
            if(localjobs->jobs[i].status == ANAX_STATE_LOADED && 
               localjobs->jobs[i].frame_coordinates.N_set != 2 &&
               localjobs->jobs[i].frame_coordinates.S_set != 2 &&
               localjobs->jobs[i].frame_coordinates.E_set != 2 &&
               localjobs->jobs[i].frame_coordinates.W_set != 2 &&
               localjobs->jobs[i].frame_coordinates.NE_set != 2 &&
               localjobs->jobs[i].frame_coordinates.SE_set != 2 &&
               localjobs->jobs[i].frame_coordinates.SW_set != 2 &&
               localjobs->jobs[i].frame_coordinates.NW_set != 2)
               localjobs->jobs[i].status = ANAX_STATE_RENDERING;
        }
        printf("All data is ready. Proceeding to rendering phase.\n");
        
        // If the colorscheme is relative, update it with the appropriate scale
        if(colorscheme->isAbsolute == ANAX_RELATIVE_COLORS) {
            printf(">>> Local Max: %i / Global Max: %i / New Global Max: %i", local_max, global_max, (local_max > global_max) ? local_max : global_max);
            global_max = (local_max > global_max) ? local_max : global_max;
            printf(">>> Local Min: %i / Global Min: %i / New Global Min: %i", local_min, global_min, (local_min < global_min) ? local_min : global_min);
            global_min = (local_min < global_min) ? local_min : global_min;
            setRelativeElevations(colorscheme, global_max, global_min);
        }
        
        // Render all local maps
        // (skipping temporarily if a map is still waiting on a frame response from another node)
        int rendered = 0;
        while(rendered < localjobs->num_jobs) {
            for(int i = 0; i < localjobs->num_jobs; i++) {
                anaxjob_t *current_job = &(localjobs->jobs[i]);
                printf("Sleeping: Status of job %i: %i [%i %i %i %i %i %i %i %i]\n", i, current_job->status, current_job->frame_coordinates.N_set, current_job->frame_coordinates.S_set, current_job->frame_coordinates.E_set, current_job->frame_coordinates.W_set, current_job->frame_coordinates.NE_set, current_job->frame_coordinates.SE_set, current_job->frame_coordinates.NW_set, current_job->frame_coordinates.SW_set);
                if(current_job->status == ANAX_STATE_RENDERING) {
                    sendUIUpdate(outsocketfd, current_job, UI_STATE_PREPARING);
                
                    printf("Rendering map %i\n", i);
                    
                    // Load the map
                    printf("  Loading\n");
                    geotiffmap_t *map;
                    readMapData(current_job, &map);
                    
                    // Find water
                    if(colorscheme->showWater) {
                        printf("  Identifying water\n");
                        findWater(map);
                    }
                    
                    // Apply relief shading
                    if(relief) {
                        printf("  Applying relief shading\n");
                        reliefshade(map, relief);
                    }
                    
                    // Scale
                    if(scale != 1.0) {
                        printf("  Scaling\n");
                        scaleImage(&map, scale);
                    }
                    
                    // Colorize
                    printf("  Colorizing\n");
                    colorize(map, colorscheme);
                    
                    // Render
                    sendUIUpdate(outsocketfd, current_job, UI_STATE_RENDERING);
                    renderPNG(map, current_job->outfile, 0);
                
                    // Update local and remote state
                    rendered++;
                    current_job->status = ANAX_STATE_COMPLETE;
                    sendStatusUpdate(outsocketfd, remotenodes, current_job, whoami);
                    
                    // Get final image dimensions
                    current_job->img_height = map->height;
                    current_job->img_width = map->width;
                    
                    // Free the map
                    freeMap(map);
                    
                    // Transmit the rendered image home
                    sendUIUpdate(outsocketfd, current_job, UI_STATE_SENDING);
                    returnPNG(outsocketfd, current_job);
                    
                } else if(localjobs->jobs[i].status == ANAX_STATE_LOADED && 
                           current_job->frame_coordinates.N_set != 2 &&
                           current_job->frame_coordinates.S_set != 2 &&
                           current_job->frame_coordinates.E_set != 2 &&
                           current_job->frame_coordinates.W_set != 2 &&
                           current_job->frame_coordinates.NE_set != 2 &&
                           current_job->frame_coordinates.SE_set != 2 &&
                           current_job->frame_coordinates.SW_set != 2 &&
                           current_job->frame_coordinates.NW_set != 2) {
                    current_job->status = ANAX_STATE_RENDERING;
                }
            }
            sleep(2);
        }
        
        printf("Rendering complete\n");
        
        // Wait for a termination message
        getTermMessage(outsocketfd);
        
        // Free memory
        close(outsocketfd);
        finalizeLocalJobs(localjobs);
        for(int i = 0; i < remotenodes->num_destinations; i++) {
            if(i != whoami) {
                close(remotenodes->destinations[i].socketfd);
                free(remotenodes->destinations[i].jobs);
            }
        }
        free(remotenodes->destinations);
        free(remotenodes);
		
	} else {
	    // Handle local rendering
	    
	    int local_max = INT16_MIN;
        int local_min = INT16_MAX;
        
        // Initialize color scheme
	    colorscheme_t *colorscheme;
	    if(cflag) {
	        loadColorScheme(NULL, &colorscheme, colorfile, wflag);
	    } else {
	        setDefaultColors(NULL, &colorscheme, ANAX_RELATIVE_COLORS);
	    }
	    
	    for(int i = 0; i < joblist->num_jobs; i++) {
	        if(!qflag) {
	            updateJobUIState(&(uilist->jobuis[i]), UI_STATE_RECEIVING);
	            updateJobView(&(uilist->jobuis[i]));
	        }
	    
	        // Open TIFF file
	        TIFF *srctiff = XTIFFOpen(joblist->jobs[i].name, "r");
	        if(srctiff == NULL) {
	            fprintf(stderr, "Error: No such file: %s\n", joblist->jobs[i].name);
	            exit(ANAX_ERR_FILE_DOES_NOT_EXIST);
	        }
	        
	        // Set the new name for the outfile (PNG) and tempfile (TMP)
	        joblist->jobs[i].outfile = malloc(32);
	        joblist->jobs[i].tmpfile = malloc(32);
	        sprintf(joblist->jobs[i].tmpfile, "/tmp/map%i.tmp", i);
	        sprintf(joblist->jobs[i].outfile, "/tmp/map%i.png", i);
	        
	        if(!qflag) {
	            updateJobUIState(&(uilist->jobuis[i]), UI_STATE_PROCESSING);
	            updateJobView(&(uilist->jobuis[i]));
	        }
	        
	        // Load data from GeoTIFF
	        geotiffmap_t *map;
	        frame_coords_t *frame = malloc(sizeof(frame_coords_t));
	        err = initMap(&map, srctiff, joblist->jobs[i].name, qflag, frame);
	        if(err)
	            exit(err);
	        XTIFFClose(srctiff);
	        
	        // Store the frame coordinates
	        memcpy(&(joblist->jobs[i].frame_coordinates), frame, sizeof(frame_coords_t));
	        
	        // Get periphery
	        getCorners(map, &(joblist->jobs[i].top_lat), &(joblist->jobs[i].bottom_lat), &(joblist->jobs[i].left_lon), &(joblist->jobs[i].right_lon));
	        
	        // Change projections
	        if(projection)
	            applyProjection(&map, projection);
	        
	        // Update elevation extreme variables
	        local_max = (map->max_elevation > local_max) ? map->max_elevation : local_max;
	        local_min = (map->min_elevation < local_min) ? map->min_elevation : local_min;
	        
	        // Write the map data to a temporary file
	        writeMapData(&(joblist->jobs[i]), map);
	        
	        // Free the map
	        freeMap(map);
	    }
	    
	    // Check for neighboring images amongst local tiles
	    for(int i = 0; i < joblist->num_jobs; i++) {
	        if(!qflag) {
	            updateJobUIState(&(uilist->jobuis[i]), UI_STATE_LOCALCHK);
	            updateJobView(&(uilist->jobuis[i]));
	        }
	        
	        queryForMapFrameLocal(&(joblist->jobs[i]), joblist);
	        
	        if(!qflag) {
	            updateJobUIState(&(uilist->jobuis[i]), UI_STATE_REMOTECHK);
	            updateJobView(&(uilist->jobuis[i]));
	            updateJobUIState(&(uilist->jobuis[i]), UI_STATE_PREPARING);
	            updateJobView(&(uilist->jobuis[i]));
	        }
	    }
	    
	    // If the colorscheme is relative, update it with the appropriate scale
	    if(colorscheme->isAbsolute == ANAX_RELATIVE_COLORS) {
	        setRelativeElevations(colorscheme, local_max, local_min);
	    }
	    
	    // Render all maps
	    for(int i = 0; i < joblist->num_jobs; i++) {
	        // Load the map
	        geotiffmap_t *map;
	        readMapData(&(joblist->jobs[i]), &map);
	        
	        // Find water
	        if(colorscheme->showWater)
	            findWater(map);
	            
	        // Apply relief shading
	        if(relief)
	            reliefshade(map, relief);
	        
	        // Scale
	        if(scale != 1.0)
	            scaleImage(&map, scale);
	        
	        // Colorize
	        colorize(map, colorscheme);
	        
	        // Render
	        if(!qflag) {
	            updateJobUIState(&(uilist->jobuis[i]), UI_STATE_RENDERING);
	            updateJobView(&(uilist->jobuis[i]));
	        }
	        renderPNG(map, joblist->jobs[i].outfile, qflag);
	        
	        // Get final image dimensions
	        joblist->jobs[i].img_height = map->height;
	        joblist->jobs[i].img_width = map->width;
	        
	        // Free the map
	        freeMap(map);

	        if(!qflag) {
	            updateJobUIState(&(uilist->jobuis[i]), UI_STATE_SENDING);
	            updateJobView(&(uilist->jobuis[i]));
	            updateJobUIState(&(uilist->jobuis[i]), UI_STATE_COMPLETE);
	            updateJobView(&(uilist->jobuis[i]));
	        }
	    }
	    
	    // Initialize a tile list
	    tilelist_t *tilelist = malloc(sizeof(tilelist_t));
	    tilelist->num_tiles = joblist->num_jobs;
	    tilelist->tiles = malloc(joblist->num_jobs * sizeof(tile_t));
	    tilelist->north_lim = -DBL_MAX;
	    tilelist->south_lim = DBL_MAX;
	    tilelist->east_lim = -DBL_MAX;
	    tilelist->west_lim = DBL_MAX;

        // Add tiles to the tile list
        for(int i = 0; i < tilelist->num_tiles; i++) {
            tilelist->tiles[i].name = calloc(32, sizeof(char));
            strcpy(tilelist->tiles[i].name, joblist->jobs[i].outfile);
            tilelist->tiles[i].img_height = joblist->jobs[i].img_height;
            tilelist->tiles[i].img_width = joblist->jobs[i].img_width;
            tilelist->tiles[i].is_open = 0;
            tilelist->tiles[i].north = joblist->jobs[i].top_lat;
            tilelist->tiles[i].south = joblist->jobs[i].bottom_lat;
            tilelist->tiles[i].east = joblist->jobs[i].right_lon;
            tilelist->tiles[i].west = joblist->jobs[i].left_lon;
            tilelist->tiles[i].top_row = 0;
            tilelist->tiles[i].bottom_row = 0;
            tilelist->tiles[i].left_col = 0;
            tilelist->tiles[i].right_col = 0;
        }
        
        // Clean up job list
        finalizeLocalJobs(joblist);
        
        // Stitch together the tiles
        stitch(tilelist, outfile, uilist);
	}
	
	if(!qflag) {
	    endWindows();
	}

	return 0;
}
