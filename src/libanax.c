#include <geotiff.h>
#include <limits.h>
#include <png.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "globals.h"
#include "libanax.h"

/* DEBUGGING FUNCTIONS */

void SHOW_DATA_AT_POINT(geotiffmap_t *map, int r, int c) {
	printf("SHOWING DATA AT ROW %i, COL %i\n", r, c);
	printf("Location: %f°N, %f°E\n", map->data[r][c].latitude, map->data[r][c].longitude);
	/*printf("Location: %i°%i'%f\"N, %i°%i'%f\"E\n", map->data[r][c].latitude.degree,
	                                               map->data[r][c].latitude.minute,
	                                               map->data[r][c].latitude.second,
	                                               map->data[r][c].longitude.degree,
	                                               map->data[r][c].longitude.minute,
	                                               map->data[r][c].longitude.second);*/
	printf("Elevation: %im\n", (int)map->data[r][c].elevation);
	printf("Color: [R %i, G %i, B %i, A %f]\n", map->data[r][c].color.r, map->data[r][c].color.g, map->data[r][c].color.b, map->data[r][c].color.a);
}

void SHOW_COLOR_SCHEME(colorscheme_t *colors) {
	printf("SHOWING COLOR SCHEME\n");
	for(int i = 1; i <= colors->num_stops; i++) {
		printf("#%i: %im = [R %i, G %i, B %i, A %f]\n", i, (int)colors->colors[i].elevation, colors->colors[i].color.r, colors->colors[i].color.g, colors->colors[i].color.b, colors->colors[i].color.a);
	}
}

/* END DEBUGGING FUNCTIONS */


int initMap(geotiffmap_t **map, TIFF *tiff, char *srcfile, int suppress_output, frame_coords_t *frame) {
	int err;
	
	// Load GTIF type from TIFF
	GTIF *geotiff = GTIFNew(tiff);

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
	
	// Get the coordinates of each corner
	double left_lon, right_lon, top_lat, bottom_lat;
	double change_in_lon, change_in_lat;
	double x, y;
	x = 0.0;
	y = 0.0;
	GTIFImageToPCS(geotiff, &x, &y);
	left_lon = x;
	top_lat = y;
	x = (double)((*map)->width - 1);
	y = (double)((*map)->height - 1);
	GTIFImageToPCS(geotiff, &x, &y);
	right_lon = x;
	bottom_lat = y;
	change_in_lon = right_lon - left_lon;
	change_in_lat = top_lat - bottom_lat;

	// Allocate enough memory for the entire map struct
    // (This is all done all at once to help ensure there won't be any out-of-memory
	// errors after processing has already begun)
	(*map)->data = malloc(((*map)->height + (2 * MAPFRAME)) * sizeof(point_t *));
	for(int i = 0; i < (*map)->height + (2 * MAPFRAME); i++) {
		(*map)->data[i] = malloc(((*map)->width + (2 * MAPFRAME)) * sizeof(point_t));
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
		double lat = bottom_lat + change_in_lat * (1.0 - ((double)row / ((double)((*map)->height) - 1.0)));
		for(int col = 0; col < (*map)->width; col++) {
			(*map)->data[row + MAPFRAME][col + MAPFRAME].elevation = tiff_line.data[col];
			if((*map)->data[row + MAPFRAME][col + MAPFRAME].elevation > (*map)->max_elevation) {
				(*map)->max_elevation = (*map)->data[row + MAPFRAME][col + MAPFRAME].elevation;
			}
			if((*map)->data[row + MAPFRAME][col + MAPFRAME].elevation < (*map)->min_elevation) {
				(*map)->min_elevation = (*map)->data[row + MAPFRAME][col + MAPFRAME].elevation;
			}
            (*map)->data[row + MAPFRAME][col + MAPFRAME].latitude = lat;
            (*map)->data[row + MAPFRAME][col + MAPFRAME].longitude = left_lon + change_in_lon * ((double)col / ((double)((*map)->width) - 1.0));
		}
	}

    GTIFFree(geotiff);
	
	// Set frame coordinates
	// This code divides the frame into eight portions (N, S, E, W, NE, SE, SW, NW) and identifies
	// what the coordinates of the midpoint of each section would be.
	point_t *midpoint = &((*map)->data[(*map)->height / 2 + MAPFRAME][(*map)->width / 2 + MAPFRAME]);
	double lat_step = (*map)->data[MAPFRAME][MAPFRAME].latitude - (*map)->data[MAPFRAME + 1][MAPFRAME + 1].latitude;
	double lon_step = (*map)->data[MAPFRAME + 1][MAPFRAME + 1].longitude - (*map)->data[MAPFRAME][MAPFRAME].longitude;
	frame->N_set = 0;
	frame->S_set = 0;
	frame->E_set = 0;
	frame->W_set = 0;
	frame->NE_set = 0;
	frame->SE_set = 0;
	frame->SW_set = 0;
	frame->NW_set = 0;
	frame->north_lat = (*map)->data[MAPFRAME][(*map)->width / 2 + MAPFRAME].latitude + ((MAPFRAME / 2.0) * lat_step);
	frame->south_lat = (*map)->data[(*map)->height + MAPFRAME - 1][(*map)->width / 2 + MAPFRAME].latitude - ((MAPFRAME / 2.0) * lat_step);
	frame->mid_lat = midpoint->latitude;
	frame->west_lon = (*map)->data[(*map)->height / 2 + MAPFRAME][MAPFRAME].longitude - ((MAPFRAME / 2.0) * lon_step);
	frame->east_lon = (*map)->data[(*map)->height / 2 + MAPFRAME][(*map)->width + MAPFRAME - 1].longitude + ((MAPFRAME / 2.0) * lon_step);
	frame->mid_lon = midpoint->longitude;

	if(!suppress_output)
		printGeotiffInfo(*map, tiff);
	return 0;
}

int setDefaultColors(geotiffmap_t *map, colorscheme_t **colorscheme, int isAbsolute) {
	int16_t min = (map == NULL) ? 0 : map->min_elevation;
	int16_t max = (map == NULL) ? 0 : map->max_elevation;
	
	*colorscheme = malloc(sizeof(colorscheme_t));
	(*colorscheme)->isAbsolute = isAbsolute;
	(*colorscheme)->num_stops = 2;
	(*colorscheme)->colors = calloc(4, sizeof(colorstop_t));
	(*colorscheme)->colors[0].elevation = min;
	(*colorscheme)->colors[0].color.r = 0;
	(*colorscheme)->colors[0].color.g = 0;
	(*colorscheme)->colors[0].color.b = 0;
	(*colorscheme)->colors[0].color.a = 1.0;
	(*colorscheme)->colors[1].elevation = min;
	(*colorscheme)->colors[1].color.r = 0;
	(*colorscheme)->colors[1].color.g = 0;
	(*colorscheme)->colors[1].color.b = 0;
	(*colorscheme)->colors[1].color.a = 1.0;
	(*colorscheme)->colors[2].elevation = max;
	(*colorscheme)->colors[2].color.r = 255;
	(*colorscheme)->colors[2].color.g = 255;
	(*colorscheme)->colors[2].color.b = 255;
	(*colorscheme)->colors[2].color.a = 1.0;
	(*colorscheme)->colors[3].elevation = max;
	(*colorscheme)->colors[3].color.r = 255;
	(*colorscheme)->colors[3].color.g = 255;
	(*colorscheme)->colors[3].color.b = 255;
	(*colorscheme)->colors[3].color.a = 1.0;

	return 0;
}

int loadColorScheme(geotiffmap_t *map, colorscheme_t **colorscheme, char *colorfile) {
    FILE *fp = fopen(colorfile, "r");
    if(!fp)
        return ANAX_ERR_FILE_DOES_NOT_EXIST;
    *colorscheme = malloc(sizeof(colorscheme_t));
    if(!*colorscheme)
        return ANAX_ERR_NO_MEMORY;

    (*colorscheme)->isAbsolute = -1;
    (*colorscheme)->num_stops = 0;
    (*colorscheme)->colors = NULL;

    char buf[BUFSIZE];
    while((*colorscheme)->isAbsolute == -1) {
        if(!fgets(buf, BUFSIZE, fp))
            return ANAX_ERR_INVALID_COLOR_FILE;
        if(buf[0] == '#' || buf[0] == '\n' || buf[0] == ' ')
            continue;

        buf[8] = 0;
        if(!strcmp(buf, "Absolute")) {
            (*colorscheme)->isAbsolute = ANAX_ABSOLUTE_COLORS;
        } else if(!strcmp(buf, "Relative")) {
            (*colorscheme)->isAbsolute = ANAX_RELATIVE_COLORS;
        }
    }

    if((*colorscheme)->isAbsolute == ANAX_ABSOLUTE_COLORS) {
        while(fgets(buf, BUFSIZE, fp)) {
            if(buf[0] == '#' || buf[0] == '\n' || buf[0] == ' ')
                continue;
            int e, r, g, b;
            int res = sscanf(buf, "%i %i %i %i", &e, &r, &g, &b);
            if(res != 4)
                return ANAX_ERR_INVALID_COLOR_FILE;
            (*colorscheme)->num_stops++;
            (*colorscheme)->colors = realloc((*colorscheme)->colors, ((*colorscheme)->num_stops + 2) * sizeof(colorstop_t));
            if(!(*colorscheme)->colors)
                return ANAX_ERR_NO_MEMORY;
            (*colorscheme)->colors[(*colorscheme)->num_stops].elevation = e;
            (*colorscheme)->colors[(*colorscheme)->num_stops].color.r = r;
            (*colorscheme)->colors[(*colorscheme)->num_stops].color.g = g;
            (*colorscheme)->colors[(*colorscheme)->num_stops].color.b = b;
            (*colorscheme)->colors[(*colorscheme)->num_stops].color.a = 1.0;
        }
    } else if((*colorscheme)->isAbsolute == ANAX_RELATIVE_COLORS) {
        while(fgets(buf, BUFSIZE, fp)) {
            if(buf[0] == '#' || buf[0] == '\n' || buf[0] == ' ')
                continue;
            int e, r, g, b;
            int res = sscanf(buf, "%i %i %i %i", &e, &r, &g, &b);
            if(res != 4)
                return ANAX_ERR_INVALID_COLOR_FILE;
            (*colorscheme)->num_stops++;
            (*colorscheme)->colors = realloc((*colorscheme)->colors, ((*colorscheme)->num_stops + 2) * sizeof(colorstop_t));
            if(!(*colorscheme)->colors)
                return ANAX_ERR_NO_MEMORY;
            (*colorscheme)->colors[(*colorscheme)->num_stops].elevation = e;
            (*colorscheme)->colors[(*colorscheme)->num_stops].color.r = r;
            (*colorscheme)->colors[(*colorscheme)->num_stops].color.g = g;
            (*colorscheme)->colors[(*colorscheme)->num_stops].color.b = b;
            (*colorscheme)->colors[(*colorscheme)->num_stops].color.a = 1.0;
        }
    }

    (*colorscheme)->colors[0].elevation = (*colorscheme)->colors[1].elevation;
    (*colorscheme)->colors[0].color.r = (*colorscheme)->colors[1].color.r;
    (*colorscheme)->colors[0].color.g = (*colorscheme)->colors[1].color.g;
    (*colorscheme)->colors[0].color.b = (*colorscheme)->colors[1].color.b;
    (*colorscheme)->colors[0].color.a = (*colorscheme)->colors[1].color.a;
    (*colorscheme)->colors[(*colorscheme)->num_stops + 1].elevation = (*colorscheme)->colors[(*colorscheme)->num_stops].elevation;
    (*colorscheme)->colors[(*colorscheme)->num_stops + 1].color.r = (*colorscheme)->colors[(*colorscheme)->num_stops].color.r;
    (*colorscheme)->colors[(*colorscheme)->num_stops + 1].color.g = (*colorscheme)->colors[(*colorscheme)->num_stops].color.g;
    (*colorscheme)->colors[(*colorscheme)->num_stops + 1].color.b = (*colorscheme)->colors[(*colorscheme)->num_stops].color.b;
    (*colorscheme)->colors[(*colorscheme)->num_stops + 1].color.a = (*colorscheme)->colors[(*colorscheme)->num_stops].color.a;

	return 0;
}

int setRelativeElevations(colorscheme_t *colorscheme, int16_t max, int16_t min) {
    for(int i = 1; i <= colorscheme->num_stops; i++) {
        double e = (double)(colorscheme->colors[i].elevation) / 100.0;
        colorscheme->colors[i].elevation = (int)(((max - min) * e) + min);
    }
    
    colorscheme->colors[0].elevation = colorscheme->colors[1].elevation;
    colorscheme->colors[colorscheme->num_stops + 1].elevation = colorscheme->colors[colorscheme->num_stops].elevation;
    
    return 0;
}

int colorize(geotiffmap_t *map, colorscheme_t *colorscheme) {
	for(int i = MAPFRAME; i < map->height + MAPFRAME; i++) {
		for(int j = MAPFRAME; j < map->width + MAPFRAME; j++) {
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

int scaleImage(geotiffmap_t **map, double scale) {
	int err;

	// Allocate a new map struct
	geotiffmap_t *newmap = malloc(sizeof(geotiffmap_t));

	// Calculate the new image size
	newmap->height = (int)((double)(*map)->height * scale);
	newmap->width = (int)((double)(*map)->width * scale);
	
	// Calculate the step size
	// (i.e., how many old pixels one new pixel corresponds to)
	double step_vert = (double)((*map)->height) / (double)(newmap->height);
	double step_horiz = (double)((*map)->height) / (double)(newmap->height);
	//int16_t step_vert = (*map)->height / newmap->height;
	//int16_t step_horiz = (*map)->width / newmap->width;
	//step_vert += (step_vert % 2 == 0) ? 1 : 0;
	//step_horiz += (step_horiz % 2 == 0) ? 1 : 0;

	// Allocate enough memory for the entire map struct
	newmap->data = malloc((newmap->height + (2 * MAPFRAME)) * sizeof(point_t *));
	for(int i = 0; i < newmap->height + (2 * MAPFRAME); i++) {
		newmap->data[i] = malloc((newmap->width + (2 * MAPFRAME)) * sizeof(point_t));
		if(newmap->data[i] == NULL)
			return ANAX_ERR_NO_MEMORY;
	}

	// Create a matrix ('box') that contains all of the old pixels corresponding to one new pixel
	int16_t box[(int)step_vert][(int)step_horiz];

	// Scale the image
	for(int r = 0; r < newmap->height; r++) {
		for(int c = 0; c < newmap->width; c++) {
			memset(box, 0, step_vert * step_horiz * sizeof(int16_t));

			// Set up the box
			int16_t box_firstrow = (int)((r * step_vert) - ((step_vert - 1) / 2) + MAPFRAME);
			int16_t box_firstcol = (int)((c * step_horiz) - ((step_horiz - 1) / 2) + MAPFRAME);
			long int sum = 0;
			int cellcount = 0;
			for(int boxr = 0; boxr < (int)step_vert; boxr++) {
				for(int boxc = 0; boxc < (int)step_horiz; boxc++) {
					box[boxr][boxc] = (*map)->data[box_firstrow + boxr][box_firstcol + boxc].elevation;
					if(box[boxr][boxc] != -9999) {
					    sum += box[boxr][boxc];
					    cellcount++;
					}
					/*
					if((box_firstrow + boxr >= 0) && (box_firstcol + boxc >= 0) && (box_firstrow + boxr < (*map)->height) && (box_firstcol + boxc < (*map)->width)) {
						box[boxr][boxc] = (*map)->data[box_firstrow + boxr][box_firstcol + boxc].elevation;
						sum += box[boxr][boxc];
						cellcount++;
					} else {
						box[boxr][boxc] = -9999;
					}
					*/
				}
			}

			// Average the elevation and save the value
			newmap->data[r + MAPFRAME][c + MAPFRAME].elevation = sum / cellcount;
		}
	}

	// Copy over metadata from the old map struct that has not changed
	newmap->name = calloc(strlen((*map)->name) + 1, sizeof(char));
	strncpy(newmap->name, (*map)->name, strlen((*map)->name));
	newmap->max_elevation = (*map)->max_elevation;
	newmap->min_elevation = (*map)->min_elevation;

	// Free the old map struct and return the new one
	freeMap(*map);
	*map = newmap;

	return 0;
}

int renderPNG(geotiffmap_t *map, char *outfile, int suppress_output) {
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
	double percent_interval = (double)map->height / 100.0;
	png_byte *row_pointer = calloc(map->width, 4 * (bit_depth / 8));
	for(int i = MAPFRAME; i < map->height + MAPFRAME; i++) {
		int pos = 0;
		for(int j = MAPFRAME; j < map->width + MAPFRAME; j++) {
			row_pointer[pos] = (char)(map->data[i][j].color.r);
			row_pointer[pos + 1] = (char)(map->data[i][j].color.g);
			row_pointer[pos + 2] = (char)(map->data[i][j].color.b);
			row_pointer[pos + 3] = (char)((int)(map->data[i][j].color.a * 255));
			pos += 4;
		}

		png_write_row(png_ptr, row_pointer);

		if(!suppress_output) {
			if((int)percent_interval > 0) {
				if((i - MAPFRAME) % (int)percent_interval == 0) {
					printf("%i%%\n", (int)((i - MAPFRAME) / percent_interval) + 1);
				}
			} else {
				printf("%i%%\n", ((i - MAPFRAME) * 100) / (int)map->height);
			}
		}
	}
	png_write_end(png_ptr, NULL);
	
	// Free memory
	free(row_pointer);
	png_destroy_write_struct(&png_ptr, &info_ptr);
	

	fclose(fp);
	return 0;
}

int getCorners(geotiffmap_t *map, double *top, double *bottom, double *left, double *right) {
    *top = map->data[MAPFRAME][MAPFRAME].latitude;
    *left = map->data[MAPFRAME][MAPFRAME].longitude;
    *bottom = map->data[map->height + MAPFRAME - 1][map->width + MAPFRAME - 1].latitude;
    *right = map->data[map->height + MAPFRAME - 1][map->width + MAPFRAME - 1].longitude;
    
    return 0;
}

int writeMapData(anaxjob_t *current_job, geotiffmap_t *map) {
    FILE *fp = fopen(current_job->tmpfile, "w+");
    
    uint32_t hdr[4];
    hdr[0] = (uint32_t)(map->height);
    hdr[1] = (uint32_t)(map->width);
    hdr[2] = map->max_elevation;
    hdr[3] = map->min_elevation;
    fwrite(hdr, sizeof(uint32_t), 4, fp);

    int bufsize = map->width + (2 * MAPFRAME);
    int16_t buf[bufsize];
    for(int i = 0; i < map->height + (2 * MAPFRAME); i++) {
        for(int j = 0; j < map->width + (2 * MAPFRAME); j++) {
            buf[j] = map->data[i][j].elevation;
        }
        fwrite(buf, sizeof(int16_t), bufsize, fp);
    }
    
    fclose(fp);
    
    return 0;
}

int readMapData(anaxjob_t *current_job, geotiffmap_t **map) {
    FILE *fp = fopen(current_job->tmpfile, "r");

	// Allocate main map struct
	*map = malloc(sizeof(geotiffmap_t));

    // Load data into main map struct
    uint32_t hdr[4];
    fread(hdr, sizeof(uint32_t), 4, fp);
    (*map)->name = current_job->name;
    (*map)->height = hdr[0];
    (*map)->width = hdr[1];
    (*map)->max_elevation = (int16_t)hdr[2];
    (*map)->min_elevation = (int16_t)hdr[3];

    // Allocate map data array
	(*map)->data = malloc(((*map)->height + (2 * MAPFRAME)) * sizeof(point_t *));
	if((*map)->data == NULL)
	    return ANAX_ERR_NO_MEMORY;
	for(int i = 0; i < (*map)->height + (2 * MAPFRAME); i++) {
		(*map)->data[i] = malloc(((*map)->width + (2 * MAPFRAME)) * sizeof(point_t));
		if((*map)->data[i] == NULL)
			return ANAX_ERR_NO_MEMORY;
	}
	
	// Read map data from the file and store it
	int bufsize = (*map)->width + (2 * MAPFRAME);
	int16_t buf[bufsize];
	for(int i = 0; i < (*map)->height + (2 * MAPFRAME); i++) {
	    fread(buf, sizeof(int16_t), bufsize, fp);
	    for(int j = 0; j < (*map)->width + (2 * MAPFRAME); j++) {
	        //fread(buf, sizeof(int16_t), bufsize, fp);
	        (*map)->data[i][j].elevation = buf[j];
	    }
	}
	
	fclose(fp);
	
	return 0;
}

void freeMap(geotiffmap_t *map) {
    for(int i = 0; i < map->height + (2 * MAPFRAME); i++) {
        free(map->data[i]);
    }
    free(map->data);
    free(map);
}

//void updatePNGWriteStatus(png_structp png_ptr, png_uint32 row, int pass);
