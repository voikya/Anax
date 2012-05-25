#include "projections.h"

double rad(double degree) {
    return degree * (M_PI / 180.0);
}

double getMajorAxis(double post) {
    return post * EARTHCIRCUMFERENCE_KM;
}

int convertToMercator(geotiffmap_t *original, geotiffmap_t **newmap, double post) {
    
    return 0;
}