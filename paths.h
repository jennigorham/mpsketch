#ifndef MPSKETCH_PATHS_INCLUDED
#define MPSKETCH_PATHS_INCLUDED

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
//#include "mplib/mplib.h" //needed to get control points for bezier curve. NOT ANY MORE

#define INITIAL_POINTS 10 //how many points to allocate space for initially
#define INCH 72.0 //an inch is 72 postscript points

char *unit_name; //might want it to write coords in terms of a variable, eg "(5u,2u)"
double unit; //default unit in postscript points

struct point {
	double x,y;
	double left_x,left_y,right_x,right_y; //bezier control points
	bool straight; //should the section after this point be straight
};

struct path {
	struct point *points;
	int n; //number of points
	double *aug_mat; //matrix representing the linear equations of theta's
	size_t size; //number of points we've allocated space for
	bool cycle; //last point connects to first point
};
//path can also store a circle by setting n=-1, points[0] = centre of the circle, points[1] = a point on the circumference

struct path *cur_path; //current path

unsigned int coord_precision; //number of decimal places for printing coordinates

void init_path(struct path *p);

//return number of postscript points (aka big points, bp) from strings like 'cm', '5.2in', etc
double string_to_bp(char *s,bool *valid);

//parse something like "(5cm,-3cm)". Returns pointer to next character after valid point string
char *string_to_point(char *s, double *x, double *y);

//Get a path from a string (store in cur_path). Returns pointer to next char after valid path string
char *string_to_path(char *buffer);

char *path_to_string(); //return the string defining the path

//Use the algorithm on p131 of metafontbook to find the control points for each curved section of the path
void get_row_k(double *l, double *psi, int n, int k); //get row k of the augmented matrix
void print_aug_matrix(int n); //for troubleshooting
double *get_aug_matrix(int i, int n, bool is_cycle); //turn the n points (starting at point i) connected by '..' into an augmented matrix representing the linear equations of theta's. return array of turning angles
void rref(int n); //put the augmented matrix into reduced row echelon form
double john_hobby_f(double theta, double phi); //John Hobby's formula. Used to find the control points
void get_u_v(int i, int k, int n, double *psi); //find the control points for a curved segment
void set_curved_pair_control_points(int i); //a curved section with only 2 points is really just a straight section. Set the control points accordingly.
void find_control_points(); //split the path into curved sections and solve for the bezier control points in each section

//edit a point
void set_straight(int i,bool is_straight);
void set_coords(int i,double x,double y);
void set_point(int i, double x, double y, bool is_straight); //set both the coords and the straightness
//edit the last point in the path
void set_last_coords(double x,double y);
void set_last_straight(bool is_straight);
void set_last_point(double x, double y, bool is_straight);

int append_point(double x, double y, bool is_straight); //returns 1 if realloc fails
void remove_point(int i);
void insert_point(int i, double x, double y, bool is_straight);

double bezier(double start, double start_right, double end_left, double end, double t);

void point_before(int i);//create a point inbetween two others

#endif
