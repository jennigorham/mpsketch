#ifndef MPSKETCH_COMMON_INCLUDED
#define MPSKETCH_COMMON_INCLUDED

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/types.h> //for getpid
#include <unistd.h>

#include "paths.h"
#include "mptoraster.h"

#define POINT_RADIUS 3 //for drawing a little circle around each point on the path
#define SCROLL_STEP 10 //How many pixels to scroll at a time

#define CURVE_MODE 0
#define STRAIGHT_MODE 1
#define CORNER_MODE 2
#define CIRCLE_MODE 3

char *job_name; //The part of the metapost filename before ".mp"
char tmp_job_name[100];

int trace_x_offset,trace_y_offset;
bool show_trace;

int mode; //drawing mode
bool finished_drawing; //if true then we're ready to start drawing another path or circle.

//for scrolling
int x_offset;
int y_offset;

double pixels_per_point;

//width/height of the drawing area
unsigned int sketch_height;
unsigned int sketch_width;

//Converting between metapost coords and pixels from upper left corner
int mp_x_coord_to_pxl(double x);
int mp_y_coord_to_pxl(double y);
double pxl_to_mp_x_coord(int x);
double pxl_to_mp_y_coord(int y);

//when we mouse over a point in a completed path, we can edit it
bool edit;
int edit_point; //which point are we editing
bool dragging_point;

void draw_path(); //draw the current path or circle
void output_path(); //print path to clipboard and stdout
void path_mode_change(bool is_straight); //change to straight/curve mode
void end_path();

void click_point(int x, int y); //when the user clicks to add a point to the path, or create a circle
void pointer_move(int x,int y);
void undo(); //undo creating a point

void initialise();
void cleanup();

//defined in mpsketch.c or gtk-test.c
void draw_circle(double centre_x, double centre_y, int r);
void draw_point(double centre_x, double centre_y);
void link_point_pair(struct point *p, struct point *q); //draw either a straight line or a bezier curve linking two consecutive points on a path
void copy_to_clipboard(char *s);
void redraw_screen();
void mode_change(); //for gtk to update info bar
#endif
