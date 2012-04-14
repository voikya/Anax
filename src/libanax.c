#include <geotiff.h>
#include <limits.h>
#include <png.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "globals.h"
#include "libanax.h"

int initMap(geotiffmap_t **map, TIFF *tiff, char *srcfile) {
	int err;

	// Allocate main map struct
	*map = malloc(sizeof(geotiffmap_t));

	// Get GeoTIFF file name
	char *name = strrchr(srcfile, '/');
	if(name == NULL) {
		(*map)->name = calloc(strlen(srcfile) + 1, sizeof(char));
		memcpy((*map)->name, srcfile, strlen(srcfile));
	} else {
		(*map)->name = calloc(strlen(name), sizeof(char));
		memcpy((*map)->name, name + 1, strlen(name) - 1);
	}

	// Get dimensions of GeoTIFF file
	TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &((*map)->width));
	TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &((*map)->height));

	// Allocate enough memory for the entire map struct
    // (This is all done all at once to help ensure there won't be any out-of-memory
	// errors after processing has already begun)
	(*map)->data = malloc((*map)->height * sizeof(point_t *));
	for(int i = 0; i < (*map)->height; i++) {
		(*map)->data[i] = malloc((*map)->width * sizeof(point_t));
		if((*map)->data[i] == NULL)
			return ANAX_ERR_NO_MEMORY;	
	}

	// Scan GeoTIFF file for topological information and store it
	int line_byte_size = TIFFScanlineSize(tiff);
	(*map)->max_elevation = INT16_MIN;
	(*map)->min_elevation = INT16_MAX;
	union {
		tdata_t buf[line_byte_size];
		int16_t data[(*map)->width];
	} tiff_line;

	for(int row = 0; row < (*map)->height; row++) {
		err = TIFFReadScanline(tiff, &(tiff_line.buf), row, 0);
		if(err != 1)
			return ANAX_ERR_TIFF_SCANLINE;
		for(int col = 0; col < (*map)->width; col++) {
			(*map)->data[row][col].elevation = tiff_line.data[col];
			if((*map)->data[row][col].elevation > (*map)->max_elevation) {
				(*map)->max_elevation = (*map)->data[row][col].elevation;
			}
			if((*map)->data[row][col].elevation < (*map)->min_elevation) {
				(*map)->min_elevation = (*map)->data[row][col].elevation;
			}
		}


	}


	printGeotiffInfo(*map, tiff);
	return 0;
}

int setDefaultColors(geotiffmap_t *map, colorscheme_t **colorscheme, int isAbsolute) {
	*colorscheme = malloc(sizeof(colorscheme_t));
	(*colorscheme)->isAbsolute = isAbsolute;
	(*colorscheme)->num_stops = 2;
	(*colorscheme)->colors = calloc(4, sizeof(colorstop_t));
	(*colorscheme)->colors[0].elevation = 0;
	(*colorscheme)->colors[0].color.r = 0;
	(*colorscheme)->colors[0].color.g = 0;
	(*colorscheme)->colors[0].color.b = 0;
	(*colorscheme)->colors[0].color.a = 1.0;
	(*colorscheme)->colors[1].elevation = 0;
	(*colorscheme)->colors[1].color.r = 0;
	(*colorscheme)->colors[1].color.g = 0;
	(*colorscheme)->colors[1].color.b = 0;
	(*colorscheme)->colors[1].color.a = 1.0;
	(*colorscheme)->colors[2].elevation = 0;
	(*colorscheme)->colors[2].color.r = 255;
	(*colorscheme)->colors[2].color.g = 255;
	(*colorscheme)->colors[2].color.b = 255;
	(*colorscheme)->colors[2].color.a = 1.0;
	(*colorscheme)->colors[3].elevation = 0;
	(*colorscheme)->colors[3].color.r = 255;
	(*colorscheme)->colors[3].color.g = 255;
	(*colorscheme)->colors[3].color.b = 255;
	(*colorscheme)->colors[3].color.a = 1.0;

	return 0;
}

int colorize(geotiffmap_t *map, colorscheme_t *colorscheme) {
	if(!colorscheme->isAbsolute) {
		int16_t max = map->max_elevation;
		int16_t min = map->min_elevation;
		colorscheme->colors[0].elevation = min;
		colorscheme->colors[1].elevation = min;
		colorscheme->colors[colorscheme->num_stops].elevation = max;
		colorscheme->colors[colorscheme->num_stops + 1].elevation = max;
		for(int i = 2; i < colorscheme->num_stops; i++) {
			colorscheme->colors[i].elevation = min + ((max - min) / (colorscheme->num_stops - 1));
		}
	}

	for(int i = 0; i < map->height; i++) {
		for(int j = 0; j < map->width; j++) {
			int stop = 0;
			for(int n = 1; n <= colorscheme->num_stops; n++) {
				if(colorscheme->colors[n].elevation <= map->data[i][j].elevation) {
					stop = n;
				} else {
					break;
				}
			}
			double percent_of_elevation_change = (double)(map->data[i][j].elevation - colorscheme->colors[stop].elevation) / (double)(colorscheme->colors[stop + 1].elevation - colorscheme->colors[stop].elevation);
			map->data[i][j].color.r = colorscheme->colors[stop].color.r + (percent_of_elevation_change * (double)(colorscheme->colors[stop + 1].color.r - colorscheme->colors[stop].color.r));
			map->data[i][j].color.g = colorscheme->colors[stop].color.g + (percent_of_elevation_change * (double)(colorscheme->colors[stop + 1].color.g - colorscheme->colors[stop].color.g));
			map->data[i][j].color.b = colorscheme->colors[stop].color.b + (percent_of_elevation_change * (double)(colorscheme->colors[stop + 1].color.b - colorscheme->colors[stop].color.b));
			map->data[i][j].color.a = 1.0;
		}
	}

	return 0;
}

void printGeotiffInfo(geotiffmap_t *map, TIFF *tiff) {
	printf("Geotiff Information:\n");
	printf("  File Name:\n");
	printf("    %s\n", map->name);
	printf("  Dimensions:\n");
	printf("    Width: %ipx\n", map->width);
	printf("    Height: %ipx\n", map->height);
    printf("  Stats:\n");
	printf("    Max Elevation: %lim\n", (long) map->max_elevation);
	printf("    Min Elevation: %lim\n", (long) map->min_elevation);
}

int renderPNG(geotiffmap_t *map, char *outfile) {
	FILE *fp = fopen(outfile, "w");
	png_structp png_ptr = NULL;
	png_infop info_ptr = NULL;
	if(!fp)
		return ANAX_ERR_NO_MEMORY;

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	//png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, (png_voidp) user_error_ptr, user_error_fn, user_warning_fn);
	if(!png_ptr)
		return ANAX_ERR_PNG_STRUCT_FAILURE;
	info_ptr = png_create_info_struct(png_ptr);
	if(!info_ptr) {
		png_destroy_write_struct(&png_ptr, (png_infopp) NULL);
		return ANAX_ERR_PNG_STRUCT_FAILURE;
	}
	
	if(setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_write_struct(&png_ptr, &info_ptr);
		fclose(fp);
		return ANAX_ERR_PNG_STRUCT_FAILURE;
	}

	png_init_io(png_ptr, fp);
	png_set_write_status_fn(png_ptr, NULL);

	png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);

	int bit_depth = 8;

	// Set up the PNG header
	png_set_IHDR(
			png_ptr,
			info_ptr,
			map->width,						// Width of image in pixels
			map->height,					// Height of image in pixels
			bit_depth, 						// Color depth of each channel
			PNG_COLOR_TYPE_RGB_ALPHA, 		// Supported channels
			PNG_INTERLACE_NONE,				// Interlace type
			PNG_COMPRESSION_TYPE_DEFAULT,	// Compression type
			PNG_FILTER_TYPE_DEFAULT			// Filter type
	);
	png_write_info(png_ptr, info_ptr);

	if(bit_depth > 8) {
		png_set_swap(png_ptr);
	} else if(bit_depth < 8) {
		png_set_packswap(png_ptr);
	}

	// Flush after every line
	png_set_flush(png_ptr, 1);

	// Write the PNG
	int percent_interval = map->height / 100;
	png_byte *row_pointer = calloc(map->width, 4 * (bit_depth / 8));
	for(int i = 0; i < map->height; i++) {
		int pos = 0;
		for(int j = 0; j < map->width; j++) {
			row_pointer[pos] = (char)(map->data[i][j].color.r);
			row_pointer[pos + 1] = (char)(map->data[i][j].color.g);
			row_pointer[pos + 2] = (char)(map->data[i][j].color.b);
			row_pointer[pos + 3] = (char)((int)(map->data[i][j].color.a * 255));
			pos += 4;
		}

		png_write_row(png_ptr, row_pointer);

		if(i % percent_interval == 0) {
			printf("%i%%\n", i / percent_interval);
		}
	}
	png_write_end(png_ptr, NULL);
	
	// Free memory
	png_destroy_write_struct(&png_ptr, &info_ptr);
	

	fclose(fp);
	return 0;
}

//void updatePNGWriteStatus(png_structp png_ptr, png_uint32 row, int pass);
