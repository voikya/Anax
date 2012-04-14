#ifndef LIBANAX_H
#define LIBANAX_H

#include <png.h>
#include <stdint.h>
#include <tiffio.h>

struct color {
	int r;
	int g;
	int b;
	double a;
};
typedef struct color rgb_t;

struct coordinate {
	int degree;
	int minute;
	int second;
};
typedef struct coordinate coord_t;

struct point {
	int16_t elevation;
	coord_t lattitude;
	coord_t longitude;
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

int initMap(geotiffmap_t **map, TIFF *tiff, char *srcfile);
void printGeotiffInfo(geotiffmap_t *map, TIFF *tiff);
int setDefaultColors(geotiffmap_t *map, colorscheme_t **colorscheme, int isAbsolute);
int colorize(geotiffmap_t *map, colorscheme_t *colorscheme);
int renderPNG(geotiffmap_t *map, char *outfile);
//void updatePNGWriteStatus(png_structp png_ptr, png_uint32 row, int pass);

#endif
