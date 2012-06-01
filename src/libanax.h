#ifndef LIBANAX_H
#define LIBANAX_H

#include <float.h>
#include <png.h>
#include <stdint.h>
#include <tiffio.h>
#include "globals.h"
#include "anaxcurses.h"

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
	uint8_t isWater;
	uint8_t relief;
	rgb_t color;
};
typedef struct point point_t;

struct geotiff_map {
	char *name;
	int height;
	int width;
	double vertical_pixel_scale;
	double horizontal_pixel_scale;
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
	int showWater;
	int num_stops;
	colorstop_t *colors;
	colorstop_t water;
};
typedef struct colorscheme colorscheme_t;

struct tile {
    char *name;
    int img_height;
    int img_width;
    int is_open;

    // Coordinate values
    double north;
    double south;
    double east;
    double west;

    // Pixel coordinates in final combined image
    int top_row;
    int bottom_row;
    int left_col;
    int right_col;
};
typedef struct tile tile_t;

struct tile_list {
    int num_tiles;
    tile_t *tiles;
    double north_lim;
    double south_lim;
    double east_lim;
    double west_lim;
    pthread_mutex_t lock;
};
typedef struct tile_list tilelist_t;

struct tile_ref {
    tile_t *tile;
    FILE *fp;
    png_structp png_ptr;
    png_infop info_ptr;
    png_infop end_info;
    int width;
};
typedef struct tile_ref tile_ref_t;

struct tile_row {
    int num_tiles;
    int height;
    tile_ref_t *refs;
};
typedef struct tile_row tile_subset_t;

int initMap(geotiffmap_t **map, TIFF *tiff, char *srcfile, int suppress_output, frame_coords_t *frame);
void printGeotiffInfo(geotiffmap_t *map, TIFF *tiff);
int setDefaultColors(geotiffmap_t *map, colorscheme_t **colorscheme, int isAbsolute);
int loadColorScheme(geotiffmap_t *map, colorscheme_t **colorscheme, char *colorfile, int wflag);
int setRelativeElevations(colorscheme_t *colorscheme, int16_t max, int16_t min);
int findWater(geotiffmap_t *map);
int applyProjection(geotiffmap_t **map, int projection);
int colorize(geotiffmap_t *map, colorscheme_t *colorscheme);
int reliefshade(geotiffmap_t *map, int direction);
int renderPNG(geotiffmap_t *map, char *outfile, int suppress_output);
//void updatePNGWriteStatus(png_structp png_ptr, png_uint32 row, int pass);
int scaleImage(geotiffmap_t **map, double scale);
int getCorners(geotiffmap_t *map, double *top, double *bottom, double *left, double *right);
int writeMapData(anaxjob_t *current_job, geotiffmap_t *map);
int readMapData(anaxjob_t *current_job, geotiffmap_t **map);
void freeMap(geotiffmap_t *map);
int finalizeLocalJobs(joblist_t *joblist);
int stitch(tilelist_t *tilelist, char *outfile, uilist_t *uilist);
int getTileRowSubset(tilelist_t *tilelist, int row, int img_width, tile_subset_t **tile_subset);
int loadRowData(png_byte *row_ptr, tile_subset_t *tile_subset, int img_width);

void SHOW_DATA_AT_POINT(geotiffmap_t *map, int r, int c);
void SHOW_COLOR_SCHEME(colorscheme_t *colors);

#endif
