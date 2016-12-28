#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../mplib/mplib.h" //needed to get control points for bezier curve

#ifndef MPSKETCH_PATHS_INCLUDED
#define MPSKETCH_PATHS_INCLUDED

#define PRECISION 0 //number of decimal points to print for drawing coordinates
#define INITIAL_POINTS 10 //how many points to allocate space for initially

struct point {
	double x,y,left_x,left_y,right_x,right_y;
	bool straight; //should the section after this point be straight
};
struct path {
	struct point *points;
	int n; //number of points
	size_t size; //number of points we've allocated space for
	bool cycle;
};

struct path *cur_path;

void init_path(struct path *p);

void get_path(char *buffer); //get path from string
void get_controls(); //find metapost's bezier control points for the path
char *path_string(); //return the string defining the path

//edit a point
void set_straight(int i,bool is_straight);
void set_coords(int i,double x,double y);
void set_point(int i, double x, double y, bool is_straight); //set both the coords and the straightness
//edit the last point in the path
void set_last_coords(double x,double y);
void set_last_straight(bool is_straight);
void set_last_point(double x, double y, bool is_straight);

void append_point(double x, double y, bool is_straight);

#endif
