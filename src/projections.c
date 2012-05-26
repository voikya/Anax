#include "projections.h"

double rad(double degree) {
    return degree * (M_PI / 180.0);
}

double getMajorAxis(double post) {
    // Return equatorial radius of the Earth in pixels
    return 1/(((post / 360.0) * EARTHCIRCUMFERENCE_KM) / EARTHRADIUS_KM);
}

double getMinorAxis(double post) {
    // Return polar radius of the Earth in pixels
    return 1/(((post / 360.0) * EARTHCIRCUMFERENCE_KM_POLAR) / EARTHRADIUS_KM_POLAR);
}

double getAvgAxis(double post) {
    // Return average radius of the Earth in pixels
    return 1/(((post / 360.0) * EARTHCIRCUMFERENCE_KM) / EARTHRADIUS_KM_AVG);
}

double _y_to_mercator(double y, double r) {
    double phi = rad(y);
    double merc = log((1.0 + sin(phi)) / cos(phi));
    return round(r * merc);
}

int convertToEquirectangular(geotiffmap_t **map) {
    return 0;
}

int convertToMercator(geotiffmap_t **map) {
    // Get the major and minor axis according to the map's pixel scale
    double r = getAvgAxis((*map)->vertical_pixel_scale);
    
    // Get the new image's height and width
    double top = (*map)->data[MAPFRAME][MAPFRAME].latitude;
    //double left = (*map)->data[MAPFRAME][MAPFRAME].longitude;
    double bottom = (*map)->data[MAPFRAME + (*map)->height - 1][MAPFRAME + (*map)->width - 1].latitude;
    //double right = (*map)->data[MAPFRAME + (*map)->height - 1][MAPFRAME + (*map)->width - 1].longitude;
    int width = (*map)->width;
    int height = _y_to_mercator(top, r) - _y_to_mercator(bottom, r) + 1;
        
    // Allocate a new map
    geotiffmap_t *newmap = malloc(sizeof(geotiffmap_t));
    newmap->data = calloc(height + (2 * MAPFRAME), sizeof(point_t *));
	for(int i = 0; i < height + (2 * MAPFRAME); i++) {
		newmap->data[i] = calloc(width + (2 * MAPFRAME), sizeof(point_t));
		if(newmap->data[i] == NULL)
			return ANAX_ERR_NO_MEMORY;
	}
	
	// Write map metadata
	newmap->name = calloc(strlen((*map)->name) + 1, sizeof(char));
	strncpy(newmap->name, (*map)->name, strlen((*map)->name));
	newmap->height = height;
	newmap->width = width;
	newmap->max_elevation = (*map)->max_elevation;
	newmap->min_elevation = (*map)->min_elevation;
	newmap->vertical_pixel_scale = 0;
	newmap->horizontal_pixel_scale = (*map)->horizontal_pixel_scale;
    
    // Project the map
    int equatorial_offset = _y_to_mercator(bottom, r);
    for(int i = MAPFRAME; i < (*map)->height + MAPFRAME; i++) {
        int row = (height - 1) - ((int)_y_to_mercator((*map)->data[i][MAPFRAME].latitude, r) - equatorial_offset) + MAPFRAME;
        for(int j = MAPFRAME; j < (*map)->width + MAPFRAME; j++) {
            int col = j;
            newmap->data[row][col].elevation = (*map)->data[i][j].elevation;
            newmap->data[row][col].longitude = (*map)->data[i][j].longitude;
            newmap->data[row][col].latitude = (*map)->data[i][j].latitude;
        }
    }
    
    // Fill in gaps in the map by averaging data
    for(int i = MAPFRAME; i < newmap->height + MAPFRAME; i++) {
        if(newmap->data[i][MAPFRAME].elevation == 0 && newmap->data[i][MAPFRAME].longitude == 0 && newmap->data[i][MAPFRAME].latitude == 0) {
            int nullcount = 1;
            int row = i + 1;
            while(newmap->data[row][MAPFRAME].elevation == 0 && newmap->data[row][MAPFRAME].longitude == 0 && newmap->data[row][MAPFRAME].latitude == 0) {
                nullcount++;
                row++;
            }
            for(int i2 = i; i2 < i + nullcount; i2++) {
                double latdiff = newmap->data[i - 1][MAPFRAME].latitude - newmap->data[i + nullcount][MAPFRAME].latitude;
                double lat = newmap->data[i - 1][MAPFRAME].latitude - ((i2 - i + 1) * (latdiff / (nullcount + 1)));
                for(int j = MAPFRAME; j < (*map)->width + MAPFRAME; j++) {
                    newmap->data[i2][j].latitude = lat;
                    newmap->data[i2][j].longitude = newmap->data[i - 1][j].longitude;
                    int eldiff = newmap->data[i - 1][j].elevation - newmap->data[i + nullcount][j].elevation;
                    newmap->data[i2][j].elevation = newmap->data[i - 1][j].elevation + ((i2 - i + 1) * (eldiff / (nullcount + 1)));
                    //printf("[%i] Above: %i, Below: %i, New: %i\n", i2, newmap->data[i - 1][j].elevation, newmap->data[i + nullcount][j].elevation, newmap->data[i2][j].elevation);
                }
            }
            i += nullcount;
        }
    }

	// Free the old map struct and return the new one
	freeMap(*map);
	*map = newmap;
    
    return 0;
}