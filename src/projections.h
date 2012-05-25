#ifndef PROJECTIONS_H
#define PROJECTIONS_H

#define EARTHRADIUS_KM          6378.1
#define EARTHCIRCUMFERENCE_KM   40075.0

#include <math.h>
#include "libanax.h"

double rad(double degree);
double getMajorAxis(double post);
int convertToMercator(geotiffmap_t *original, geotiffmap_t **newmap, double post);

#endif