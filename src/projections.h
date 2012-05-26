#ifndef PROJECTIONS_H
#define PROJECTIONS_H

#define EARTHRADIUS_KM              6378.1
#define EARTHRADIUS_KM_POLAR        6356.8
#define EARTHRADIUS_KM_AVG          6367.5
#define EARTHCIRCUMFERENCE_KM       40075.0
#define EARTHCIRCUMFERENCE_KM_POLAR 39941.0

#include <math.h>
#include <stdlib.h>
#include "libanax.h"

double rad(double degree);
double getMajorAxis(double post);
double getMinorAxis(double post);
double getAvgAxis(double post);
int convertToEquirectangular(geotiffmap_t **map);
int convertToMercator(geotiffmap_t **map);

#endif