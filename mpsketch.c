#include <X11/Xlib.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <string.h>

#include "common.h"

#define _NET_WM_STATE_ADD 1
#define SUBINTERVALS 20 //how many straight sections to make up the bezier curve connecting two points

/*
TODO: 
do spaces in jobname stuff up the xbm?
include instructions on how to get mplib in README
	also link to mpman: https://www.tug.org/docs/metapost/mpman.pdf
if pushing a path outside current view, scroll to it
move a vee
edit point to v
create mp file if it doesn't exist
make points visible against black
arrow keys to scroll
shift or scale path
centre diagram on screen. also when zooming, zoom into centre
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

Pixmap trace_bitmap;
void get_trace();
unsigned int trace_width,trace_height;

bool quit=false;
bool help=true; //show help message

void button_release(short x,short y,int button); //do stuff when mouse button is pressed
void keypress(int keycode,int state); //do stuff when a key is pressed
void move_point(); //when you click and drag a point on a path/circle

void show_help(); //display message about usage
void show_msg(int pos,char *msg); //display message

void draw_bezier(double start_x, double start_y, double start_right_x, double start_right_y, double end_left_x, double end_left_y, double end_x, double end_y); //draw the cubic bezier curve connecting two points

void output_path();//print the path and copy to clipboard
void refresh(); //rerun metapost if necessary, otherwise just rerun convert (if metapost has already been run from elsewhere).

void mode_change() {
}

void draw_circle(double centre_x, double centre_y, int r) {
		XDrawArc(d,w,gc, mp_x_coord_to_pxl(centre_x) - r, mp_y_coord_to_pxl(centre_y) - r, 2*r, 2*r, 0, 360*64);
}
void draw_point(double centre_x, double centre_y) {
		draw_circle(centre_x,centre_y,POINT_RADIUS);
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
	int x = sketch_width/2 - box_width/2;
	int y = sketch_height/2 - box_height/2;
	XSetForeground(d,gc,WhitePixel(d, s));
	XFillRectangle(d,w,gc,x,y,box_width,box_height);
	XSetForeground(d,gc,BlackPixel(d, s));
	XDrawRectangle(d,w,gc,x,y,box_width,box_height);
	XDrawString(d, w, gc, x+10, sketch_height/2, msg, strlen(msg));
	XFlush(d);
}
void error() {
	box_msg("Error. See stdout for details.");
}

int get_bitmap(char *filename, Display *d, Window w, Pixmap *bitmap, unsigned int *bitmap_width, unsigned int *bitmap_height) {
	int hotspot_x, hotspot_y;
	int ret = XReadBitmapFile(d, w,
							 filename,
							 bitmap_width, bitmap_height,
							 bitmap,
							 &hotspot_x, &hotspot_y);
	if (ret != BitmapSuccess) {
		switch (ret) {
			case BitmapOpenFailed:
				fprintf(stderr, "XReadBitmapFile - could not open file '%s'.\n",filename);
				break;
			case BitmapFileInvalid:
				fprintf(stderr, "XReadBitmapFile - file '%s' doesn't contain a valid bitmap.\n", filename);
				break;
			case BitmapNoMemory:
				fprintf(stderr, "XReadBitmapFile - not enough memory.\n");
				break;
		}
	}
	return ret;
}

void zoom() { //remake the bitmap after density change
	char filename[strlen(tmp_job_name) + 5];
	sprintf(filename,"%s.xbm",tmp_job_name);

	if (make_bitmap(tmp_job_name) == 0 && get_bitmap(filename,d,w,&bitmap,&bitmap_width,&bitmap_height) == 0) {
		remove(filename);
		redraw_screen();
	} else error();
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
	show_trace = false;
	int c;
	unit_name = "";
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
					/*if (strlen(optarg) > sizeof(unit_name)-1) {
						puts("Unit name too long");
						return 1;
					}*/
					unit_name = optarg;
				} else {
					unit = string_to_bp(optarg,&valid);
					if (isalpha(optarg[0]))
						//strcat(unit_name,optarg);
						unit_name = optarg;
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

	density = 100;
	pixels_per_point = density/INCH;

	if (argc > optind) mp_filename = argv[optind];
	else mp_filename = "test.mp";
	if (argc > optind+1) {
		fig_num = atoi(argv[optind+1]);
	} else fig_num = 1;

	initialise();

	sketch_width = 400;
	sketch_height = 400;
	
	XEvent e;
	
	d = XOpenDisplay(NULL);
	if (d == NULL) {
		fprintf(stderr, "Cannot open display\n");
		exit(1);
	}
	
	s = DefaultScreen(d);
	w = XCreateSimpleWindow(d, RootWindow(d, s), 0, 0, sketch_width, sketch_height, 1, BlackPixel(d, s), WhitePixel(d, s));

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

	show_help();
	XFlush(d);
	refresh();

	while (!quit) {
		XNextEvent(d, &e);
		switch(e.type) {
		case Expose: //on startup, and when window is resized, workspace switched
			;Window rt; int x,y; unsigned int bw,dpth;
			XGetGeometry(d,w,&rt,&x,&y,&sketch_width,&sketch_height,&bw,&dpth);
			//x_offset = -ll_x/INCH*density - sketch_width/2;
			//y_offset = ll_y/INCH*density + sketch_height/2;
			redraw_screen();
			break;
		case ButtonPress:
			if (edit) dragging_point=true;
			break;
		case ButtonRelease:
			button_release(e.xbutton.x,e.xbutton.y,e.xbutton.button);
			break;
		case MotionNotify:
			if (!help) pointer_move((int) e.xmotion.x,(int) e.xmotion.y);
			break;
		case KeyPress:
			if (!dragging_point) keypress(e.xkey.keycode,e.xkey.state);
			break;
		case ClientMessage:
			if ((Atom)e.xclient.data.l[0] == wm_delete_window)
				quit=true;
			break;
		}
	}

	cleanup();

	XCloseDisplay(d);
	return 0;
}
void show_msg(int pos,char *msg) {
	XDrawString(d, w, gc, 10, 20*pos, msg, strlen(msg));
}
void show_help() {
	XSetForeground(d,gc,WhitePixel(d, s));
	XFillRectangle(d,w,gc,0,0,sketch_width,sketch_height);
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
		if (edit && edit_point >= 0) {
			remove_point(edit_point);
			edit=false;
			redraw_screen();
		}
		break;
	case 42://i - insert a point before current point
		if (edit && edit_point >= 0) {
			point_before(edit_point);
			edit_point++;
			redraw_screen();
		}
		break;
	case 38://a - insert a point after current point
		if (edit && edit_point >= 0) {
			point_before(edit_point+1);
			redraw_screen();
		}
		break;
	case 26://period
		path_mode_change(false);
		break;
	case 48://dash
		path_mode_change(true);
		break;
	case 27: //p - push path string onto the screen
		get_path_from_clipboard();
		break;
	case 32://r
		refresh();
		break;
	case 41://u - undo creating a point
		undo();
		break;
	case 61://z - zoom
		if (state & ShiftMask) density/=2; //shift-z zooms out
		else density*=2;
		pixels_per_point = density/INCH;
		zoom();
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

/*
//Old version of what happens when the user clicks and drags a point on a path. Use dragging_point variable instead now because it works across xlib, gtk, etc
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
}*/

void button_release(short x, short y, int button) {
	if (button == 1) {
		click_point((int) x, (int) y);
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
		run_mpost(job_name);
	} else {
		get_coords(job_name);
	}*/
	if (!help) box_msg("Running metapost...");

	char tmp_filename[strlen(tmp_job_name)+4];
	sprintf(tmp_filename,"%s.mp",tmp_job_name);
	int ret = create_mp_file(mp_filename,tmp_filename);
	if (ret != 0) {
		error();
		if (ret == 1) printf("Couldn't open %s\n",mp_filename);
		else if (ret == 2) printf("Couldn't open %s.mp for writing\n",tmp_job_name);
	} else if (run_mpost(tmp_job_name) != 0 || get_coords(tmp_job_name) != 0) {
		error();
	} else {
		if (!help) box_msg("Creating raster image...");
		char filename[strlen(tmp_job_name) + 5];
		sprintf(filename,"%s.xbm",tmp_job_name);
		if (make_bitmap(tmp_job_name) != 0 || get_bitmap(filename,d,w,&bitmap,&bitmap_width,&bitmap_height) != 0) {
			error();
		} else {
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

void redraw_screen() {
	if (help) {
		show_help();
	} else {
		//Clear the screen
		XSetForeground(d,gc,WhitePixel(d, s));
		XFillRectangle(d,w,gc,0,0,sketch_width,sketch_height);
		XSetForeground(d,gc,BlackPixel(d, s));

		//copy the bitmap to the screen
		if (!bitmap) {
			error();
		} else {
			XCopyPlane(d, bitmap, w, gc,
				x_offset, y_offset + bitmap_height - sketch_height,
				sketch_width, sketch_height,
				0, 0,
				1);
		}

		if (show_trace && trace_bitmap) {
			XCopyPlane(d, trace_bitmap, w, gc,
				0, 0,
				trace_width, trace_height,
				mp_x_coord_to_pxl(trace_x), 
				mp_y_coord_to_pxl(trace_y),
				1);
			//draw a point at top left of trace so we can drag it to move the trace
			draw_point(trace_x, trace_y);
			//make a border around the trace so it's clear the point goes with the trace
			XDrawRectangle(d,w,gc,
				mp_x_coord_to_pxl(trace_x),
				mp_y_coord_to_pxl(trace_y),
				trace_width,
				trace_height);
		}

		draw_path();
		highlight_edit_point();
	}
}

void get_trace(char *filename) {
	if (get_bitmap(filename,d,w,&trace_bitmap,&trace_width,&trace_height) != 0) show_trace = false;
}
