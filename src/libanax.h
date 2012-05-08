#ifndef LIBANAX_H
#define LIBANAX_H

#include <png.h>
#include <stdint.h>
#include <tiffio.h>
#include "globals.h"

struct color {
	int r;
	int g;
	int b;
	double a;
};
typedef struct color rgb_t;

struct point {
	int16_t elevation;
	double latitude;
	double longitude;
	rgb_t color;
};
typedef struct point point_t;

struct geotiff_map {
	char *name;
	int height;
	int width;
	int16_t max_elevation;
	int16_t min_elevation;
	point_t **data;
};
typedef struct geotiff_map geotiffmap_t;

struct colorstop {
	int16_t elevation;
	rgb_t color;
};
typedef struct colorstop colorstop_t;

struct colorscheme {
	int isAbsolute;
	int num_stops;
	colorstop_t *colors;
};
typedef struct colorscheme colorscheme_t;

int initMap(geotiffmap_t **map, TIFF *tiff, char *srcfile, int suppress_output, frame_coords_t *frame);
void printGeotiffInfo(geotiffmap_t *map, TIFF *tiff);
int setDefaultColors(geotiffmap_t *map, colorscheme_t **colorscheme, int isAbsolute);
int loadColorScheme(geotiffmap_t *map, colorscheme_t **colorscheme, char *colorfile);
int setRelativeElevations(colorscheme_t *colorscheme, int16_t max, int16_t min);
int colorize(geotiffmap_t *map, colorscheme_t *colorscheme);
int renderPNG(geotiffmap_t *map, char *outfile, int suppress_output);
//void updatePNGWriteStatus(png_structp png_ptr, png_uint32 row, int pass);
int scaleImage(geotiffmap_t **map, double scale);
int getCorners(geotiffmap_t *map, double *top, double *bottom, double *left, double *right);
int writeMapData(anaxjob_t *current_job, geotiffmap_t *map);
int readMapData(anaxjob_t *current_job, geotiffmap_t **map);
void freeMap(geotiffmap_t *map);

void SHOW_DATA_AT_POINT(geotiffmap_t *map, int r, int c);
void SHOW_COLOR_SCHEME(colorscheme_t *colors);

#endif
