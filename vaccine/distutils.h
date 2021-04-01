#ifndef __DISTUTILS_H_ENV__
#define __DISTUTILS_H_ENV__ 1

/* called with lat and long in degrees */
double getDistance(double alat1, double along1, double alat2, double along2);

void printDistances(double latitude, double longitude);

#endif /* __DISTUTILS_H_ENV__ */

