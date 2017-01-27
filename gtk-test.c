#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "common.h"

#define MP_BORDER 200 //extra space around the mp png

/*
TODO:
corner mode
move trace
custom precision
check for find_control_points() failure?

desktop file, mime type https://developer.gnome.org/integration-guide/stable/desktop-files.html.en and https://developer.gnome.org/integration-guide/stable/mime.html.en
tabs for different figures? http://www.cc.gatech.edu/data_files/public/doc/gtk/tutorial/gtk_tut-8.html
menus? open, help, quit, show/hide trace, change units, precision, change figure
may be possible to display the vector graphics without converting to png first: http://stackoverflow.com/questions/3672847/how-to-embed-evince
*/

cairo_surface_t *mp_png;  
cairo_surface_t *trace;  
cairo_t *cr;

GtkWidget *darea;
GtkWidget *info_bar;
GtkWidget *message_label;
GtkWidget *scrolled_window;

GtkAdjustment *hadj;
GtkAdjustment *vadj;

gint win_width,win_height;

//mp coords of centre of window
double scroll_centre_x;
double scroll_centre_y;

//scale of mp_png
double scale;

gchar *get_info_msg() {
	//not mentioned: 'r' to refresh metapost, 't' to toggle trace, and 'p' to push path
	if (finished_drawing) {
		if (edit) {
			if (cur_path->n == -1) {
				return "Click and drag point. Then press 'y' to copy the new circle.";
			} else if (cur_path-> n == 1) {
				return "Click and drag point. Then press 'y' to copy the new coordinates.";
			} else {
				if (!cur_path->cycle && edit_point == cur_path->n - 1) { //can't change to curve/straight since there's nothing after last point
					return "Click and drag point to move. Then press 'y' to copy path.\n"
						"'d' = delete. 'a'/'i' = append/insert point. Enter = toggle cycle.";
				} else if (cur_path->points[edit_point].straight)
					return "Click and drag point to move. Then press 'y' to copy path.\n"
						"'.' = curve. 'd' = delete. 'a'/'i' = append/insert point. Enter = toggle cycle.";
				else
					return "Click and drag point to move. Then press 'y' to copy path.\n"
						"'-' = straight. 'd' = delete. 'a'/'i' = append/insert point. Enter = toggle cycle.";
			}
		} else {
			if (mode == CURVE_MODE)
				return "Click to start drawing a curve. Press '-' for straight line mode, 'c' for circle mode, 'h' to show help.";
			else if (mode == STRAIGHT_MODE)
				return "Click to start drawing straight lines. Press '.' for curve mode, 'c' for circle mode, 'h' to show help.";
			else if (mode == CIRCLE_MODE)
				return "Click to start drawing a circle. Press '-' for straight line mode, '.' for curve mode, 'h' to show help.";
		}
	} else {
		if (mode == CURVE_MODE)
			return "Click to add point. Escape = end path. Enter = make cycle. '-' = straight line mode.";
		else if (mode == STRAIGHT_MODE)
			return "Click to add point. Escape = end path. Enter = make cycle. '.' = curve mode.";
		else if (mode == CIRCLE_MODE)
			return "Click to end circle.";
	}
	return "";
}

void mode_change() {
	gtk_label_set_text (GTK_LABEL (message_label), get_info_msg());
}

void show_error(gpointer window,char *fmt,char *msg) {//http://zetcode.com/gui/gtk2/gtkdialogs/
	GtkWidget *dialog;
	dialog = gtk_message_dialog_new(GTK_WINDOW(window),
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_ERROR,
			GTK_BUTTONS_OK,
			fmt,msg);
	gtk_window_set_title(GTK_WINDOW(dialog), "Error");
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

void show_help(gpointer window) {
	GtkWidget *dialog;
	dialog = gtk_message_dialog_new(GTK_WINDOW(window),
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_INFO,
			GTK_BUTTONS_OK,
			"r = rerun metapost\n"
			"f = change figure number\n"
			//"t = toggle trace visibility\n"
			"z = zoom in, shift-z = zoom out\n"
			"p or ctrl-v = paste path from clipboard\n"
			"h or ? = show this help dialog\n"
			"q = quit\n"

			"\nDrawing paths and circles:\n"
			"'.' = curve mode, '-' = straight line mode, c = circle mode\n"
			"u = undo creating last point\n"
			"Escape = end path, Enter = end path and make a cycle\n"

			"\nEdit mode (mouse over a point on a path):\n"
			"d = delete point\n"
			"a = append point (after current point), i = insert point (before current point)\n"
			"'.' = make following section curved, '-' = make following section straight\n"
			"Enter = toggle cycle\n"
			"After editing, press y or ctrl-c to copy path to clipboard\n"
			);
	gtk_window_set_title(GTK_WINDOW(dialog), "Keybindings");
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

void adjust_darea_size() {
	sketch_width  = scale*(2*MP_BORDER + cairo_image_surface_get_width(mp_png));
	sketch_height = scale*(2*MP_BORDER + cairo_image_surface_get_height(mp_png));
	gtk_widget_set_size_request(darea, sketch_width, sketch_height);
	y_offset = MP_BORDER*scale;
	x_offset = -MP_BORDER*scale;
	pixels_per_point = scale*density/INCH;
	redraw_screen();
}

void get_figure(gpointer window) {
	int ret = get_coords(tmp_job_name);
	if (ret != 0) {
		if (ret == 1) show_error(window,"Couldn't open %s.log.",tmp_job_name);
		else if (ret == 2) {
			char s[10];
			snprintf(s,sizeof s,"%d",fig_num);
			show_error(window,"Couldn't find figure %s coordinates. Ensure figure number is correct.",s);
		}
	} else if (make_png(tmp_job_name) != 0) {
		show_error(window,"%s","Error converting to png. See stdout for more details.");
	} else {
		char png[strlen(tmp_job_name)+5];
		sprintf(png,"%s.png",tmp_job_name);
		mp_png = cairo_image_surface_create_from_png(png);

		adjust_darea_size();
	}
}

void select_figure(gpointer window) {
	GtkWidget *dialog, *content_area, *combo;

	dialog = gtk_dialog_new_with_buttons("Figure",window,GTK_DIALOG_DESTROY_WITH_PARENT,"OK",GTK_RESPONSE_NONE,NULL);
	content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

	combo = gtk_combo_box_text_new();
	gtk_container_add( GTK_CONTAINER( content_area ), combo );
	int i;
	int index; //index of current figure
	for (i=0; i<n_fig; i++) {
		char entry[10];
		snprintf(entry,sizeof(entry),"Figure %d",figures[i]);
		if (figures[i] == fig_num) index = i;
		gtk_combo_box_text_append_text( GTK_COMBO_BOX_TEXT( combo ), entry );
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo),index);
	gtk_widget_show(combo);

	gtk_dialog_run(GTK_DIALOG(dialog));

	gchar *fig = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT( combo ));
	if (fig) {
		fig_num = atoi(fig + strlen("Figure "));
		g_free(fig);
	}
	gtk_widget_destroy(dialog);

	//scroll to origin
	scroll_centre_x = 0;
	scroll_centre_y = 0;
	//create and load in the png
	get_figure(window);
}

//rerun metapost, convert to png
static gboolean refresh(gpointer window) {
	int ret = create_mp_file(job_name,tmp_job_name);
	if (ret == 1) 
		show_error(window,"Couldn't open %s.mp",job_name);
	else if (ret == 2)
		show_error(window,"Couldn't open %s.mp for writing",tmp_job_name);
	else if (run_mpost(tmp_job_name) != 0) {
		show_error(window,"%s","Error running metapost. See stdout for more details.");
	} else get_figure(window);
	mode_change(); //replace info bar message
	return FALSE;
}

void fill_circle(double centre_x, double centre_y, int r) {
	cairo_new_sub_path(cr);
	cairo_arc(cr, mp_x_coord_to_pxl(centre_x), mp_y_coord_to_pxl(centre_y), r, 0, 2*M_PI);
	cairo_fill(cr);
}

void draw_circle(double centre_x, double centre_y, int r) {
	cairo_arc(cr, mp_x_coord_to_pxl(centre_x), mp_y_coord_to_pxl(centre_y), r, 0, 2*M_PI);
	cairo_stroke(cr);
}

void draw_point(double centre_x, double centre_y) {
	cairo_set_source_rgb(cr, 1, 1, 1);
	fill_circle(centre_x,centre_y,POINT_RADIUS);
	cairo_set_source_rgb(cr, 0, 0, 0);
	draw_circle(centre_x,centre_y,POINT_RADIUS);
}

void draw_bezier(double start_x, double start_y, double start_right_x, double start_right_y, double end_left_x, double end_left_y, double end_x, double end_y) {
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

void link_point_pair(struct point *p, struct point *q) {
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
	cairo_stroke(cr);
}
static gboolean on_draw_event(GtkWidget *widget, cairo_t *this_cr, gpointer user_data) {
	cr = this_cr; //make it global. Otherwise I'd have to pass it to draw_path which would no longer be common between the xlib and gtk versions
	if (show_trace && trace) {
		cairo_set_source_surface(cr, trace, trace_x_offset, trace_y_offset);
		cairo_paint(cr);
	}

	if (mp_png) {
		cairo_set_source_surface(cr, mp_png, MP_BORDER-1, MP_BORDER-1); //since adding the scrolled_window, it was off by 1

		//scale it
		cairo_pattern_t *pattern = cairo_get_source(cr);
		cairo_matrix_t matrix;
		cairo_pattern_get_matrix(pattern,&matrix);
		cairo_matrix_scale(&matrix,1/scale,1/scale);
		cairo_pattern_set_matrix(pattern,&matrix);

		cairo_paint(cr);
	}

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_set_line_width(cr, 0.5);

	draw_path();
	cairo_stroke(cr);

	if (edit)
		fill_circle(cur_path->points[edit_point].x,cur_path->points[edit_point].y,POINT_RADIUS);

	return FALSE;
}

static gboolean button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	if (event->button == 1 && edit) dragging_point = true;
	return FALSE;
}

//translate event->x and y to drawing area coords
void darea_coords(gdouble wx, gdouble wy, int *x, int *y) {
	//compensate for scrollbar position
	*x = wx + gtk_adjustment_get_value(hadj);
	*y = wy + gtk_adjustment_get_value(vadj);

	//compensate for menubar
	GtkAllocation alloc;
	gtk_widget_get_allocation(scrolled_window, &alloc);
	*y-=alloc.y;
}
static gboolean button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	if (event->button == 1) {
		int x,y;
		darea_coords(event->x,event->y,&x,&y);
		click_point(x,y);
	}
	return FALSE;
}

void redraw_screen() {
	gtk_widget_queue_draw(darea);
}

static gboolean resize(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	gtk_window_get_size(GTK_WINDOW(widget), &win_width, &win_height);
	return FALSE;
}

//centre given mp coords in window
void scroll_to(double x,double y) {
	gtk_adjustment_set_value(hadj,mp_x_coord_to_pxl(x) - win_width/2);
	gtk_adjustment_set_value(vadj,mp_y_coord_to_pxl(y) - win_height/2);
}
static gboolean scroll_to_origin() {
	if (!mp_png || win_width == 0) return TRUE;
	scroll_to(0,0);
	return FALSE;
}
//when zooming in, we want the centre of the window to stay fixed
static gboolean maintain_scroll(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	scroll_to(scroll_centre_x, scroll_centre_y);
	return FALSE;
}

void copy_to_clipboard(char *s) {
	gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), s, -1);

	//Show it in the info bar too
	char msg[strlen(s) + strlen("Copied to clipboard: ") + 1];
	sprintf(msg,"Copied to clipboard: %s",s);
	gtk_label_set_text(GTK_LABEL (message_label), msg);
}

bool is_off_screen(double x, double y) {
	int screen_x = mp_x_coord_to_pxl(x) - gtk_adjustment_get_value(hadj);
	int screen_y = mp_y_coord_to_pxl(y) - gtk_adjustment_get_value(vadj);
	GtkAllocation alloc;
	GtkWidget *child = gtk_bin_get_child(GTK_BIN(scrolled_window));
	gtk_widget_get_allocation(child, &alloc);
	return (screen_x < 0 || screen_x > alloc.width || screen_y < 0 || screen_y > alloc.height);
}

void push_path() {
	gchar *text = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
	if (text != NULL) {
		string_to_path(text);
		finished_drawing = true;
		if (cur_path->n == 0) {
			gtk_label_set_text (GTK_LABEL (message_label), "Invalid path");
		} else {
			struct point fp = cur_path->points[0]; //first point
			if (is_off_screen(fp.x,fp.y))
				scroll_to(fp.x,fp.y);
			redraw_screen();
			mode_change();
		}
	}
}

static gboolean key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	///usr/include/gtk-3.0/gdk/gdkkeysyms.h
	if (!dragging_point) {
		switch(event->keyval) {
			case GDK_KEY_Escape:
				if (finished_drawing) { //clear
					cur_path->n = 0;
					redraw_screen();
				} else
					end_path();
				break;
			case GDK_KEY_Return: //cycle
				if (!finished_drawing) {
					cur_path->cycle = true;
					end_path();
				} else if (edit) {
					cur_path->cycle = !cur_path->cycle;
					redraw_screen();
				}
				break;
			//help
			case GDK_KEY_question:
			case GDK_KEY_h:
				show_help(GTK_WINDOW(widget));
				break;
			case GDK_KEY_Z: //zoom out
				//remember where we want to scroll to
				scroll_centre_x = pxl_to_mp_x_coord(gtk_adjustment_get_value(hadj) + win_width/2);
				scroll_centre_y = pxl_to_mp_y_coord(gtk_adjustment_get_value(vadj) + win_height/2);

				scale /= 1.5;
				adjust_darea_size();
				break;
			case GDK_KEY_z: //zoom
				//remember where we want to scroll to
				scroll_centre_x = pxl_to_mp_x_coord(gtk_adjustment_get_value(hadj) + win_width/2);
				scroll_centre_y = pxl_to_mp_y_coord(gtk_adjustment_get_value(vadj) + win_height/2);

				scale *= 1.5;
				adjust_darea_size();
				break;
			case GDK_KEY_f: 
				select_figure(widget);
				break;
			case GDK_KEY_i: //insert point before edit_point
				if (edit) {
					point_before(edit_point);
					edit_point++;
					redraw_screen();
				}
				break;
			case GDK_KEY_a: //append point after edit_point
				if (edit) {
					point_before(edit_point+1);
					redraw_screen();
					mode_change();
				}
				break;
			case GDK_KEY_d: //delete point
				if (edit) {
					remove_point(edit_point);
					edit=false;
					redraw_screen();
					mode_change();
				}
				break;
			case GDK_KEY_u: //undo last point
				undo();
				break;
			case GDK_KEY_t: //toggle trace visibility
				show_trace = !show_trace;
				redraw_screen();
				break;
			case GDK_KEY_r: //refresh metapost
				gtk_label_set_text (GTK_LABEL (message_label), "Running metapost...");
				g_idle_add(refresh,GTK_WINDOW(widget));
				break;
			case GDK_KEY_q: //quit
				gtk_widget_destroy(widget);
				break;
			case GDK_KEY_minus: //straight line mode
				path_mode_change(true);
				mode_change();
				break;
			case GDK_KEY_period: //curve mode
				path_mode_change(false);
				mode_change();
				break;
			case GDK_KEY_v:
				if (event->state & GDK_CONTROL_MASK) { //ctrl-v paste
					push_path();
				}
				break;
			case GDK_KEY_c:
				if (event->state & GDK_CONTROL_MASK) { //ctrl-c copy
					output_path();
				} else { //circle mode
					if (!finished_drawing) end_path();
					mode=CIRCLE_MODE;
					mode_change();
				}
				break;
			case GDK_KEY_x:
				if (event->state & GDK_CONTROL_MASK) { //ctrl-x cut
					output_path();
					cur_path->n = 0;
					redraw_screen();
				}
				break;
			case GDK_KEY_y: //yank
				output_path();
				break;
			case GDK_KEY_p: ; //push
				push_path();
				break;
		}
	}

	return FALSE;
}

static gboolean on_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
	int x, y;
	darea_coords(event->x,event->y,&x,&y);
	pointer_move(x,y);
	return FALSE;
}

static void activate (GtkApplication* app, gpointer user_data) {
	GtkWidget *window;

	initialise();

	if (job_name == NULL) job_name = "test";
	//mp_png = cairo_image_surface_create_from_png("test.png");

	window = gtk_application_window_new (app);
	gtk_window_set_title (GTK_WINDOW (window), "MPSketch");
	gtk_window_set_default_size (GTK_WINDOW (window), 200, 200);
	gtk_widget_add_events(window, GDK_BUTTON_PRESS_MASK);
	gtk_widget_add_events(window, GDK_POINTER_MOTION_MASK);
	gtk_widget_add_events(window, GDK_BUTTON_RELEASE_MASK);
	gtk_widget_add_events(window, GDK_SCROLL_MASK);
	g_signal_connect(window, "button-press-event", G_CALLBACK(button_press), NULL);
	g_signal_connect(window, "button-release-event", G_CALLBACK(button_release), NULL);
	g_signal_connect(window, "key-press-event", G_CALLBACK (key_press), NULL);
	g_signal_connect(window, "motion-notify-event", G_CALLBACK (on_motion), NULL);
	g_signal_connect(window, "configure-event", G_CALLBACK(resize), NULL);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	//menus
	GtkWidget *menubar = gtk_menu_bar_new();
	GtkWidget *file_menu = gtk_menu_new();
	GtkWidget *file_mi = gtk_menu_item_new_with_label("File");
	GtkWidget *quit_mi = gtk_menu_item_new_with_label("Quit");
	GtkWidget *fig_mi = gtk_menu_item_new_with_label("Change figure");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_mi), file_menu);

	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), fig_mi);
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), quit_mi);

	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file_mi);
	gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);

	g_signal_connect_swapped(G_OBJECT(fig_mi), "activate", G_CALLBACK (select_figure), window);
	g_signal_connect_swapped(G_OBJECT(quit_mi), "activate", G_CALLBACK (g_application_quit), G_APPLICATION(app));

	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
		GTK_POLICY_AUTOMATIC, 
		GTK_POLICY_AUTOMATIC);

	darea = gtk_drawing_area_new();
	gtk_container_add(GTK_CONTAINER(scrolled_window),darea);
	gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);
	g_signal_connect(G_OBJECT(darea), "draw", G_CALLBACK(on_draw_event), NULL);
	g_signal_connect(darea, "size-allocate", G_CALLBACK(maintain_scroll), NULL);
	GtkWidget *child = gtk_bin_get_child(GTK_BIN(scrolled_window));
	hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(child));
	vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(child));

	info_bar = gtk_info_bar_new ();
	message_label = gtk_label_new (get_info_msg());
	gtk_widget_show (message_label);
	GtkWidget *content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (info_bar));
	gtk_container_add (GTK_CONTAINER (content_area), message_label);
	gtk_box_pack_end(GTK_BOX(vbox), info_bar, FALSE, TRUE, 0);

	gtk_widget_show_all (window);
	density = 100;
	scale = 1;
	pixels_per_point = scale*density/INCH;
	refresh(window);

	g_timeout_add(100,scroll_to_origin,window);
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
	//printf("unit=%f\n",unit);
	//printf("precision=%d\n",coord_precision);
	return TRUE;
}
static gboolean get_trace(gchar *option_name,gchar *value,gpointer data,GError **error) {
	show_trace = true;
	trace_x_offset = 0;
	trace_y_offset = 0;
	trace = cairo_image_surface_create_from_png(value);
	if (cairo_surface_status(trace) != CAIRO_STATUS_SUCCESS) {
		g_set_error(error,G_OPTION_ERROR, G_OPTION_ERROR_FAILED,"Couldn't get trace image from %s.\n",value);
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
	show_trace = false;

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

	cleanup();

	return status;
}

