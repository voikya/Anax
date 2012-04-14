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
	fprintf(stderr, "    -o [FILEPATH]: Save the output file to FILEPATH\n");
	fprintf(stderr, "    -s [SCALE]: Scale the output file by a factor of SCALE\n");
}

int main(int argc, char *argv[]) {
	// Argument flags
	int c;
	int oflag = 0;
	int sflag = 0;
	char *srcfile = NULL;
	char *outfile = NULL;
	double scale;

	int err;

	while((c = getopt(argc, argv, "o:s:")) != -1) {
		switch(c) {
			case 'o':
				oflag = 1;
				outfile = optarg;
				printf("Outfile set to: %s\n", outfile);
				break;
			case 's':
				sflag = 1;
				scale = atof(optarg);
				printf("Scale set to: %f\n", scale);
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

	// Init color scheme
	colorscheme_t *colorscheme;
	setDefaultColors(map, &colorscheme, ANAX_RELATIVE_COLORS);
	colorize(map, colorscheme);

	// Close TIFF
	TIFFClose(srctiff);

	// Render PNG
	renderPNG(map, outfile);

	return 0;
}
