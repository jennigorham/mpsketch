#include <X11/Xlib.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <math.h>

#include "paths.h"
#include "mptoraster.h"

#define _NET_WM_STATE_ADD 1
#define SUBINTERVALS 20 //how many straight sections to make up the bezier curve connecting two points
#define XBM_FILENAME "mp-drawing.xbm"
#define POINT_RADIUS 3 //for drawing a little circle around each point on the path
#define SCROLL_STEP 10 //How many pixels to scroll at a time

#define CURVE_MODE 0
#define STRAIGHT_MODE 1
#define CORNER_MODE 2
#define CIRCLE_MODE 3

/*
TODO: 
port to gtk+
take jobname.mp as first arg, rather than jobname
do spaces in jobname stuff up the xbm?
could create a new mp file by concatenating the save_coords macro and the source mp file then delete it (and the ps and log files) afterwards. that way the user wouldn't need to "input mpsketch-coords;"
include instructions on how to get mplib in README
	also link to mpman: https://www.tug.org/docs/metapost/mpman.pdf
if pushing a path outside current view, scroll to it
move a vee
edit point to v
draw_path should work for circles too
create mp file if it doesn't exist
make points visible against black
arrow keys to scroll
shift or scale path
centre diagram on screen. also when zooming, zoom into centre
make fig_num int? then how to handle zero-padding?
remember past paths. key to clear paths. read in all paths in metapost file
create program that takes in path string, outputs control points, so a gui could be made in a scripting language that calls it
consider generating ps internally rather than calling mpost. see section 2.2 of mplibapi.pdf
config file to change keybindings?

integration with vim: named pipe? 
can read in lines from pipe into vim using :r !cat pipe
vim can tell mpsketch to read path in by calling program that triggers XClientMessage, simulates keypress (p to read path from clipboard, r to redraw)
When editing path already in mp file, could output replacement command, eg ':%s/(32,-17)\.\.(-106,85)/(31,-10)..(-130,85)/g'
*/

Display *d;
Window w;
GC gc;
int s; //screen

Pixmap bitmap; //metapost output, converted to bitmap
unsigned int bitmap_width, bitmap_height;

Pixmap tracing_bitmap;
int trace_x_offset,trace_y_offset;
void get_trace();
bool show_trace = false;

bool quit=false;
bool help=true; //show help message
int mode=CURVE_MODE; //default drawing mode
bool finished_drawing=true; //if true then we're ready to start drawing another path or circle.

char *job_name; //The part of the metapost filename before ".mp"
char *fig_num; //metapost figure number (the "1" in "beginfig(1)" for example). Should be string rather than int because sometimes figure numbers are zero-padded, depending on the output template

int density = 100; //points per inch for bitmap. changes when we zoom in
int x_offset=0; //for scrolling
int y_offset=0;
unsigned int win_height = 600;
unsigned int win_width = 800;

// metapost coords of lower left corner of image
float ll_x = 0;
float ll_y = 0;

//when we mouse over a point in a completed path, we can edit it
bool edit=false;
int edit_point; //which point are we editing

void button_release(short x,short y,int button); //do stuff when mouse button is pressed
void keypress(int keycode,int state); //do stuff when a key is pressed
void pointer_move(short x,short y);
void move_point(); //when you click and drag a point on a path/circle

void show_help(); //display message about usage
void show_msg(int pos,char *msg); //display message

void redraw_screen(); //draw bitmap on screen, then draw the path
void draw_path();
void link_point_pair(struct point *p, struct point *q); //draw either a straight line or a bezier curve linking two consecutive points on a path
double bezier(double start, double start_right, double end_left, double end, double t);
void draw_bezier(double start_x, double start_y, double start_right_x, double start_right_y, double end_left_x, double end_left_y, double end_x, double end_y); //draw the cubic bezier curve connecting two points

void output_path();//print the path and copy to clipboard
void refresh(); //rerun metapost if necessary, otherwise just rerun convert (if metapost has already been run from elsewhere).

//Converting between metapost coords and xlib coords
int mp_x_coord_to_pxl(double x) {
	return round((x - ll_x)/INCH*density - x_offset);
}
int mp_y_coord_to_pxl(double y) {
	return round(win_height - y_offset - (y - ll_y)/INCH*density);
}
double pxl_to_mp_x_coord(int x) {
	return ((x_offset + ((float) x))*INCH/density + ll_x);
}
double pxl_to_mp_y_coord(int y) {
	return ((-y_offset + win_height-((float) y))*INCH/density + ll_y);
}

void draw_circle(double centre_x, double centre_y, int r) {
		XDrawArc(d,w,gc, mp_x_coord_to_pxl(centre_x) - r, mp_y_coord_to_pxl(centre_y) - r, 2*r, 2*r, 0, 360*64);
}
void fill_circle(double centre_x, double centre_y, int r) {
		XFillArc(d,w,gc, mp_x_coord_to_pxl(centre_x) - r, mp_y_coord_to_pxl(centre_y) - r, 2*r, 2*r, 0, 360*64);
}

void get_path_from_clipboard() {
	char buffer[1000];
	FILE *clipboard = popen("xsel -b -o", "r");
	fgets(buffer, sizeof(buffer), clipboard);
	pclose(clipboard);

	/*if (!finished_drawing) {
		output_path();
	}*/

	string_to_path(buffer);
	finished_drawing = true;
	redraw_screen();
}

void box_msg(char *msg) {
	int box_width = 250;
	int box_height = 50;
	int x = win_width/2 - box_width/2;
	int y = win_height/2 - box_height/2;
	XSetForeground(d,gc,WhitePixel(d, s));
	XFillRectangle(d,w,gc,x,y,box_width,box_height);
	XSetForeground(d,gc,BlackPixel(d, s));
	XDrawRectangle(d,w,gc,x,y,box_width,box_height);
	XDrawString(d, w, gc, x+10, win_height/2, msg, strlen(msg));
}
void error() {
	box_msg("Error. See stdout for details.");
}

//Find X(t) or Y(t) given coords of two points on curve and control points between them
double bezier (double start, double start_right, double end_left, double end, double t) {
	return (1-t)*(1-t)*(1-t)*start + 3*t*(1-t)*(1-t)*start_right + 3*t*t*(1-t)*end_left + t*t*t*end;
}
//draw the cubic bezier curve connecting two points
void draw_bezier (double start_x, double start_y, double start_right_x, double start_right_y, double end_left_x, double end_left_y, double end_x, double end_y) {
	int i;
	int x,y,nextx,nexty;
	x = mp_x_coord_to_pxl(start_x); y = mp_y_coord_to_pxl(start_y);
	for (i=1;i<SUBINTERVALS + 1;i++) {
		nextx = mp_x_coord_to_pxl(bezier(start_x,start_right_x,end_left_x,end_x,i*1.0/SUBINTERVALS));
		nexty = mp_y_coord_to_pxl(bezier(start_y,start_right_y,end_left_y,end_y,i*1.0/SUBINTERVALS));
		XDrawLine(d, w, gc, x,y,nextx, nexty);
		x = nextx; y = nexty;
	}
}

void copy_to_clipboard(char* s) {
	char cmd[strlen(s) + strlen("echo -n \"\" | xsel -i -b") + 1]; 
	sprintf(cmd,"echo -n \"%s\" | xsel -i -b",s);
	system(cmd);
}

int main(int argc, char **argv) {
	unit = 1; //default is to use units of postscript points (1/72 inches). 
	char *trace_filename;
	int c;
	strcpy(unit_name,"");
	while ((c = getopt (argc, argv, "u:t:")) != -1)
		switch (c)
		{
			case 'u': //units
				;
				/*Examples of usage:
				u=5cm //print coords as '(2u,3u)' for example (meaning (10cm,15cm))
				u=cm
				5cm //print coords as (2,3) to mean (10cm,15cm)
				u=72 //72 postscript points = 1 inch, so print '(2u,3u)' to mean (2in,3in)
				72 //print '(2,3)' to mean (2in,3in)
				mm //equivalent to mm=1mm
				*/

				bool valid;
				//check for '=' in optarg
				char *num = strchr(optarg,'=');
				if (num) {
					//take everything before the '=' as the unit_name
					*num = '\0';
					unit = string_to_bp(num+1,&valid); //need to do this before setting unit_name, because str_to_bp uses unit_name and an optarg like 'u=5u' shouldn't be valid
					if (strlen(optarg) > sizeof(unit_name)-1) {
						puts("Unit name too long");
						return 1;
					}
					strcat(unit_name,optarg);
				} else {
					unit = string_to_bp(optarg,&valid);
					if (isalpha(optarg[0]))
						strcat(unit_name,optarg);
				}

				if (!valid) {
					puts("Invalid measure in argument of -u");
					puts("Usage: mpsketch -u [<unitname>=]<number>[cm|mm|in|pt|bp]");
					puts("e.g. mpsketch -u u=2cm");
					return 1;
				}
				break;
			case 't':
				show_trace = true;
				trace_filename = optarg;
				break;
			case '?':
				//TODO: print usage message
				return 1;
			default:
				abort ();
		}
	coord_precision = ceil(log10(unit)); //choose a sensible number of decimal places for coordinates

	if (argc > optind) job_name = argv[optind];
	else job_name = "test";
	if (argc > optind+1) {
		fig_num = argv[optind+1];
	} else fig_num = "1";
	//TODO: create job_name.mp if it doesn't exist
	
	XEvent e;

	cur_path = malloc(sizeof(struct path));
	init_path(cur_path);
	
	d = XOpenDisplay(NULL);
	if (d == NULL) {
		fprintf(stderr, "Cannot open display\n");
		exit(1);
	}
	
	s = DefaultScreen(d);
	w = XCreateSimpleWindow(d, RootWindow(d, s), 0, 0, win_width, win_height, 1, BlackPixel(d, s), WhitePixel(d, s));

	gc = DefaultGC(d,s);
	XSelectInput(d, w, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask); //Inputs we want to handle.
	XMapWindow(d, w);
	XStoreName(d,w,"MetaPost Sketch"); //Window title

	XDefineCursor(d,w,XCreateFontCursor(d,86)); //pencil

	//need to handle the user closing the window otherwise we get errors. See ClientMessage case below
	Atom wm_delete_window = XInternAtom(d, "WM_DELETE_WINDOW", False); 
	XSetWMProtocols(d, w, &wm_delete_window, 1);

	//load picture to trace over
	if (show_trace)
		get_trace(trace_filename);

	refresh();

	while (!quit) {
		XNextEvent(d, &e);
		switch(e.type) {
		case Expose: //on startup, and when window is resized, workspace switched
			;Window rt; int x,y; unsigned int bw,dpth;
			XGetGeometry(d,w,&rt,&x,&y,&win_width,&win_height,&bw,&dpth);
			//x_offset = -ll_x/INCH*density - win_width/2;
			//y_offset = ll_y/INCH*density + win_height/2;
			redraw_screen();
			break;
		case ButtonPress:
			if (edit) move_point();
			break;
		case ButtonRelease:
			button_release(e.xbutton.x,e.xbutton.y,e.xbutton.button);
			break;
		case MotionNotify:
			pointer_move(e.xmotion.x,e.xmotion.y);
			break;
		case KeyPress:
			keypress(e.xkey.keycode,e.xkey.state);
			break;
		case ClientMessage:
			if ((Atom)e.xclient.data.l[0] == wm_delete_window)
				quit=true;
			break;
		}
	}
	free(cur_path);

	XCloseDisplay(d);
	return 0;
}

void end_path() {
	cur_path->n--; //remove the extra point under the cursor. for circles, sets n to -1 so we know it's a circle
	output_path();
	finished_drawing=true;
	redraw_screen();
}
void show_msg(int pos,char *msg) {
	XDrawString(d, w, gc, 10, 20*pos, msg, strlen(msg));
}
void show_help() {
	XSetForeground(d,gc,WhitePixel(d, s));
	XFillRectangle(d,w,gc,0,0,win_width,win_height);
	XSetForeground(d,gc,BlackPixel(d, s));
	int pos = 1;
	show_msg(pos++,"MPSketch: a MetaPost GUI");
	show_msg(pos++,"This program does not edit an mp file, but instead copies MetaPost paths to stdout and the clipboard");
	show_msg(pos++,"so you can add them to your mp file.");
	pos++;

	show_msg(pos++,"Draw a path by clicking at points on the path. Escape will end the path, Enter will make it a cycle.");
	show_msg(pos++,"Press . for curve mode (default), press - for straight lines, press v for a corner in a curved path.");
	show_msg(pos++,"Pressing u will undo the last point.");
	pos++;

	show_msg(pos++,"Press c for circle mode. Click at the centre of the circle, then the edge."); 
	pos++;

	show_msg(pos++,"After a path, circle, or point has been drawn, you can edit it by dragging the little circles.");
	show_msg(pos++,"To delete a point, mouse over it, then press d.");
	show_msg(pos++,"Press a to insert a point after the selected point, or i to insert it before.");
	show_msg(pos++,"Once you're happy with the path, press y (\"yank\" the path) to output the path to clipboard and stdout."); 
	show_msg(pos++,"You can copy a path from your mp file to the clipboard, then edit it by pressing p (\"push\" a path)."); 
	pos++;

	show_msg(pos++,"Press q to quit, r to redraw the metapost file, ? to show this help message.");
	show_msg(pos++,"Press h, l, k, or j to scroll left, right, up, or down, respectively. Or use scrollwheel.");
	show_msg(pos++,"Press z or shift-z to zoom in or out. This will be slow.");

	show_msg(++pos,"Press spacebar to continue.");
}
void keypress(int keycode,int state) {
	//printf("Keypress: %d\n",keycode);
	switch(keycode) {
	case 53://q
		quit=true;
		break;
	case 45://t
		show_trace = !show_trace;
		redraw_screen();
		break;
	case 66://Escape
		if (finished_drawing) { //clear
			cur_path->n = 0;
			redraw_screen();
		} else
			end_path();
		break;
	case 36://Return
		if (!finished_drawing) {
			cur_path->cycle = true;
			end_path();
		} else if (edit) {
			cur_path->cycle = !cur_path->cycle;
			redraw_screen();
		}
		break;
	case 43://d - delete a point
		if (edit) {
			remove_point(edit_point);
			edit=false;
			redraw_screen();
		}
		break;
	case 42://i - insert a point before current point
		if (edit && edit_point > 0) {
			find_control_points();
			struct point p = cur_path->points[edit_point-1];
			struct point q = cur_path->points[edit_point];
			insert_point(edit_point,
				bezier(p.x,p.right_x,q.left_x,q.x,0.5),
				bezier(p.y,p.right_y,q.left_y,q.y,0.5),
				p.straight
			);
			edit_point++;
			redraw_screen();
		}
		break;
	case 38://a - insert a point after current point
		if (edit && edit_point < cur_path->n-1) {
			find_control_points();
			struct point p = cur_path->points[edit_point];
			struct point q = cur_path->points[edit_point+1];
			insert_point(edit_point+1,
				bezier(p.x,p.right_x,q.left_x,q.x,0.5),
				bezier(p.y,p.right_y,q.left_y,q.y,0.5),
				p.straight
			);
			redraw_screen();
		}
		break;
	case 26://period
		mode=CURVE_MODE;
		if (!finished_drawing) set_straight(cur_path->n-2,false);
		else if (edit) set_straight(edit_point,false);
		redraw_screen();
		break;
	case 48://dash
		mode=STRAIGHT_MODE;
		if (!finished_drawing) set_straight(cur_path->n-2,true);
		else if (edit) set_straight(edit_point,true);
		redraw_screen();
		break;
	case 27: //p - push path string onto the screen
		get_path_from_clipboard();
		break;
	case 32://r
		refresh();
		break;
	case 41://u - undo creating a point
		if (!finished_drawing && cur_path->n > 0) {
			cur_path->n--;
			if (cur_path->n == 0) finished_drawing = true;
			redraw_screen();
		}
		break;
	case 61://z - zoom
		if (state & ShiftMask) density/=2; //shift-z zooms out
		else density*=2;
		if (make_bitmap(job_name,fig_num,density,XBM_FILENAME) == 0 && get_bitmap(XBM_FILENAME,d,w,&bitmap,&bitmap_width,&bitmap_height) == 0) {
			remove(XBM_FILENAME);
			redraw_screen();
		} else error();
		break;
	case 28://y - yank path string
		output_path();
		break;
	case 65://Space
		help=false;
		redraw_screen();
		break;
	case 33://l - scroll right
		x_offset +=SCROLL_STEP;
		redraw_screen();
		break;
	case 44://h - scroll left
		x_offset -=SCROLL_STEP;
		redraw_screen();
		break;
	case 54://j - scroll down
		y_offset +=SCROLL_STEP;
		redraw_screen();
		break;
	case 55://k - scroll up
		y_offset -=SCROLL_STEP;
		redraw_screen();
		break;
	case 34://?
		help=true;
		show_help();
		break;
	case 31://c - draw a circle
		if (!finished_drawing) {
			end_path();
		}
		mode=CIRCLE_MODE;
		break;
	case 60://v
		mode=CORNER_MODE;
	}
}

void move_point() {
	XEvent e;
	XNextEvent(d, &e);
	while (e.type != ButtonRelease) {
		if (e.type == MotionNotify) {
			set_coords(
				edit_point,
				pxl_to_mp_x_coord(e.xmotion.x),
				pxl_to_mp_y_coord(e.xmotion.y)
			);
			redraw_screen();
		}
		XNextEvent(d, &e);
	}
}

void output_path() {
	char *s = path_to_string();
	copy_to_clipboard(s);
	if (cur_path->n > 1 || cur_path->n == -1)
		printf("draw %s;\n",s);
	else if (cur_path->n == 1)
		printf("drawdot %s;\n",s);
	free(s);
}

void button_release(short x, short y,int button) {
	if (button == 1) {
		if (mode == CIRCLE_MODE) {
			if (finished_drawing) { //start a new circle
				cur_path->n = 0;
				set_coords(0,pxl_to_mp_x_coord(x),pxl_to_mp_y_coord(y));
				finished_drawing = false;
			} else {
				finished_drawing=true;
				cur_path->n = -1;
				output_path();
			}
		} else {
			if (finished_drawing) {//start a new path
				cur_path->cycle = false;
				finished_drawing = false;
				cur_path->n = 1;
			} 
			set_last_point(
				pxl_to_mp_x_coord(x),
				pxl_to_mp_y_coord(y),
				mode!=CURVE_MODE
			);
			if (mode == CORNER_MODE) {
				mode = CURVE_MODE;
				append_point(
					pxl_to_mp_x_coord(x),
					pxl_to_mp_y_coord(y),
					false
				);
			}
			redraw_screen();
			//point under cursor
			append_point(
				pxl_to_mp_x_coord(x),
				pxl_to_mp_y_coord(y),
				false
			);
		}
	} else if (button == 4) {
		y_offset -=SCROLL_STEP;
		redraw_screen();
	} else if (button == 5) {
		y_offset +=SCROLL_STEP;
		redraw_screen();
	} else if (button == 6) {
		x_offset -=SCROLL_STEP;
		redraw_screen();
	} else if (button == 7) {
		x_offset +=SCROLL_STEP;
		redraw_screen();
	}
}

void refresh() {
	//run mpost unless the output file is up-to-date. I've commented this out because if they run mptopdf rather than mpost then the coords won't be in the log file
	/*struct stat pic_attrib;
	struct stat mp_attrib;
	char mp_filename[strlen(job_name)+4];
	char pic_filename[strlen(job_name) + strlen(fig_num) + 6];
	sprintf(mp_filename,"%s.mp",job_name);
	if (USE_MPTOPDF)
		sprintf(pic_filename,"%s-%s.pdf",job_name,fig_num);
	else
		sprintf(pic_filename,"%s.%s",job_name,fig_num);

	stat(pic_filename, &pic_attrib);
	stat(mp_filename, &mp_attrib);
	if (difftime(mp_attrib.st_ctime, pic_attrib.st_ctime) > 0) {
		run_mpost();
	} else {
		get_coords();
	}*/
	box_msg("Running metapost...");
	XFlush(d); //Xlib won't update the screen while metapost runs unless we do this
	if (run_mpost(job_name) != 0 || get_coords(job_name,fig_num,&ll_x, &ll_y) != 0) {
		error();
	} else {
		box_msg("Creating raster image...");
		XFlush(d);
		if (make_bitmap(job_name,fig_num,density,XBM_FILENAME) != 0 || get_bitmap(XBM_FILENAME,d,w,&bitmap,&bitmap_width,&bitmap_height) != 0) {
			error();
		} else {
			remove(XBM_FILENAME);
			redraw_screen();
		}
	}
}

void link_point_pair(struct point *p, struct point *q) {
	if (p->straight) {
		XDrawLine(d, w, gc, 
			mp_x_coord_to_pxl(p->x),
			mp_y_coord_to_pxl(p->y),
			mp_x_coord_to_pxl(q->x),
			mp_y_coord_to_pxl(q->y)
		);
	} else {
		draw_bezier(
			p->x,
			p->y,
			p->right_x,
			p->right_y,
			q->left_x,
			q->left_y,
			q->x,
			q->y
		);
	}
}
void draw_path() {
	find_control_points();
	
	int i;
	struct point *p,*q;
	for (i=0; i<cur_path->n-1; i++) {
		p = &cur_path->points[i];
		q = &cur_path->points[i+1];
		draw_circle(p->x,p->y, POINT_RADIUS);
		link_point_pair(p,q);
	}
	p = &cur_path->points[cur_path->n-1];
	draw_circle(p->x,p->y, POINT_RADIUS);
	if (cur_path->cycle) {
		q = &cur_path->points[0];
		link_point_pair(p,q);
	}
}

void redraw_screen() {
	if (help) {
		show_help();
	} else {
		//Clear the screen
		XSetForeground(d,gc,WhitePixel(d, s));
		XFillRectangle(d,w,gc,0,0,win_width,win_height);
		XSetForeground(d,gc,BlackPixel(d, s));

		//copy the bitmap to the screen
		if (!bitmap) {
			error();
		} else {
			XCopyPlane(d, bitmap, w, gc,
				x_offset, y_offset + bitmap_height - win_height,
				win_width, win_height,
				0, 0,
				1);
		}
		
		if (show_trace) {
			XCopyPlane(d, tracing_bitmap, w, gc,
				trace_x_offset, trace_y_offset,
				win_width, win_height,
				0, 0,
				1);
		}
		//draw the current path
		if (cur_path->n > 1) draw_path();
		else if (cur_path->n == 1 && finished_drawing)
			draw_circle(cur_path->points[0].x,cur_path->points[0].y,POINT_RADIUS); 
		else if ((mode == CIRCLE_MODE && !finished_drawing) || cur_path->n == -1) {//a circle is being drawn or has just been drawn
			double delta_x,delta_y, r;
			delta_x = cur_path->points[0].x - cur_path->points[1].x;
			delta_y = cur_path->points[0].y - cur_path->points[1].y;
			r = sqrt(delta_x*delta_x + delta_y*delta_y) / INCH * density * unit;

			draw_circle(cur_path->points[0].x,cur_path->points[0].y,(int) r);
			draw_circle(cur_path->points[0].x,cur_path->points[0].y,POINT_RADIUS);
			draw_circle(cur_path->points[1].x,cur_path->points[1].y,POINT_RADIUS);
		}

		if (edit)
			fill_circle(cur_path->points[edit_point].x,cur_path->points[edit_point].y,POINT_RADIUS);
	}
}

void pointer_move(short x,short y) {
	if (!finished_drawing) {
		if (mode == CIRCLE_MODE)
			set_coords(1,pxl_to_mp_x_coord(x),pxl_to_mp_y_coord(y));
		else
			set_coords(cur_path->n-1,pxl_to_mp_x_coord(x),pxl_to_mp_y_coord(y));
		redraw_screen();
	} else if (!help) {
		//if the user mouses over a point on the path, they can edit it (drag it, change the section after it to straight/curved)
		int i;
		edit=false;
		for (i=0;i<(cur_path->n == -1 ? 2 : cur_path->n);i++) {
			int delta_x, delta_y;
			delta_x = mp_x_coord_to_pxl(cur_path->points[i].x) - x;
			delta_y = mp_y_coord_to_pxl(cur_path->points[i].y) - y;
			if (delta_x*delta_x + delta_y*delta_y < POINT_RADIUS*POINT_RADIUS) {
				edit=true;
				edit_point=i;
				break;
			}
		}
		redraw_screen();
	}
}

void get_trace(char *filename) {
	unsigned int trace_width,trace_height;
	if (get_bitmap(filename,d,w,&tracing_bitmap,&trace_width,&trace_height) != 0) show_trace = false;
}
