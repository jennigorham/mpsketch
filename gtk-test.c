#include <gtk/gtk.h>
#include <stdlib.h>
#include <gdk/gdkkeysyms.h>

/*
TODO:
how to quit
tabs for different figures? http://www.cc.gatech.edu/data_files/public/doc/gtk/tutorial/gtk_tut-8.html
menus? open, help, quit, show/hide trace, change units, precision, change figure
may be possible to display the vector graphics without converting to png first: http://stackoverflow.com/questions/3672847/how-to-embed-evince
*/

/*
//bezier spline
cairo_move_to(cr,x0,y0);
cairo_curve_to (cairo_t *cr,
                double x1,
                double y1,
                double x2,
                double y2,
                double x3,
                double y3);

cairo_arc (cairo_t *cr,
           double xc,
           double yc,
           double radius,
           0,
           0);
*/

cairo_surface_t *mp_png;  
cairo_surface_t *trace;  

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
	cairo_set_source_surface(cr, trace, 10, 10);
	cairo_paint(cr);

	cairo_set_source_surface(cr, mp_png, -100, 10);
	cairo_paint(cr);

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_set_line_width(cr, 0.5);
	cairo_move_to(cr,10,10);
	cairo_line_to(cr,100,100);
	cairo_stroke(cr);

	return FALSE;
}

static gboolean clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	printf("Button %d, (%f,%f)\n",event->button,event->x,event->y);
	return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	printf("%d\n",event->keyval);
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
		case GDK_KEY_y: ;
			GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
			gtk_clipboard_set_text(clip, "test", -1);
			break;
		case GDK_KEY_p: ;
			gchar *text = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
			if (text)
				printf("%s\n",text);
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

static void activate (GtkApplication* app, gpointer user_data) {
	GtkWidget *window;
	GtkWidget *darea;

	mp_png = cairo_image_surface_create_from_png("test.png");
	trace = cairo_image_surface_create_from_png("trace.png");

	window = gtk_application_window_new (app);
	gtk_window_set_title (GTK_WINDOW (window), "MPSketch");
	gtk_window_set_default_size (GTK_WINDOW (window), 200, 200);
	gtk_widget_add_events(window, GDK_BUTTON_PRESS_MASK);
	gtk_widget_add_events(window, GDK_POINTER_MOTION_MASK);
	g_signal_connect(window, "button-press-event", G_CALLBACK(clicked), NULL);
	g_signal_connect(window, "key-press-event", G_CALLBACK (on_key_press), NULL);
	g_signal_connect(window, "motion-notify-event", G_CALLBACK (on_motion), NULL);

	darea = gtk_drawing_area_new();
	gtk_container_add(GTK_CONTAINER(window), darea);

	g_signal_connect(G_OBJECT(darea), "draw", G_CALLBACK(on_draw_event), NULL);

	gtk_widget_show_all (window);
}

int main (int argc, char **argv) {
	GtkApplication *app;
	int status;

	app = gtk_application_new ("com.github.jennigorham.mpsketch", G_APPLICATION_FLAGS_NONE);
	g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
	status = g_application_run (G_APPLICATION (app), argc, argv);
	g_object_unref (app);

	cairo_surface_destroy(mp_png);
	cairo_surface_destroy(trace);
	return status;
}

