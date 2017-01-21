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

unsigned int fig_num; //metapost figure number (the "1" in "beginfig(1)" for example)

int density; //points per inch for bitmap. changes when we zoom in

//create a new mp file from the original, with commands added to save the coords and override outputtemplate and prologues
int create_mp_file(char *job_name_in, char *job_name_out);
//run metapost to create ps/pdf file
int run_mpost(char *job_name);
//get the coordinates of the lower left corner of the image, which are written in job_name.log
int get_coords(char *job_name);
//convert the ps/pdf to a raster image, creates job_name.xbm
int make_bitmap(char *job_name);
//convert the ps/pdf to a raster image, creates job_name.png
int make_png(char *job_name);

#endif
