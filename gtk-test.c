#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>

#include "paths.h"
#include "mptoraster.h"

/*
TODO:
tabs for different figures? http://www.cc.gatech.edu/data_files/public/doc/gtk/tutorial/gtk_tut-8.html
menus? open, help, quit, show/hide trace, change units, precision, change figure
may be possible to display the vector graphics without converting to png first: http://stackoverflow.com/questions/3672847/how-to-embed-evince
*/

#define SCROLL_STEP 10 //How many pixels to scroll at a time

#define CURVE_MODE 0
#define STRAIGHT_MODE 1
#define CORNER_MODE 2
#define CIRCLE_MODE 3

cairo_surface_t *mp_png;  

cairo_surface_t *trace;  
char *trace_filename;
int trace_x_offset,trace_y_offset;
bool show_trace = false;

char *job_name; //The part of the metapost filename before ".mp"
unsigned int fig_num; //metapost figure number (the "1" in "beginfig(1)" for example)

int mode=CURVE_MODE; //default drawing mode
bool finished_drawing=true; //if true then we're ready to start drawing another path or circle.

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

void show_error(gpointer window) {//http://zetcode.com/gui/gtk2/gtkdialogs/
	GtkWidget *dialog;
	dialog = gtk_message_dialog_new(GTK_WINDOW(window),
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_ERROR,
			GTK_BUTTONS_OK,
			"Error loading file");
	gtk_window_set_title(GTK_WINDOW(dialog), "Error");
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

static gboolean on_draw_event(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
	if (show_trace) {
		cairo_set_source_surface(cr, trace, trace_x_offset, trace_y_offset);
		cairo_paint(cr);
	}

	cairo_set_source_surface(cr, mp_png, x_offset, y_offset);
	cairo_paint(cr);

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_set_line_width(cr, 0.5);
	//straight line
	cairo_move_to(cr,10,10);
	cairo_line_to(cr,100,100);
	//circle
	cairo_new_sub_path(cr);
	cairo_arc(cr, 50, 100, 10, 0, 2*M_PI);
	//bezier spline
	cairo_move_to(cr,110,110);
	cairo_curve_to(cr,120,110,140,140,150,140);

	cairo_stroke(cr);

	//filled circle
	cairo_arc(cr, 10, 10, 3, 0, 2*M_PI);
	cairo_fill(cr);

	return FALSE;
}

static gboolean clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	printf("Button %d, (%f,%f)\n",event->button,event->x,event->y);
	return FALSE;
}

static gboolean button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	printf("Button %d release, (%f,%f)\n",event->button,event->x,event->y);
	return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	///usr/include/gtk-3.0/gdk/gdkkeysyms.h
	switch(event->keyval) {
		case GDK_KEY_e: ;
			show_error(GTK_WINDOW(widget));
			break;
		case GDK_KEY_w: ;
			gint width,height;
			gtk_window_get_size(GTK_WINDOW(widget), &width, &height);
			printf("%d,%d\n",width,height);
			break;
		case GDK_KEY_q: ;
			gtk_widget_destroy(widget);
			break;
		case GDK_KEY_y: ;
			gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), "test", -1);
			break;
		case GDK_KEY_p: ;
			gchar *text = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
			if (text)
				printf("%s\n",text);
			break;
		default:
			printf("%d\n",event->keyval);
	}
	return FALSE;
}
static gboolean on_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
	int x, y;
	GdkModifierType state;
	GdkDevice *device=gdk_event_get_source_device((GdkEvent *)event);
	gdk_window_get_device_position(event->window,device,&x,&y,&state);
	printf("(%d,%d)\n",x,y);
	return FALSE;
}

static gboolean on_scroll(GtkWidget* widget, GdkEventScroll* event, gpointer user_data) {
	switch (event->direction) {
		case GDK_SCROLL_UP:
			y_offset += SCROLL_STEP;
			break;
		case GDK_SCROLL_DOWN:
			y_offset -= SCROLL_STEP;
			break;
		case GDK_SCROLL_LEFT:
			x_offset += SCROLL_STEP;
			break;
		case GDK_SCROLL_RIGHT:
			x_offset -= SCROLL_STEP;
			break;
		case GDK_SCROLL_SMOOTH:
			puts("Smooth scrolling not implemented yet. Sorry.");
			break;
	}
	gtk_widget_queue_draw(widget);
	return FALSE;
}

static void activate (GtkApplication* app, gpointer user_data) {
	GtkWidget *window;
	GtkWidget *darea;

	mp_png = cairo_image_surface_create_from_png("test.png");

	if (show_trace) {
		trace_x_offset = 0;
		trace_y_offset = 0;
		trace = cairo_image_surface_create_from_png(trace_filename);
		if (cairo_surface_status(trace) != CAIRO_STATUS_SUCCESS) show_trace = false;
	}

	window = gtk_application_window_new (app);
	gtk_window_set_title (GTK_WINDOW (window), "MPSketch");
	gtk_window_set_default_size (GTK_WINDOW (window), 200, 200);
	gtk_widget_add_events(window, GDK_BUTTON_PRESS_MASK);
	gtk_widget_add_events(window, GDK_POINTER_MOTION_MASK);
	gtk_widget_add_events(window, GDK_BUTTON_RELEASE_MASK);
	gtk_widget_add_events(window, GDK_SCROLL_MASK);
	g_signal_connect(window, "button-press-event", G_CALLBACK(clicked), NULL);
	g_signal_connect(window, "button-release-event", G_CALLBACK(button_release), NULL);
	g_signal_connect(window, "key-press-event", G_CALLBACK (on_key_press), NULL);
	g_signal_connect(window, "motion-notify-event", G_CALLBACK (on_motion), NULL);
	g_signal_connect(window, "scroll-event", G_CALLBACK (on_scroll), NULL);

	darea = gtk_drawing_area_new();
	gtk_container_add(GTK_CONTAINER(window), darea);

	g_signal_connect(G_OBJECT(darea), "draw", G_CALLBACK(on_draw_event), NULL);

	gtk_widget_show_all (window);
}

int main (int argc, char **argv) {
	unit = 1; //default is to use units of postscript points (1/72 inches). 
	int c;
	strcpy(unit_name,"");
	while ((c = getopt (argc, argv, "u:t:")) != -1){
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
				//printf("%f,%s\n",unit,unit_name);
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
	}
	coord_precision = ceil(log10(unit)); //choose a sensible number of decimal places for coordinates

	if (argc > optind) job_name = argv[optind];
	else job_name = "test";
	if (argc > optind+1) {
		fig_num = atoi(argv[optind+1]);
	} else fig_num = 1;
	//TODO: create job_name.mp if it doesn't exist

	GtkApplication *app;
	int status;

	app = gtk_application_new ("com.github.jennigorham.mpsketch", G_APPLICATION_FLAGS_NONE);
	g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
	status = g_application_run (G_APPLICATION (app), 1, argv); //Couldn't find any info on how to stop it from complaining about unknown options and dying, so I'm telling it there are none (argc = 1)
	//status = g_application_run (G_APPLICATION (app), argc, argv);
	g_object_unref (app);

	cairo_surface_destroy(mp_png);
	cairo_surface_destroy(trace);
	return status;
}

