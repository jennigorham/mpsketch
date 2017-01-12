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

#define POINT_RADIUS 3 //for drawing a little circle around each point on the path
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
gint win_height = 600;
gint win_width = 800;

// metapost coords of lower left corner of image
//Coding these in for now until I set up mptoraster
float ll_x = -359.35;
float ll_y = -215.52;

//when we mouse over a point in a completed path, we can edit it
bool edit=false;
int edit_point; //which point are we editing

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

void draw_circle(cairo_t *cr, double centre_x, double centre_y, int r) {
	cairo_new_sub_path(cr);
	cairo_arc(cr, mp_x_coord_to_pxl(centre_x), mp_y_coord_to_pxl(centre_y), r, 0, 2*M_PI);
}

void draw_bezier(cairo_t *cr, double start_x, double start_y, double start_right_x, double start_right_y, double end_left_x, double end_left_y, double end_x, double end_y) {
	cairo_new_sub_path(cr);
	cairo_move_to(cr,
		mp_x_coord_to_pxl(start_x),
		mp_y_coord_to_pxl(start_y));
	cairo_curve_to(cr,
		mp_x_coord_to_pxl(start_right_x),
		mp_y_coord_to_pxl(start_right_y),
		mp_x_coord_to_pxl(end_left_x),
		mp_y_coord_to_pxl(end_left_y),
		mp_x_coord_to_pxl(end_x),
		mp_y_coord_to_pxl(end_y));
}

void link_point_pair(cairo_t *cr, struct point *p, struct point *q) {
	if (p->straight) {
		cairo_move_to(
			cr,
			mp_x_coord_to_pxl(p->x),
			mp_y_coord_to_pxl(p->y)
		);
		cairo_line_to(
			cr,
			mp_x_coord_to_pxl(q->x),
			mp_y_coord_to_pxl(q->y)
		);
	} else {
		draw_bezier(
			cr,
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
void draw_path(cairo_t *cr) {
	find_control_points();
	
	int i;
	struct point *p,*q;
	for (i=0; i<cur_path->n-1; i++) {
		p = &cur_path->points[i];
		q = &cur_path->points[i+1];
		draw_circle(cr,p->x,p->y, POINT_RADIUS);
		link_point_pair(cr,p,q);
	}
	p = &cur_path->points[cur_path->n-1];
	draw_circle(cr,p->x,p->y, POINT_RADIUS);
	if (cur_path->cycle) {
		q = &cur_path->points[0];
		link_point_pair(cr,p,q);
	}
}

static gboolean on_draw_event(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
	if (show_trace) {
		cairo_set_source_surface(cr, trace, trace_x_offset, trace_y_offset);
		cairo_paint(cr);
	}

	cairo_set_source_surface(cr, mp_png, -x_offset, -y_offset - cairo_image_surface_get_height(mp_png) + win_height);
	cairo_paint(cr);

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_set_line_width(cr, 0.5);

	//filled circle
	cairo_arc(cr, 10, 10, 3, 0, 2*M_PI);
	cairo_fill(cr);

	if (cur_path->n > 1) {
		draw_path(cr);
		cairo_stroke(cr);
	}

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

static gboolean resize(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	gtk_window_get_size(GTK_WINDOW(widget), &win_width, &win_height);
	//printf("Resize: %d,%d\n",win_width,win_height);
	gtk_widget_queue_draw(widget);
	return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	///usr/include/gtk-3.0/gdk/gdkkeysyms.h
	switch(event->keyval) {
		case GDK_KEY_e: ;
			show_error(GTK_WINDOW(widget));
			break;
		case GDK_KEY_q: ;
			gtk_widget_destroy(widget);
			break;
		case GDK_KEY_y: ;
			gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), "test", -1);
			break;
		case GDK_KEY_p: ;
			gchar *text = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
			if (text != NULL) {
				string_to_path(text);
				gtk_widget_queue_draw(widget);
			}
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
			y_offset -= SCROLL_STEP;
			break;
		case GDK_SCROLL_DOWN:
			y_offset += SCROLL_STEP;
			break;
		case GDK_SCROLL_LEFT:
			x_offset -= SCROLL_STEP;
			break;
		case GDK_SCROLL_RIGHT:
			x_offset += SCROLL_STEP;
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

	cur_path = malloc(sizeof(struct path));
	init_path(cur_path);

	mp_png = cairo_image_surface_create_from_png("test.png");

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
	g_signal_connect(window, "configure-event", G_CALLBACK(resize), NULL);

	darea = gtk_drawing_area_new();
	gtk_container_add(GTK_CONTAINER(window), darea);

	g_signal_connect(G_OBJECT(darea), "draw", G_CALLBACK(on_draw_event), NULL);
	printf("Figure number: %d\n",fig_num);
	printf("Unit name: %s\n",unit_name);

	gtk_widget_show_all (window);
}

static void open(GApplication *app,
                 GFile         **files,
                 gint          n_files,
                 gchar        *hint,
                 gpointer      user_data) {

	job_name = g_file_get_basename(files[0]);
	//check that it ends in ".mp"
	if (strcmp(job_name + strlen(job_name) - 3,".mp") == 0) {
		job_name[strlen(job_name) - 3] = '\0';
		printf("Opening %s\n",g_file_get_parse_name(files[0]));
		activate(GTK_APPLICATION (app),NULL);
	} else {
		g_print("Argument is not an mp file\n");
	}
}

static gboolean get_unit(gchar *option_name,gchar *value,gpointer data,GError **error) {
	bool valid;
	unit = string_to_bp(value,&valid);
	if (!valid) {
		puts("Invalid measure in argument of -u");
		puts("Usage: mpsketch -u <number>[cm|mm|in|pt|bp]");
		puts("e.g. mpsketch -u 2cm");
		return FALSE;
	}
	coord_precision = ceil(log10(unit)); //choose a sensible number of decimal places for coordinates
	printf("unit=%f\n",unit);
	printf("precision=%d\n",coord_precision);
	return TRUE;
}
static gboolean get_trace(gchar *option_name,gchar *value,gpointer data,GError **error) {
	show_trace = true;
	trace_filename = value;

	trace_x_offset = 0;
	trace_y_offset = 0;
	trace = cairo_image_surface_create_from_png(trace_filename);
	if (cairo_surface_status(trace) != CAIRO_STATUS_SUCCESS) {
		g_print("Couldn't get trace image.\n");
		return FALSE;
	}

	return TRUE;
}

int main (int argc, char **argv) {
	//defaults which will be overridden if specified on command line
	unit = 1; //1 postscript point (1/72 inches). 
	coord_precision = 0;
	unit_name = "";
	fig_num = 1;

	GOptionEntry entries[] =
	{
		{ "figure-number", 'f', 0, G_OPTION_ARG_INT, &fig_num, "Figure number (the number in \"beginfig(...)\") to display. Defaults to 1.", "<num>" },
		{ "units", 'u', 0, G_OPTION_ARG_CALLBACK, (GOptionArgFunc) get_unit, "Coordinates will be printed in terms of these units. E.g. if units=5cm, then the point 10cm above the origin would be printed as \"(0.000,2.000)\". Default is to use bp (big points, i.e. 1/72 of an inch).", "<num>[mm|cm|in|bp|pt]" },
		{ "unit-name", 'n', 0, G_OPTION_ARG_STRING, &unit_name, "If set, coordinates will be printed as multiples of this name. E.g. if unit-name=u then the point (2,3) would be printed as \"(2u,3u)\".", "<name>" },
		{ "trace", 't', G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, (GOptionArgFunc) get_trace, "PNG file to trace over.", "<filename>" },
		{NULL}
	};
	
	GtkApplication *app;
	int status;

	app = gtk_application_new ("com.github.jennigorham.mpsketch", G_APPLICATION_HANDLES_OPEN);
	g_application_add_main_option_entries(G_APPLICATION(app),entries);
	g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
	g_signal_connect (app, "open", G_CALLBACK (open), NULL);
	status = g_application_run (G_APPLICATION (app), argc, argv);
	g_object_unref (app);

	cairo_surface_destroy(mp_png);
	cairo_surface_destroy(trace);
	return status;
}

