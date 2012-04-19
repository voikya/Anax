#include <stdio.h>
#include <stdlib.h>
#include <tiffio.h>
#include <geotiff.h>
#include <unistd.h>
#include "globals.h"
#include "libanax.h"

void usage() {
	fprintf(stderr, "Usage: geotiff [-os] [SRC PATH]\n");
	fprintf(stderr, "    Flags:\n");
	fprintf(stderr, "    -c [FILEPATH]: Apply the color scheme in FILEPATH instead of the default color scheme\n");
	fprintf(stderr, "    -o [FILEPATH]: Save the output file to FILEPATH\n");
	fprintf(stderr, "    -s [SCALE]: Scale the output file by a factor of SCALE\n");
}

int main(int argc, char *argv[]) {
	// Argument flags
	int c;
	int cflag = 0;
	int oflag = 0;
	int sflag = 0;
	char *srcfile = NULL;
	char *outfile = NULL;
	char *colorfile = NULL;
	double scale = 1.0;

	int err;

	while((c = getopt(argc, argv, "c:o:s:")) != -1) {
		switch(c) {
			case 'c':
				cflag = 1;
				colorfile = optarg;
				break;
			case 'o':
				oflag = 1;
				outfile = optarg;
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
	if(outfile == NULL) {
		char cwd[FILENAME_MAX];
		getcwd(cwd, FILENAME_MAX);
		outfile = calloc(FILENAME_MAX, sizeof(char));
		sprintf(outfile, "%s%s", cwd, "/out.png");
		printf("%s\n", outfile);
	}

	// Open TIFF file
	TIFF *srctiff = TIFFOpen(srcfile, "r");
	if(srctiff == NULL) {
		fprintf(stderr, "Error: No such file: %s\n", srcfile);
		exit(ANAX_ERR_FILE_DOES_NOT_EXIST);
	}

	// Load data from GeoTIFF
	geotiffmap_t *map;
	err = initMap(&map, srctiff, srcfile);
	if(err)
		exit(err);

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
	TIFFClose(srctiff);

	// Render PNG
	renderPNG(map, outfile);

	return 0;
}
