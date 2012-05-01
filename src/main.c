#include <stdio.h>
#include <stdlib.h>
#include <tiffio.h>
#include <xtiffio.h>
#include <geotiff.h>
#include <unistd.h>
#include "globals.h"
#include "libanax.h"
#include "distranax.h"

void usage() {
	fprintf(stderr, "Usage: geotiff [-cdloqs] [SRC PATH]\n");
	fprintf(stderr, "    Flags:\n");
	fprintf(stderr, "    -c [FILEPATH]: Apply the color scheme in FILEPATH instead of the default color scheme\n");
    fprintf(stderr, "    -d [FILEPATH]: Run in distributed mode, with FILEPATH containing a list of addresses to other machines\n");
    fprintf(stderr, "    -l : Run in listening mode, waiting for a connection from an instance running in distributed mode\n");
	fprintf(stderr, "    -o [FILEPATH]: Save the output file to FILEPATH\n");
	fprintf(stderr, "    -q : Suppress output to stdout\n");
	fprintf(stderr, "    -s [SCALE]: Scale the output file by a factor of SCALE\n");
}

int main(int argc, char *argv[]) {
	// Argument flags
	int c;
	int cflag = 0;
    int dflag = 0;
	int lflag = 0;
	int oflag = 0;
	int qflag = 0;
	int sflag = 0;
	char *srcfile = calloc(FILENAME_MAX, sizeof(char));
	char *outfile = NULL;
	char *colorfile = NULL;
	char *addrfile = NULL;
	double scale = 1.0;

	int err;

	while((c = getopt(argc, argv, "c:d:lo:qs:")) != -1) {
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
			case 'q':
				qflag = 1;
				break;
			case 's':
				sflag = 1;
				scale = atof(optarg);
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
	joblist->num_jobs = 0;
	for(int i = optind; i < argc; i++) {
		joblist->num_jobs++;
		joblist->jobs = realloc(joblist->jobs, joblist->num_jobs * sizeof(anaxjob_t));
		joblist->jobs[joblist->num_jobs - 1].name = argv[i];
		joblist->jobs[joblist->num_jobs - 1].index = i;
		joblist->jobs[joblist->num_jobs - 1].status = ANAX_STATE_PENDING;
		joblist->jobs[joblist->num_jobs - 1].thread = NULL;
	}
	if(joblist->num_jobs == 0 && !lflag) {
		fprintf(stderr, "Error: Source file(s) must be specified\n");
		usage();
		exit(ANAX_ERR_INVALID_INVOCATION);
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
		if(!qflag)
			printf("%s\n", outfile);
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
		    err = connectToRemoteHost(&(destinationlist->destinations[i]));
		    destinationlist->destinations[i].status = ANAX_STATE_NOJOB;
		}
		
		// Initialize color scheme
        colorscheme_t *colorscheme;
        if(cflag) {
            loadColorScheme(NULL, &colorscheme, colorfile);
        } else {
            setDefaultColors(NULL, &colorscheme, ANAX_RELATIVE_COLORS);
        }
        
        // Break if the color scheme is relative (this does not make sense when dealing with multiple files)
		if(colorscheme->isAbsolute == ANAX_RELATIVE_COLORS) {
		    fprintf(stderr, "Error: Distributed rendering is only possible with an absolute color scheme\n");
		    exit(ANAX_ERR_INVALID_COLOR_FILE);
		}
		
		// Send out initial jobs
		err = distributeJobs(destinationlist, joblist, colorscheme, scale);
		
		// Send out later jobs as remote nodes free up
		int completed = 0;
		while(completed < joblist->num_jobs) {
    		pthread_mutex_lock(&ready_mutex);
	    	while(countComplete(joblist) == completed) {
		        pthread_cond_wait(&ready_cond, &ready_mutex);
		    }
		    completed = countComplete(joblist);
		    pthread_mutex_unlock(&ready_mutex);
		    
		    err = distributeJobs(destinationlist, joblist, colorscheme, scale);
		}
		
		pthread_mutex_destroy(&ready_mutex);
		pthread_cond_destroy(&ready_cond);
		
		// Wait for all threads to terminate
		for(int i = 0; i < joblist->num_jobs; i++) {
		    pthread_join(joblist->jobs[i].thread, NULL);
		}
		
    } else if(lflag) {
        // Handle receipt of distributed rendering job
        
        // Network setup
        int socketfd, outsocketfd;
        err = initRemoteListener(&socketfd);
        
        // Receive header data
        struct sockaddr_in clientAddr;
        socklen_t sinSize = sizeof(struct sockaddr_in);
        outsocketfd = accept(socketfd, (struct sockaddr *)&clientAddr, &sinSize);
        
        while(1) {
            char *filename;
            colorscheme_t *colorscheme;
            double scale;
            getHeaderData(outsocketfd, &filename, &colorscheme, &scale);
            
            // Get image data
            if(strstr(filename, "http://") == NULL) {
                // Local file, must request transfer from originating node
                getImageFromPrimary(outsocketfd, filename);
            } else {
                // Remote file, must be downloaded
                downloadImage(filename);
            }
    
            // Open TIFF file
            char *filename_without_path = strrchr(filename, '/');
            filename_without_path = (filename_without_path == NULL) ? 0 : filename_without_path + 1;
            char srcfile[strlen(filename_without_path) + 6];
            sprintf(srcfile, "/tmp/%s", filename_without_path);
            TIFF *srctiff = XTIFFOpen(srcfile, "r");
            if(srctiff == NULL) {
                fprintf(stderr, "Error: No such file: %s\n", srcfile);
                exit(ANAX_ERR_FILE_DOES_NOT_EXIST);
            }
            char outfile[strlen(srcfile) + 1];
            strcpy(outfile, srcfile);
            outfile[strlen(srcfile)] = 0;
            outfile[strlen(srcfile) - 1] = 'g';
            outfile[strlen(srcfile) - 2] = 'n';
            outfile[strlen(srcfile) - 3] = 'p';
    
            // Load data from GeoTIFF
            geotiffmap_t *map;
            err = initMap(&map, srctiff, srcfile, 0);
            if(err)
                exit(err);
            
            // Get corners
            double top, bottom, left, right;
            getCorners(map, &top, &bottom, &left, &right);
            sendCorners(outsocketfd, top, bottom, left, right);
            
            // Scale
            if(scale != 1.0)
                scaleImage(&map, scale);
            
            // Colorize
            colorize(map, colorscheme);
            
            // Close TIFF
            XTIFFClose(srctiff);
            
            // Render PNG
            renderPNG(map, outfile, 0);
            
            // Return PNG to primary node
            returnPNG(outsocketfd, outfile);
        }        
	} else {
	    // Handle local rendering
	    for(int i = 0; i < joblist->num_jobs; i++) {

            ////// The following code is temporary
            ////// Remove it once proper stitching is implemented
            char cwd[FILENAME_MAX];
            getcwd(cwd, FILENAME_MAX);
            sprintf(outfile, "%s/out%i.png", cwd, i);
            strcpy(srcfile, joblist->jobs[i].name);
            ////// End temporary code
    
            // Open TIFF file
            TIFF *srctiff = XTIFFOpen(srcfile, "r");
            if(srctiff == NULL) {
                fprintf(stderr, "Error: No such file: %s\n", srcfile);
                exit(ANAX_ERR_FILE_DOES_NOT_EXIST);
            }
    
            // Load data from GeoTIFF
            geotiffmap_t *map;
            err = initMap(&map, srctiff, srcfile, qflag);
            if(err)
                exit(err);
            
            // Get corners
            getCorners(map, &(joblist->jobs[i].top_lat), &(joblist->jobs[i].bottom_lat), &(joblist->jobs[i].left_lon), &(joblist->jobs[i].right_lon));
    
            // Scale
            if(scale != 1.0) {
                if(scale > 1.0) {
                    printf("Scale must be between 0 and 1.\n");
                } else {
                    scaleImage(&map, scale);
                }
            }
    
            // Init color scheme
            colorscheme_t *colorscheme;
            if(cflag) {
                loadColorScheme(map, &colorscheme, colorfile);
            } else {
                setDefaultColors(map, &colorscheme, ANAX_RELATIVE_COLORS);
            }
            colorize(map, colorscheme);
    
            // Close TIFF
            XTIFFClose(srctiff);
    
            // Render PNG
            renderPNG(map, outfile, qflag);
        }
	}

	return 0;
}
