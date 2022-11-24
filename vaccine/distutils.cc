#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <string.h>
#include <math.h>

class Location {
public:
    double _lat;
    double _long;
    const char *_namep;
};

Location main_locations [] = {
                              {40.44895, -79.92070, "Shady Ave"},
                              {38.91745, -77.03058, "Suzie"},
                              {0.0, 0.0, "End"}
};

/* called with lat and long in degrees */
double
getDistance(double alat1, double along1, double alat2, double along2)
{
    double a;
    double c;
    double r;
    double lat1;
    double lat2;
    double long1;
    double long2;
    double deltaLat;    /* greek letter phi */
    double deltaLong;   /* greek letter lambda */
    double pi;

    r = 3963.0;
    pi = 3.14159265358;
    lat1 = alat1 * pi / 180.0;
    lat2 = alat2 * pi / 180.0;
    long1 = along1 * pi / 180.0;
    long2 = along2 * pi / 180.0;
    deltaLat = lat1-lat2;
    deltaLong = long1-long2;
    a = (sin(deltaLat/2) * sin(deltaLat/2) +
         cos(lat1)*cos(lat2)*sin(deltaLong/2)*sin(deltaLong/2));
    c = 2 * atan2(sqrt(a), sqrt(1-a));
    return r*c;
}

void
printDistances(double latitude, double longitude)
{
    Location *lp;
    double distance;

    lp = main_locations;
    while(1) {
        if (strcmp(lp->_namep, "End") == 0)
            break;
        distance = getDistance(latitude, longitude, lp->_lat, lp->_long);
        printf("Distance from %s is %f\n", lp->_namep, distance);
        lp++;
    }
}
