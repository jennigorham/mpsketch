// gcc -Wall mpsketch3.c paths.c -L/usr/X11R6/lib -lX11 -L . -l mplib -l kpathsea -l mputil -lcairo -lpixman -lpng -lz -lmpfr -lgmp -lm -o mpsketch
#include <X11/Xlib.h>
#include <X11/Xutil.h>		/* BitmapOpenFailed, etc. */
#include <time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <math.h>

#include "paths.h"

#define _NET_WM_STATE_ADD 1
#define SUBINTERVALS 20 //how many straight sections to make up the bezier curve connecting two points
#define SCALE 72.0 //metapost units are 1/72 of an inch
#define XBM_FILENAME "mp-drawing.xbm"
#define USE_MPTOPDF false //whether to use mptopdf or mpost. The advantage of using mpost is that we can run it in nonstopmode and therefore display an error message onscreen when it fails (maybe this is possible with mptopdf but I don't know how). The advantage of using mptopdf is that we don't need "prologues:=3" in the mp file
#define POINT_RADIUS 3 //for drawing a little circle around each point on the path
#define SCROLL_STEP 10 //How many pixels to scroll at a time

#define CURVE_MODE 0
#define STRAIGHT_MODE 1
#define CORNER_MODE 2
#define CIRCLE_MODE 3

/*
TODO: 
don't draw_bezier for straight sections
move a vee
end_path should work for circles too. same with draw_path
y and p (yank and push) rather than Enter and i
fix offset adjustment on expose
create mp file if it doesn't exist.
make points visible against black.
arrow keys to scroll
port to gtk+
centre diagram on screen. also when zooming, zoom into centre
make fig_num int?
remember past paths. key to clear paths. read in all paths in metapost file
create program that takes in path string, outputs control points, so a gui could be made in a scripting language that calls it
consider generating ps internally rather than calling mpost. see section 2.2 of mplibapi.pdf
config file to change keybindings

split into multiple files:
	gui-related parts: event handling, displaying error messages, drawing circles and lines, convert mp coords to pixels, clipboard, move_knot, trace
	refresh/run mpost/get_coords/convert

getopt:
tracing mode
option for copy path only vs include "draw ...;"
pass in scale, precision
pass in filename (to support non-default outputtemplate)
mpsketch -f filename -s scale -p precision job_name fignum

path editing: add points, remove point
i to add point before, a for after. how to do "point t of p" in mplib?

integration with vim: named pipe? 
can read in lines from pipe into vim using :r !cat pipe
vim can tell mpsketch to read path in by calling program that triggers XClientMessage, simulates keypress (i to read path from clipboard, r to redraw)
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
bool show_trace;

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
int edit_point; //which knot are we editing

void button_release(short x,short y,int button); //do stuff when mouse button is pressed
void keypress(int keycode,int state); //do stuff when a key is pressed
void pointer_move(short x,short y);
void move_knot(); //when you click and drag a point on a path/circle

void show_help(); //display message about usage
void show_msg(int pos,char *msg); //display message

void redraw_screen(); //draw bitmap on screen, then draw the path
void draw_path();
double bezier(double start, double start_right, double end_left, double end, double t);
void draw_bezier(double start_x, double start_y, double start_right_x, double start_right_y, double end_left_x, double end_left_y, double end_x, double end_y); //draw the cubic bezier curve connecting two points
void output_path();//print the path and copy to clipboard
void refresh(); //rerun metapost if necessary, otherwise just rerun convert (if metapost has already been run from elsewhere).

//Converting between metapost coords and xlib coords
int mp_x_coord_to_pxl(double x) {
	return round((x - ll_x)/SCALE*density - x_offset);
}
int mp_y_coord_to_pxl(double y) {
	return round(win_height - y_offset - (y - ll_y)/SCALE*density);
}
double pxl_to_mp_x_coord(int x) {
	return (x_offset + ((float) x))*SCALE/density + ll_x;
}
double pxl_to_mp_y_coord(int y) {
	return (-y_offset + win_height-((float) y))*SCALE/density + ll_y;
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

	get_path(buffer);
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

/*void mp_dump_solved_path (MP mp, mp_knot h) { //from https://www.tug.org/metapost/src/manual/mplibapi.pdf, modified to draw the path
	mp_knot p, q;
	if (h == NULL) return;
	p = h;
	do {
		draw_circle(
			mp_number_as_double(mp,p->x_coord),
			mp_number_as_double(mp,p->y_coord),
			POINT_RADIUS
		);
		q=mp_knot_next(mp,p);
		if ( q!=h || h->data.types.left_type!=mp_endpoint) {
			 draw_bezier(
					  mp_number_as_double(mp,p->x_coord),
					  mp_number_as_double(mp,p->y_coord),
					  mp_number_as_double(mp,p->right_x),
					  mp_number_as_double(mp,p->right_y),
					  mp_number_as_double(mp,q->left_x),
					  mp_number_as_double(mp,q->left_y),
					  mp_number_as_double(mp,q->x_coord),
					  mp_number_as_double(mp,q->y_coord)
			 );
		}
		p=q;
		if ( p!=h || h->data.types.left_type!=mp_endpoint) {
		}
	} while (p!=h);
}*/

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

void make_bitmap() {
	//TODO: if ps/pdf file not found, show message about leaving outputtemplate as default
	box_msg("Creating raster image...");
	XFlush(d); //Xlib won't update the screen while metapost runs unless we do this
	char cmd[strlen("convert -density  -.pdf ") + (int) floor(log10(density)) + 2 + strlen(job_name) + strlen(fig_num) + strlen(XBM_FILENAME) + 1]; //+2 is just in case floor(log10(density))+1 doesn't exactly give the number of chars needed for the density
	if (USE_MPTOPDF)
		sprintf(cmd,"convert -density %d %s-%s.pdf %s",density,job_name,fig_num,XBM_FILENAME);
	else
		sprintf(cmd,"convert -density %d %s.%s %s",density,job_name,fig_num,XBM_FILENAME);
	printf("\nRunning \"%s\"...\n",cmd);
	int ret = system(cmd);
	if (ret != 0) {
		error();
		puts("\nERROR: couldn't create bitmap. This could be due to one of the following:");
		puts("You're using a non-default outputtemplate.");
		puts("Imagemagick is not installed on your system.");
		puts("You haven't set \"prologues:=3;\" in your metapost file.");
	} else {
		int hotspot_x, hotspot_y;
		int rc = XReadBitmapFile(d, w,
								 XBM_FILENAME,
								 &bitmap_width, &bitmap_height,
								 &bitmap,
								 &hotspot_x, &hotspot_y);
		/* check for failure or success. */
		if (rc != BitmapSuccess) {
			error();
			switch (rc) {
				case BitmapOpenFailed:
					fprintf(stderr, "XReadBitmapFile - could not open file '%s'.\n",XBM_FILENAME);
					break;
				case BitmapFileInvalid:
					fprintf(stderr,
							"XReadBitmapFile - file '%s' doesn't contain a valid bitmap.\n",
							XBM_FILENAME);
					break;
				case BitmapNoMemory:
					fprintf(stderr, "XReadBitmapFile - not enough memory.\n");
					break;
			}
		} else {
			sprintf(cmd,"rm %s",XBM_FILENAME); //get rid of xbm file
			system(cmd);
			if (!help) redraw_screen();
		}
	}
}

void copy_to_clipboard(char* s) {
	char cmd[strlen(s) + strlen("echo -n \"\" | xsel -i -b") + 1]; 
	sprintf(cmd,"echo -n \"%s\" | xsel -i -b",s);
	system(cmd);
}

void get_coords() {
	//Get the metapost coordinates of the lower left corner of the image
	//There are three ways I can think of doing this.
	//Method 1: use "show ..." in the metapost to write the coords to stdout then read them in with popen
	//Method 2: similar, but read them from the log file afterwards. (This doesn't work with mptopdf - it doesn't seem to write show commands to the log.)
	//Method 3: use "write ..." in the metapost to write the coords to a separate file. The advantage of this is that the file will only contain those coords so we don't have to search through it like with the log file. But how to handle multiple figures? And it's a bit messy to be creating all these files; we delete it afterwards, but still.

	char buffer[100];
	bool found_coords = false;

	//Method 1: get coords from stdout
	/*FILE *mp_out = popen("mptopdf test.mp | grep '>>'", "r");
	fgets(buffer, sizeof(buffer), mp_out);
	puts(buffer);
	pclose(mp_out);

	int i = 4; 
	while (buffer[i] != ',') i++;
	buffer[i] = '\0';
	ll_x = strtof(buffer+4,NULL);
	int j = i+1;
	while (buffer[j] != ')') j++;
	buffer[j] = '\0';
	ll_y = strtof(buffer+i+1,NULL);*/

	//Method 2: get coords from the log file
	//Doesn't check that metapost is actually outputting sensible coordinates, just searches for '>> "Figure 1 x-coordinate:' and takes whatever comes after (up to another double-quote) as the x-coord, then whatever's on the next line in the right position as the y-coord.
	FILE *log;
	char log_filename[strlen(job_name)+5];
	sprintf(log_filename,"%s.log",job_name);
	log = fopen(log_filename,"r");
	if (log == NULL) {
	} else {
		char substring[26 + strlen(fig_num)];
		sprintf(substring,">> \"Figure %d x-coordinate:",atoi(fig_num));

		while (fgets(buffer,sizeof(buffer),log) != NULL) {
			buffer[strlen(substring)] = '\0';
			if (strcmp(buffer,substring) == 0) {
				int i = strlen(substring) + 1;
				while (buffer[i] != '"' && i < sizeof(buffer)-1) i++;
				buffer[i] = '\0';
				ll_x = strtof(buffer+strlen(substring)+1,NULL);

				fgets(buffer,sizeof(buffer),log); //y-coordinate will be on next line
				i = strlen(substring) + 1;
				while (buffer[i] != '"') i++;
				buffer[i] = '\0';
				ll_y = strtof(buffer+strlen(substring)+1,NULL);
				if (!USE_MPTOPDF) ll_y -= 0.8; //The vertical coordinate seems to be off by about 0.8pt when using mpost. I don't know why. 
				found_coords = true;
				break;
			}
		}
		if (!found_coords) {
			error();
			puts("\nERROR: coordinates of lower left corner of diagram not found.");
			printf("Ensure that %s.mp contains the following lines at the start:\n\n",job_name);
			puts("prologues:=3;");
			puts("def save_coords =");
			puts("	show \"Figure \" & decimal(charcode) & \" x-coordinate: \" & decimal((xpart llcorner bbox currentpicture) + bboxmargin);");
			puts("	show \"Figure \" & decimal(charcode) & \" y-coordinate: \" & decimal((ypart llcorner bbox currentpicture) + bboxmargin);");
			puts("enddef;");
			puts("extra_endfig := extra_endfig & \"save_coords;\";");
			//copy_to_clipboard("write decimal ((xpart llcorner bbox currentpicture) + bboxmargin) to \"coords.txt\";\n write decimal ((ypart llcorner bbox currentpicture) + bboxmargin) to \"coords.txt\";");
		}

		fclose(log);
	}

	//Method 3: get coords from "coords.txt"
	//Metapost must run 'write decimal ((xpart llcorner bbox currentpicture) + bboxmargin) to "coords.txt";' after the figure.
	/*FILE *coords;
	coords = fopen("coords.txt","r");
	if (coords == NULL) {
		puts("coords.txt not found.");
		puts("Please put the following two lines after endfig:\n");
		puts("write decimal ((xpart llcorner bbox currentpicture) + bboxmargin) to \"coords.txt\";\nwrite decimal ((ypart llcorner bbox currentpicture) + bboxmargin) to \"coords.txt\";");
		//copy_to_clipboard("write decimal ((xpart llcorner bbox currentpicture) + bboxmargin) to \"coords.txt\";\n write decimal ((ypart llcorner bbox currentpicture) + bboxmargin) to \"coords.txt\";");
		error();
	} else {
		fgets(buffer,sizeof(buffer),coords);
		ll_x = strtof(buffer,NULL);
		fgets(buffer,sizeof(buffer),coords);
		ll_y = strtof(buffer,NULL);
		if (!USE_MPTOPDF) ll_y -= 0.8; //The vertical coordinate seems to be off by about 0.8pt when using mpost. I don't know why. 
		fclose(coords);
		system("rm coords.txt");
	}*/

	if (found_coords) make_bitmap();
}

//run metapost on the file and get coords of lower left corner
void run_mpost() {
	box_msg("Running metapost...");
	XFlush(d); //Xlib won't update the screen while metapost runs unless we do this
	char cmd[strlen(job_name) + strlen("mpost --interaction=nonstopmode .mp") + 1];
	if (USE_MPTOPDF)
		sprintf(cmd,"mptopdf %s.mp",job_name);
	else
		sprintf(cmd,"mpost --interaction=nonstopmode %s.mp",job_name);
	printf("\nRunning \"%s\"...\n",cmd);
	int ret = system(cmd);
	if (ret != 0) {
		error();
	} else {
		get_coords();
	}

}

int main(int argc, char **argv) {
	if (argc > 1) job_name = argv[1];
	else job_name = "test";
	if (argc > 2) {
		fig_num = argv[2];
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

	//load picture in to trace over
	/*
	show_trace = true;
	get_trace();*/

	run_mpost();

	while (!quit) {
		XNextEvent(d, &e);
		switch(e.type) {
		case Expose: //on startup, and when window is resized, workspace switched
			;Window rt; int x,y; unsigned int bw,dpth;
			XGetGeometry(d,w,&rt,&x,&y,&win_width,&win_height,&bw,&dpth);
			//x_offset = -ll_x/SCALE*density - win_width/2;
			//y_offset = ll_y/SCALE*density + win_height/2;
			if (help) show_help(); else redraw_screen();
			break;
		case ButtonPress:
			if (edit) move_knot();
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
	//printf("endfig;\nend\n");
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
	show_msg(pos++,"WYSIAWYG metapost editor.");
	show_msg(pos++,"Press q to quit, r to redraw the metapost file, ? to show this help message.");
	show_msg(pos++,"Metapost instructions are shown in stdout, and also copied to clipboard so you can add them to your mp file.");
	pos++;

	show_msg(pos++,"Draw a path by clicking at points on the path. Escape will end the path, Enter will make it a cycle.");
	show_msg(pos++,"Press . for curve mode (default), press - for straight lines, press v for a corner in a curved path.");
	show_msg(pos++,"Pressing u will undo the last point.");
	pos++;

	show_msg(pos++,"Press c for circle mode. Click at the centre of the circle, then the edge."); 
	pos++;

	show_msg(pos++,"After a path, circle, or point has been drawn, you can edit it by dragging the little circles.");
	show_msg(pos++,"Once you're happy with it, press y (\"yank\" the path) to output the path to clipboard and stdout."); 
	show_msg(pos++,"You can copy a path from your mp file to the clipboard, then edit it by pressing p (\"push\" a path)."); 
	pos++;

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
	case 41://u - undo creating a knot
		if (!finished_drawing && cur_path->n > 0) {
			cur_path->n--;
			if (cur_path->n == 0) finished_drawing = true;
			redraw_screen();
		}
		break;
	case 61://z - zoom
		if (state & ShiftMask) density/=2; //shift-z zooms out
		else density*=2;
		make_bitmap();
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

void move_knot() {
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
	char *s = path_string();
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
			//knot under pointer
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
	run_mpost();
}

void draw_path() {
	get_controls();
	
	int i;
	struct point *p,*q;
	for (i=0; i<cur_path->n-1; i++) {
		p = &cur_path->points[i];
		q = &cur_path->points[i+1];
		draw_circle(cur_path->points[i].x,cur_path->points[i].y, POINT_RADIUS);
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
	draw_circle(cur_path->points[cur_path->n-1].x,cur_path->points[cur_path->n-1].y, POINT_RADIUS);
	if (cur_path->cycle) {
		p = &cur_path->points[cur_path->n-1];
		q = &cur_path->points[0];
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
		r = sqrt(delta_x*delta_x + delta_y*delta_y) / SCALE * density;

		draw_circle(cur_path->points[0].x,cur_path->points[0].y,(int) r);
		draw_circle(cur_path->points[0].x,cur_path->points[0].y,POINT_RADIUS);
		draw_circle(cur_path->points[1].x,cur_path->points[1].y,POINT_RADIUS);
	}

	if (edit)
		fill_circle(cur_path->points[edit_point].x,cur_path->points[edit_point].y,POINT_RADIUS);
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

void get_trace () {
		int hotspot_x, hotspot_y;
		unsigned int trace_width,trace_height;
		char *filename = "test.xbm";
		int rc = XReadBitmapFile(d, w,
								 filename,
								 &trace_width, &trace_height,
								 &tracing_bitmap,
								 &hotspot_x, &hotspot_y);
		/* check for failure or success. */
		if (rc != BitmapSuccess) {
			error();
			show_trace = false;
			switch (rc) {
				case BitmapOpenFailed:
					fprintf(stderr, "XReadBitmapFile - could not open file '%s'.\n",filename);
					break;
				case BitmapFileInvalid:
					fprintf(stderr,
							"XReadBitmapFile - file '%s' doesn't contain a valid bitmap.\n",
							filename);
					break;
				case BitmapNoMemory:
					fprintf(stderr, "XReadBitmapFile - not enough memory.\n");
					break;
			}
		}
}
