#ifndef MPSKETCH_MPTORASTER_INCLUDED
#define MPSKETCH_MPTORASTER_INCLUDED

#include <X11/Xutil.h>		/* BitmapOpenFailed, etc. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#define USE_MPTOPDF false //whether to use mptopdf or mpost. The advantage of using mpost is that we can run it in nonstopmode and therefore display an error message onscreen when it fails (maybe this is possible with mptopdf but I don't know how). The advantage of using mptopdf is that we don't need "prologues:=3" in the mp file

//run metapost to create ps/pdf file
int run_mpost(char *job_name);
//get the coordinates of the lower left corner of the image, which are written in the logfile
int get_coords(char *job_name, char *fig_num, float *ll_x, float *ll_y);
//convert the ps/pdf to a raster image
int make_bitmap(char *job_name, char *fig_num, int density, char *filename);
//read in the raster
int get_bitmap(char *filename, Display *d, Window w, Pixmap *bitmap, unsigned int *bitmap_width, unsigned int *bitmap_height);

#endif
