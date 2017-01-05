#ifndef MPSKETCH_PATHS_INCLUDED
#define MPSKETCH_PATHS_INCLUDED

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mplib/mplib.h" //needed to get control points for bezier curve

#define INITIAL_POINTS 10 //how many points to allocate space for initially
#define INCH 72.0 //an inch is 72 postscript points

char unit_name[20]; //might want it to write coords in terms of a variable, eg "(5u,2u)"
double unit; //default unit in postscript points

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

struct path *cur_path; //current path

unsigned int coord_precision; //number of decimal places for printing coordinates

void init_path(struct path *p);

double string_to_bp(char *s,bool *valid); //return number of postscript points (aka big points, bp) from strings like 'cm', '5.2in', etc
void string_to_path(char *buffer); //get path from string
char *path_to_string(); //return the string defining the path

void find_control_points(); //find metapost's bezier control points for the path

//edit a point
void set_straight(int i,bool is_straight);
void set_coords(int i,double x,double y);
void set_point(int i, double x, double y, bool is_straight); //set both the coords and the straightness
//edit the last point in the path
void set_last_coords(double x,double y);
void set_last_straight(bool is_straight);
void set_last_point(double x, double y, bool is_straight);

void append_point(double x, double y, bool is_straight);
void remove_point(int i);
void insert_point(int i, double x, double y, bool is_straight);

#endif
