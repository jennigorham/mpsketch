#ifndef MPSKETCH_MPTORASTER_INCLUDED
#define MPSKETCH_MPTORASTER_INCLUDED

#include <X11/Xutil.h>		/* BitmapOpenFailed, etc. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#define USE_MPTOPDF false //whether to use mptopdf or mpost. The advantage of using mpost is that we can run it in nonstopmode and therefore display an error message onscreen when it fails (maybe this is possible with mptopdf but I don't know how). The advantage of using mptopdf is that we don't need "prologues:=3" in the mp file

// metapost coords of lower left corner of image
float ll_x;
float ll_y;

char *job_name; //The part of the metapost filename before ".mp"
unsigned int fig_num; //metapost figure number (the "1" in "beginfig(1)" for example)

int density; //points per inch for bitmap. changes when we zoom in

//run metapost to create ps/pdf file
int run_mpost();
//get the coordinates of the lower left corner of the image, which are written in the logfile
int get_coords();
//convert the ps/pdf to a raster image
int make_bitmap(char *filename);
//read in the raster
int get_bitmap(char *filename, Display *d, Window w, Pixmap *bitmap, unsigned int *bitmap_width, unsigned int *bitmap_height);

#endif
