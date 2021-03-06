#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "common.h"
#include <libgen.h> //dirname
#include <time.h>

#define MP_BORDER 200 //extra space around the mp png

/*
TODO:
corner mode
custom precision
check for find_control_points() failure?

desktop file, mime type https://developer.gnome.org/integration-guide/stable/desktop-files.html.en and https://developer.gnome.org/integration-guide/stable/mime.html.en
tabs for different figures? http://www.cc.gatech.edu/data_files/public/doc/gtk/tutorial/gtk_tut-8.html
menus? open, help, quit, show/hide trace, change units, precision, change figure
may be possible to display the vector graphics without converting to png first: http://stackoverflow.com/questions/3672847/how-to-embed-evince
*/

int units_dropdown_index; //remember whether units were specified as bp/mm/cm/in/pt

cairo_surface_t *mp_png; //raster image of the metapost output
cairo_surface_t *trace; //image to trace over
cairo_t *cr;

//status messages at bottom of window
GtkWidget *message_label;

GtkWidget *darea; //drawing area
GtkWidget *scrolled_window; //contains darea
//scroll adjustments (used to get/set scrollbar position)
GtkAdjustment *hadj;
GtkAdjustment *vadj;

gint win_width,win_height;

//mp coords of centre of window
double scroll_centre_x = 0;
double scroll_centre_y = 0;
//when scrollbar position changes ("value-changed" event for hadj,vadj), we save the new scroll_centre_x, scroll_centre_y, but we don't want that to be triggered when darea changes size, so we need to "lock" the scrollbar position at the start of adjust_darea_size and release it at the end (in maintain_scroll)
bool scrollbar_lock = false;

//scale of mp_png
double scale;

//menu items that need to be deactivated/reactivated
GtkWidget *resume_mi;
GtkWidget *end_mi;
GtkWidget *fig_mi;
GtkWidget *next_fig_mi;
GtkWidget *prev_fig_mi;
GtkWidget *rerun_mi;

//hack to stop it double clicking
time_t last_click = 0;


//Info bar
void add_info_bar(GtkWidget *vbox);
void mode_change(); //update the info message and deactivate/reactivate menu items whenever we change modes (eg start editing a path, change drawing mode, finish drawing a path, etc)
gchar *get_info_msg(); //decides what hints/keyboard shortcuts to display in the info bar based on the mode

//Drawing menu callbacks
void curve_mode();
void straight_mode();
void circle_mode();
void resume_drawing(gpointer window); //undo end_path()

//Dialogs
void show_error(gpointer window,char *fmt,char *msg);
void show_help(gpointer window);
void open_dialog(GtkWidget *window); //open an mp file

void units_preferences(gpointer window); //dialog for changing units
void units_update(GtkWidget *widget, GtkWidget **widgets); //update the example line in the units dialog when the user changes something
double units_multiplier(int i); //get multiplier from index on the units dropdown

//Get png of the metapost
void rerun_metapost(gpointer window); //show infobar message then do refresh
static gboolean refresh(gpointer window); //run metapost
void get_figure(gpointer window); //convert the current figure to png
void adjust_darea_size(); //make darea fit the new png, or new scale

//Figure selection
int get_fig_index(); //return index of the current figure within the available figures array
void next_fig(gpointer window);
void prev_fig(gpointer window);
void select_figure(gpointer window);

//Scrolling
//When we zoom in/out, and when the window resizes, we want what was at the centre of the window to stay there, so we need to record where we were (with save_scroll_position, triggered whenever the scrollbars change position), and scroll back to it afterwards (with maintain_scroll, triggered at the end of adjust_darea_size)
void save_scroll_position();
void scroll_to(double x,double y);
static gboolean scroll_to_origin();
static gboolean maintain_scroll(); //scroll back to last saved position. called whenever darea is resized (eg after zooming)
void darea_coords(gdouble wx, gdouble wy, int *x, int *y); //translate event->x and y to darea coords, compensating for scrollbar position, and the menu bar
bool is_off_screen(double x, double y); //used to check if we should scroll to the position of a pasted path

//Zooming
void zoom_in();
void zoom_out();

//Drawing commands used by draw_path() etc
void fill_circle(double centre_x, double centre_y, int r);
void draw_circle(double centre_x, double centre_y, int r);
void draw_point(double centre_x, double centre_y);
void draw_bezier(double start_x, double start_y, double start_right_x, double start_right_y, double end_left_x, double end_left_y, double end_x, double end_y);
void link_point_pair(struct point *p, struct point *q);

//Copy/paste paths
void copy_to_clipboard(char *s);
void paste_path();

//Callbacks
static gboolean on_draw_event(GtkWidget *widget, cairo_t *this_cr, gpointer user_data);
static gboolean button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static gboolean button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static gboolean resize(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
static gboolean key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
static gboolean on_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data);

//Gtk stuff
static void setup_menus(GtkApplication* app, GtkWidget *window, GtkWidget *vbox);
static void activate (GtkApplication* app, gpointer user_data);
static void open(GApplication *app,
                 GFile         **files,
                 gint          n_files,
                 gchar        *hint,
                 gpointer      user_data);
//Process command-line options
static gboolean get_unit(gchar *option_name,gchar *value,gpointer data,GError **error);
static gboolean get_trace(gchar *option_name,gchar *value,gpointer data,GError **error);



void redraw_screen() {
	gtk_widget_queue_draw(darea);
}

/****************
Info Bar
****************/

void add_info_bar(GtkWidget *vbox) {
	GtkWidget *info_bar = gtk_info_bar_new ();
	message_label = gtk_label_new (get_info_msg());
	gtk_label_set_ellipsize(GTK_LABEL(message_label),PANGO_ELLIPSIZE_END);
	gtk_widget_show (message_label);
	GtkWidget *content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (info_bar));
	gtk_container_add (GTK_CONTAINER (content_area), message_label);
	gtk_box_pack_end(GTK_BOX(vbox), info_bar, FALSE, TRUE, 0);
}

gchar *get_info_msg() {
	//not mentioned: 'r' to refresh metapost, 't' to toggle trace, and 'p' to push path
	if (finished_drawing) {
		if (edit) {
			if (edit_point == -1) {
				return "Click and drag to move trace.";
			} else if (cur_path->n == -1) {
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
				return "Click to start drawing a curve. Press '-' for straight line mode, 'c' for circle mode, '?' to show help.";
			else if (mode == STRAIGHT_MODE)
				return "Click to start drawing straight lines. Press '.' for curve mode, 'c' for circle mode, '?' to show help.";
			else if (mode == CIRCLE_MODE)
				return "Click to start drawing a circle. Press '-' for straight line mode, '.' for curve mode, '?' to show help.";
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
	char *msg = get_info_msg();
	if (mp_png) {
		char s[strlen(msg) + 10];
		snprintf(s,sizeof(s),"Fig%d | %s",fig_num,msg);
		gtk_label_set_text (GTK_LABEL (message_label), s);
	} else
		gtk_label_set_text (GTK_LABEL (message_label), msg);

	//disable resume drawing menu item unless we have a resumable path
	if (!finished_drawing || cur_path->n < 1)
		gtk_widget_set_sensitive(resume_mi,false);
	//activate "End path" menu item if we're currently drawing a path
	if (!finished_drawing && mode != CIRCLE_MODE)
		gtk_widget_set_sensitive(end_mi,true);
}


/*********************
Drawing menu callbacks
*********************/

void curve_mode() {
	path_mode_change(false);
}
void straight_mode() {
	path_mode_change(true);
}
void circle_mode() {
	if (!finished_drawing) end_path();
	mode=CIRCLE_MODE;
	mode_change();
}

void resume_drawing(gpointer window) {
	if (finished_drawing && cur_path->n > 0) {
		finished_drawing = false;
		cur_path->cycle = false;
		struct point p = cur_path->points[cur_path->n-1];
		mode = p.straight ? STRAIGHT_MODE : CURVE_MODE;

		//get the coords of the pointer
		gint wx, wy;
		GdkDisplay *display = gdk_display_get_default ();
		GdkSeat *seat = gdk_display_get_default_seat (display);
		GdkDevice *device = gdk_seat_get_pointer (seat);
		gdk_window_get_device_position(gtk_widget_get_window(window),device,&wx,&wy,NULL);
		int x,y;
		darea_coords(wx,wy,&x,&y);

		append_point(
			pxl_to_mp_x_coord(x),
			pxl_to_mp_y_coord(y),
			p.straight);
		redraw_screen();
		mode_change();
	}
}


/*********************
Dialogs
*********************/

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
			"? = show this help dialog\n"

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

void open_dialog(GtkWidget *window) {
	GtkWidget *dialog;
	gint res;
	dialog = gtk_file_chooser_dialog_new (
		"Open File",
		GTK_WINDOW(window),
		GTK_FILE_CHOOSER_ACTION_OPEN,
		"Cancel",GTK_RESPONSE_CANCEL,
		"Open",GTK_RESPONSE_ACCEPT,
		NULL);

	//show only mp files
	GtkFileFilter *filter = gtk_file_filter_new ();
	gtk_file_filter_add_pattern(filter, "*.mp");
	gtk_file_filter_set_name (filter,"MetaPost Files");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog),filter);

	res = gtk_dialog_run (GTK_DIALOG (dialog));
	if (res == GTK_RESPONSE_ACCEPT) {
		g_free(mp_filename);
		cairo_surface_destroy(mp_png);
		mp_png = NULL; //needs to be NULL so that save_scroll_position() will choose the origin

		mp_filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (dialog));

		//change to the directory containing mp_filename so that mpost doesn't fail if they input a local file
		char dir[strlen(mp_filename)+1]; //need to copy the mp_filename string because dirname may alter it
		strcpy(dir,mp_filename);
		rm_tmp(); //get rid of any temporary files we may have created in current directory
		chdir(dirname(dir)); //POSIX only. Use _splitpath_s if porting to windows (http://stackoverflow.com/questions/21229214/how-to-get-the-directory-of-a-file-from-the-full-path-in-c)

		gtk_label_set_text (GTK_LABEL (message_label), "Running metapost...");
		g_idle_add(refresh,GTK_WINDOW(window));

		gtk_widget_set_sensitive(rerun_mi,true);
	}
	gtk_widget_destroy(dialog);
}

//dialog for changing units. Looks something like this:
/*
         Units: _____ [bp/mm/cm/in/pt] <-- dropdown box
Decimal points: __________
     Unit name: __________

E.g. (100,-200) will be printed as:
(10.0u,-20.0u) <-- this updates based on what units are chosen
OK    Cancel
*/
void units_preferences(gpointer window) {
	GtkWidget *dialog = gtk_dialog_new_with_buttons(
		"Units",
		window,
		GTK_DIALOG_DESTROY_WITH_PARENT, 
		"Cancel", GTK_RESPONSE_NONE,
		"OK", GTK_RESPONSE_OK, 
		NULL);
	GtkWidget *content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

	GtkWidget *grid = gtk_grid_new();
	gtk_container_add(GTK_CONTAINER(content_area), grid);

	//Units row
	GtkWidget *label = gtk_label_new("Units: ");
	gtk_widget_set_halign(label,GTK_ALIGN_END);
	gtk_grid_attach (GTK_GRID (grid), label, 0,0,1,1);

	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_grid_attach (GTK_GRID (grid), hbox, 1,0,1,1);
	GtkWidget *units_entry = gtk_entry_new();
	gtk_entry_set_width_chars(GTK_ENTRY(units_entry),4);
	gtk_box_pack_start(GTK_BOX(hbox), units_entry, FALSE, FALSE, 0);

	char unit_text[5];
	snprintf(unit_text,5,"%.2f", unit/units_multiplier(units_dropdown_index));
	gtk_entry_set_text(GTK_ENTRY(units_entry),unit_text);

	GtkWidget *units_dropdown = gtk_combo_box_text_new();
	gtk_box_pack_start(GTK_BOX(hbox), units_dropdown, FALSE, FALSE, 0);

	gtk_combo_box_text_append_text( GTK_COMBO_BOX_TEXT( units_dropdown ), "bp" );
	gtk_combo_box_text_append_text( GTK_COMBO_BOX_TEXT( units_dropdown ), "mm" );
	gtk_combo_box_text_append_text( GTK_COMBO_BOX_TEXT( units_dropdown ), "cm" );
	gtk_combo_box_text_append_text( GTK_COMBO_BOX_TEXT( units_dropdown ), "in" );
	gtk_combo_box_text_append_text( GTK_COMBO_BOX_TEXT( units_dropdown ), "pt" );
	gtk_combo_box_set_active(GTK_COMBO_BOX(units_dropdown),units_dropdown_index);
	
	//Precision row
	label = gtk_label_new("Decimal points: ");
	gtk_widget_set_halign(label,GTK_ALIGN_END);
	gtk_grid_attach (GTK_GRID (grid), label, 0,1,1,1);

	GtkWidget *prec_entry = gtk_entry_new();
	gtk_entry_set_width_chars(GTK_ENTRY(prec_entry),4);
	gtk_grid_attach (GTK_GRID (grid), prec_entry, 1,1,1,1);

	char prec_text[5];
	snprintf(prec_text,5,"%d",coord_precision);
	gtk_entry_set_text(GTK_ENTRY(prec_entry),prec_text);

	//Name row
	label = gtk_label_new("Unit name: ");
	gtk_widget_set_halign(label,GTK_ALIGN_END);
	gtk_grid_attach (GTK_GRID (grid), label, 0,2,1,1);

	GtkWidget *name_entry = gtk_entry_new();
	gtk_entry_set_width_chars(GTK_ENTRY(name_entry),4);
	gtk_entry_set_text(GTK_ENTRY(name_entry),unit_name);
	gtk_grid_attach (GTK_GRID (grid), name_entry, 1,2,1,1);
	
	//Example row
	label = gtk_label_new("");
	gtk_grid_attach (GTK_GRID (grid), label, 0,3,2,1);

	//Call units_update when the user changes something
	GtkWidget *widgets[6] = { units_entry, units_dropdown, prec_entry, name_entry, label, dialog };
	g_signal_connect(G_OBJECT(units_entry),    "changed", G_CALLBACK (units_update),widgets);
	g_signal_connect(G_OBJECT(units_dropdown), "changed", G_CALLBACK (units_update),widgets);
	g_signal_connect(G_OBJECT(prec_entry),     "changed", G_CALLBACK (units_update),widgets);
	g_signal_connect(G_OBJECT(name_entry),     "changed", G_CALLBACK (units_update),widgets);
	units_update(prec_entry,widgets);

	gtk_widget_show_all(content_area);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
		double bp = strtod(gtk_entry_get_text(GTK_ENTRY(units_entry)),NULL);
		if (bp != 0) { //OK button is deactivated when bp == 0 so this should always be true
			units_dropdown_index = gtk_combo_box_get_active(GTK_COMBO_BOX(units_dropdown));
			bp *= units_multiplier(units_dropdown_index);
			unit = bp;
			coord_precision = atoi(gtk_entry_get_text(GTK_ENTRY( prec_entry )));

			snprintf(unit_name,100,"%s",gtk_entry_get_text(GTK_ENTRY( name_entry )));

			printf("Changing units to %s%s, %s decimal points, unit name \"%s\"\n",
				gtk_entry_get_text(GTK_ENTRY(units_entry)),
				gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(units_dropdown)),
				gtk_entry_get_text(GTK_ENTRY(prec_entry)),
				unit_name);
		}
	}
	gtk_widget_destroy(dialog);
}

//get multiplier from index on the units dropdown
double units_multiplier(int i) {
	double multiplier;
	switch (i) {
		case 1: //mm
			multiplier = INCH/25.4;
			break;
		case 2: //cm
			multiplier = INCH/2.54;
			break;
		case 3: //in
			multiplier = INCH;
			break;
		case 4: //pt
			multiplier = INCH/72.27; //conversion between printer's points and postscript points
			break;
		default:
			multiplier = 1;
	}
	return multiplier;
}

//update the example line in the units dialog when the user changes something
void units_update(GtkWidget *widget, GtkWidget **widgets) {
	double bp = strtod(gtk_entry_get_text(GTK_ENTRY(widgets[0])),NULL);
	if (bp == 0) { //user hasn't typed a valid number yet
		gtk_dialog_set_response_sensitive(GTK_DIALOG(widgets[5]), GTK_RESPONSE_OK, FALSE); //disable OK button
		gtk_label_set_text (GTK_LABEL (widgets[4]), "\nE.g. (100,-200) will be printed as:\n...");
	} else {
		gtk_dialog_set_response_sensitive(GTK_DIALOG(widgets[5]), GTK_RESPONSE_OK, TRUE); //enable OK button
		bp *= units_multiplier(gtk_combo_box_get_active(GTK_COMBO_BOX(widgets[1])));

		int precision;
		//set precision appropriately if unit has changed
		if (widget == widgets[0] || widget == widgets[1]) {
			char prec_text[5];
			precision = ceil(log10(bp));
			snprintf(prec_text,5,"%d", precision);
			gtk_entry_set_text(GTK_ENTRY(widgets[2]),prec_text);
		} else
			precision = atoi(gtk_entry_get_text(GTK_ENTRY(widgets[2])));

		const gchar *name = gtk_entry_get_text(GTK_ENTRY(widgets[3]));
		char s[100];
		snprintf(s,100,"\nE.g. (100,-200) will be printed as:\n(%.*f%s,%.*f%s)",precision,100 / bp,name,precision,-200 / bp,name); 
		gtk_label_set_text (GTK_LABEL (widgets[4]), s);
	}
}


/**********************
Get png of the metapost
**********************/

void rerun_metapost(gpointer window) {
	if (mp_filename) {
		gtk_label_set_text (GTK_LABEL (message_label), "Running metapost...");
		//if we run refresh in the normal manner, then the infobar message won't show up. we have to use g_idle_add to wait until after the label is updated
		g_idle_add(refresh,window);
	}
}

//rerun metapost, convert to png
static gboolean refresh(gpointer window) {
	//these values will be overwritten if running metapost succeeds
	n_fig=0;
	cairo_surface_destroy(mp_png);
	mp_png = NULL;

	char tmp_filename[strlen(tmp_job_name)+4];
	sprintf(tmp_filename,"%s.mp",tmp_job_name);
	int ret = create_mp_file(mp_filename,tmp_filename);
	if (ret == 1) 
		show_error(window,"Couldn't open %s",mp_filename);
	else if (ret == 2)
		show_error(window,"Couldn't open %s.mp for writing",tmp_job_name);
	else if (run_mpost(tmp_job_name) != 0) {
		show_error(window,"%s","Error running metapost. See stdout for more details.");
	} else get_figure(window);

	//if there's only one fig then we don't need the menu items for changing figs
	if (n_fig > 1) {
		gtk_widget_set_sensitive(fig_mi,true);
		gtk_widget_set_sensitive(next_fig_mi,true);
		gtk_widget_set_sensitive(prev_fig_mi,true);
	} else {
		gtk_widget_set_sensitive(fig_mi,false);
		gtk_widget_set_sensitive(next_fig_mi,false);
		gtk_widget_set_sensitive(prev_fig_mi,false);
	}

	mode_change(); //replace info bar message
	adjust_darea_size();
	return FALSE;
}

void get_figure(gpointer window) {
	int ret = get_coords(tmp_job_name);
	if (ret != 0) {
		if (ret == 1) show_error(window,"Couldn't open %s.log.",tmp_job_name);
		else if (ret == 2) {
			if (n_fig == 0) show_error(window,"%s","No figures found.");
			else {
				fig_num = figures[0];
				printf("Falling back to figure %d...\n",fig_num);
				get_figure(window);
			}
		} else if (ret == 3) {
			show_error(window,"%s","Aborting due to missing font. See stderr for more details.");
		}
	} else if (make_png(tmp_job_name) != 0) {
		show_error(window,"%s","Error converting to png. See stdout for more details.");
	} else {
		char png[strlen(tmp_job_name)+5];
		sprintf(png,"%s.png",tmp_job_name);
		mp_png = cairo_image_surface_create_from_png(png);
		if (cairo_surface_status(mp_png) != CAIRO_STATUS_SUCCESS) {
			show_error(window,"Couldn't load %s",png);
			cairo_surface_destroy(mp_png);
			mp_png = NULL;
		}
	}
	adjust_darea_size();
}

void adjust_darea_size() {
	scrollbar_lock = true; //stop save_scroll_position from running before maintain_scroll
	unsigned int old_sketch_width = sketch_width;
	unsigned int old_sketch_height = sketch_height;
	int w = 0;
	int h = 0;
	if (mp_png) {
		w = cairo_image_surface_get_width(mp_png);
		h = cairo_image_surface_get_height(mp_png);
	} else {
		ll_x = 0; ll_y = 0; //put origin at centre of darea
	}
	y_offset = MP_BORDER*scale;
	x_offset = -MP_BORDER*scale;
	sketch_width  = scale*(2*MP_BORDER + w);
	sketch_height = scale*(2*MP_BORDER + h);

	//if window is bigger than darea, grow it to fit the window
	//height and width available to the darea calculated from window dimensions minus enough space for scrollbars, infobar, menubar. If we instead use the scrolled_window dimensions as the available height and width then things will jump around when the infobar changes size
	int avail_height = win_height - 80;
	int avail_width = win_width - 20;
	if (avail_width > (int) sketch_width) {
		x_offset -= (win_width - sketch_width)/2;
		sketch_width = avail_width;
	}
	if (avail_height > (int) sketch_height) {
		y_offset += (avail_height - sketch_height)/2;
		sketch_height = avail_height;
	}

	pixels_per_point = scale*density/INCH;

	//make sure maintain_scroll is run. It's triggered by the darea "size-allocate" event, which won't run if sketch_width and sketch_height are the same as before
	if (sketch_height == old_sketch_height && sketch_width == old_sketch_width)
		maintain_scroll();
	else
		gtk_widget_set_size_request(darea, sketch_width, sketch_height);

	redraw_screen();
}


/**********************
Figure selection
**********************/

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
	mode_change();
}

//return index of current figure in list of available figures, or return index of last figure if not found
int get_fig_index() {
	int i;
	for (i=0;i<n_fig;i++) {
		if (figures[i] == fig_num) break;
	}
	return i;
}
void next_fig(gpointer window) {
	fig_num = figures[(get_fig_index() + 1)%n_fig];
	get_figure(window);
	mode_change();
	scroll_to_origin();
}
void prev_fig(gpointer window) {
	fig_num = figures[(get_fig_index() + n_fig - 1)%n_fig];
	get_figure(window);
	mode_change();
	scroll_to_origin();
}


/**********************
Scrolling
**********************/

//save mp coords of centre of the screen (adjust_darea_size scrolls to the last saved position)
void save_scroll_position() {
	if (!scrollbar_lock) {
		GtkAllocation alloc;
		gtk_widget_get_allocation(scrolled_window, &alloc);

		if (alloc.width < sketch_width)
			scroll_centre_x = pxl_to_mp_x_coord(gtk_adjustment_get_value(hadj) + alloc.width/2);
		else
			scroll_centre_x = pxl_to_mp_x_coord(sketch_width/2);

		if (alloc.height < sketch_height)
			scroll_centre_y = pxl_to_mp_y_coord(gtk_adjustment_get_value(vadj) + win_height/2);
		else
			scroll_centre_y = pxl_to_mp_y_coord(sketch_height/2);
	}
}

//centre given mp coords in window
void scroll_to(double x,double y) {
	GtkAllocation alloc;
	gtk_widget_get_allocation(scrolled_window, &alloc);
	gtk_adjustment_set_value(hadj,mp_x_coord_to_pxl(x) - alloc.width/2);
	gtk_adjustment_set_value(vadj,mp_y_coord_to_pxl(y) - win_height/2);
}
static gboolean scroll_to_origin() {
	if (!mp_png || win_width == 0) return TRUE;
	scroll_to(0,0);
	return FALSE;
}
//when zooming in/out, or resizing the window, we want the centre of the window to stay fixed
static gboolean maintain_scroll() {
	scroll_to(scroll_centre_x, scroll_centre_y);
	scrollbar_lock = false; //allow save_scroll_position to run now that adjust_darea_size has finished
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

bool is_off_screen(double x, double y) {
	int screen_x = mp_x_coord_to_pxl(x) - gtk_adjustment_get_value(hadj);
	int screen_y = mp_y_coord_to_pxl(y) - gtk_adjustment_get_value(vadj);
	GtkAllocation alloc;
	GtkWidget *child = gtk_bin_get_child(GTK_BIN(scrolled_window));
	gtk_widget_get_allocation(child, &alloc);
	return (screen_x < 0 || screen_x > alloc.width || screen_y < 0 || screen_y > alloc.height);
}


/**********************
Zooming
**********************/

void zoom_in() {
	scale *= 1.5;
	adjust_darea_size();
}

void zoom_out() {
	scale /= 1.5;
	adjust_darea_size();
}


/***************************************
Drawing commands used by draw_path() etc
***************************************/

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


/**********************
Callbacks
**********************/

static gboolean on_draw_event(GtkWidget *widget, cairo_t *this_cr, gpointer user_data) {
	cr = this_cr; //make it global. Otherwise I'd have to pass it to draw_path which would no longer be common between the xlib and gtk versions

	if (show_trace && trace) {
		cairo_set_source_surface(cr, trace,
			mp_x_coord_to_pxl(trace_x), 
			mp_y_coord_to_pxl(trace_y));
		cairo_paint(cr);
	}

	if (mp_png) {
		cairo_set_source_surface(cr, mp_png, -x_offset/scale -1, y_offset/scale); //since adding the scrolled_window, it was off by 1

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

	if (show_trace && trace) {
		//make a border around the trace so it's clear the point goes with the trace
		cairo_rectangle(cr,
			mp_x_coord_to_pxl(trace_x), 
			mp_y_coord_to_pxl(trace_y),
			cairo_image_surface_get_width(trace),
			cairo_image_surface_get_height(trace));
		static const double dashed1[] = {10.0,10.0};
		cairo_set_dash(cr, dashed1, 2, 1);
		cairo_stroke(cr);
		cairo_set_dash(cr, NULL, 0, 1);
		//draw the edit point at top left of trace
		draw_point(trace_x, trace_y);
	}

	draw_path();

	highlight_edit_point();

	return FALSE;
}

static gboolean button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	if (event->button == 1 && edit) dragging_point = true;
	return FALSE;
}
static gboolean button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	if (event->button == 1) {
		int x,y;
		darea_coords(event->x,event->y,&x,&y);
		//For some reason gtk has started sending two button release events every time (as of May 2018, vs back in 2017 when I originally wrote this). I don't know how to fix this so we'll just reject any click that comes too soon after the last one
		if (clock() - last_click < 100)
			click_point(x,y);
		last_click = clock();
	}
	return FALSE;
}

static gboolean resize(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	gtk_window_get_size(GTK_WINDOW(widget), &win_width, &win_height);
	adjust_darea_size();
	return FALSE;
}

static gboolean key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	///usr/include/gtk-3.0/gdk/gdkkeysyms.h
	if (!dragging_point) {
		switch(event->keyval) {
			/*case GDK_KEY_Escape:
				if (finished_drawing) { //clear
					cur_path->n = 0;
					redraw_screen();
				} else
					end_path();
					//activate the resume drawing menu item
					if (mode != CIRCLE_MODE)
						gtk_widget_set_sensitive(resume_mi,true);
				break;*/
			case GDK_KEY_Return: //cycle
				if (!finished_drawing) {
					cur_path->cycle = true;
					end_path();
					//activate the resume drawing menu item
					if (mode != CIRCLE_MODE)
						gtk_widget_set_sensitive(resume_mi,true);
				} else if (edit) {
					cur_path->cycle = !cur_path->cycle;
					redraw_screen();
				}
				break;
			case GDK_KEY_i: //insert point before edit_point
				if (edit && edit_point >= 0) {
					point_before(edit_point);
					edit_point++;
					redraw_screen();
				}
				break;
			case GDK_KEY_a: //append point after edit_point
				if (edit && edit_point >= 0) {
					point_before(edit_point+1);
					redraw_screen();
					mode_change();
				}
				break;
			case GDK_KEY_d: //delete point
				if (edit && edit_point >= 0) {
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
				paste_path();
				break;
		}
	}

	return FALSE;
}

static gboolean on_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
	int x, y;
	darea_coords(event->x,event->y,&x,&y);

	//For some reason we now (2018 vs 2017) don't need to compensate for the menubar while the mouse is up, so we need to undo the compensation that darea_coords did. Don't ask me why this has changed or why it doesn't affect motion while the mouse is down
	if (!(event->state & GDK_BUTTON1_MASK)) {
		GtkAllocation alloc;
		gtk_widget_get_allocation(scrolled_window, &alloc);
		y+=alloc.y;
	}

	pointer_move(x,y);
	return FALSE;
}


/**********************
Copy/paste paths
**********************/

void copy_to_clipboard(char *s) {
	gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), s, -1);

	//Show it in the info bar too
	char msg[strlen(s) + strlen("Copied to clipboard: ") + 1];
	sprintf(msg,"Copied to clipboard: %s",s);
	gtk_label_set_text(GTK_LABEL (message_label), msg);

	//activate resume menu item after ending a path
	if (mode != CIRCLE_MODE)
		gtk_widget_set_sensitive(resume_mi,true);
	//deactivate "End path" menu item
	if (finished_drawing)
		gtk_widget_set_sensitive(end_mi,false);
}

void paste_path() {
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


/**********************
Gtk stuff
**********************/

static void setup_menus(GtkApplication* app, GtkWidget *window, GtkWidget *vbox) {
	GtkWidget *menubar = gtk_menu_bar_new();

	GtkAccelGroup *accel_group = gtk_accel_group_new();
	gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);

	//File menu
	GtkWidget *file_menu = gtk_menu_new();
	GtkWidget *file_mi = gtk_menu_item_new_with_label("File");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_mi), file_menu);

	GtkWidget *open_mi = gtk_menu_item_new_with_label("Open...");
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), open_mi);
	g_signal_connect_swapped(G_OBJECT(open_mi), "activate", G_CALLBACK (open_dialog), window);
	gtk_widget_add_accelerator(open_mi, "activate", accel_group, GDK_KEY_o, 0, GTK_ACCEL_VISIBLE);

	rerun_mi = gtk_menu_item_new_with_label("Rerun MetaPost");
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), rerun_mi);
	g_signal_connect_swapped(G_OBJECT(rerun_mi), "activate", G_CALLBACK (rerun_metapost), window);
	gtk_widget_add_accelerator(rerun_mi, "activate", accel_group, GDK_KEY_r, 0, GTK_ACCEL_VISIBLE);
	if (!mp_filename) gtk_widget_set_sensitive(rerun_mi,false);

	fig_mi = gtk_menu_item_new_with_label("Change figure...");
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), fig_mi);
	g_signal_connect_swapped(G_OBJECT(fig_mi), "activate", G_CALLBACK (select_figure), window);
	gtk_widget_add_accelerator(fig_mi, "activate", accel_group, GDK_KEY_f, 0, GTK_ACCEL_VISIBLE);
	gtk_widget_set_sensitive(fig_mi,false);

	next_fig_mi = gtk_menu_item_new_with_label("Next figure");
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), next_fig_mi);
	g_signal_connect_swapped(G_OBJECT(next_fig_mi), "activate", G_CALLBACK (next_fig),window);
	gtk_widget_add_accelerator(next_fig_mi, "activate", accel_group, GDK_KEY_Page_Down, 0, GTK_ACCEL_VISIBLE);
	gtk_widget_set_sensitive(next_fig_mi,false);

	prev_fig_mi = gtk_menu_item_new_with_label("Previous figure");
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), prev_fig_mi);
	g_signal_connect_swapped(G_OBJECT(prev_fig_mi), "activate", G_CALLBACK (prev_fig),window);
	gtk_widget_add_accelerator(prev_fig_mi, "activate", accel_group, GDK_KEY_Page_Up, 0, GTK_ACCEL_VISIBLE);
	gtk_widget_set_sensitive(prev_fig_mi,false);

	GtkWidget *help_mi = gtk_menu_item_new_with_label("Help");
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), help_mi);
	g_signal_connect_swapped(G_OBJECT(help_mi), "activate", G_CALLBACK (show_help), window);
	gtk_widget_add_accelerator(help_mi, "activate", accel_group, GDK_KEY_question, 0, GTK_ACCEL_VISIBLE);

	GtkWidget *quit_mi = gtk_menu_item_new_with_label("Quit");
	gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), quit_mi);
	g_signal_connect_swapped(G_OBJECT(quit_mi), "activate", G_CALLBACK (g_application_quit), G_APPLICATION(app));
	gtk_widget_add_accelerator(quit_mi, "activate", accel_group, GDK_KEY_q, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

	//Edit menu
	GtkWidget *edit_menu = gtk_menu_new();
	GtkWidget *edit_mi = gtk_menu_item_new_with_label("Edit");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(edit_mi), edit_menu);

	GtkWidget *copy_mi = gtk_menu_item_new_with_label("Copy path");
	gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), copy_mi);
	g_signal_connect_swapped(G_OBJECT(copy_mi), "activate", G_CALLBACK (output_path), window);
	gtk_widget_add_accelerator(copy_mi, "activate", accel_group, GDK_KEY_c, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

	GtkWidget *paste_mi = gtk_menu_item_new_with_label("Paste path");
	gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), paste_mi);
	g_signal_connect_swapped(G_OBJECT(paste_mi), "activate", G_CALLBACK (paste_path), window);
	gtk_widget_add_accelerator(paste_mi, "activate", accel_group, GDK_KEY_v, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

	GtkWidget *sep = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), sep);

	GtkWidget *units_mi = gtk_menu_item_new_with_label("Units...");
	gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), units_mi);
	g_signal_connect_swapped(G_OBJECT(units_mi), "activate", G_CALLBACK (units_preferences), window);
	gtk_widget_add_accelerator(units_mi, "activate", accel_group, GDK_KEY_u, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

	//View menu
	GtkWidget *view_menu = gtk_menu_new();
	GtkWidget *view_mi = gtk_menu_item_new_with_label("View");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(view_mi), view_menu);

	GtkWidget *zoom_in_mi = gtk_menu_item_new_with_label("Zoom in");
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), zoom_in_mi);
	g_signal_connect_swapped(G_OBJECT(zoom_in_mi), "activate", G_CALLBACK (zoom_in), window);
	gtk_widget_add_accelerator(zoom_in_mi, "activate", accel_group, GDK_KEY_plus, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

	GtkWidget *zoom_out_mi = gtk_menu_item_new_with_label("Zoom out");
	gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), zoom_out_mi);
	g_signal_connect_swapped(G_OBJECT(zoom_out_mi), "activate", G_CALLBACK (zoom_out), window);
	gtk_widget_add_accelerator(zoom_out_mi, "activate", accel_group, GDK_KEY_minus, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

	//Sketch menu
	GtkWidget *sketch_menu = gtk_menu_new();
	GtkWidget *sketch_mi = gtk_menu_item_new_with_label("Sketch");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(sketch_mi), sketch_menu);

	end_mi = gtk_menu_item_new_with_label("End path");
	gtk_menu_shell_append(GTK_MENU_SHELL(sketch_menu), end_mi);
	g_signal_connect_swapped(G_OBJECT(end_mi), "activate", G_CALLBACK (end_path), window);
	gtk_widget_add_accelerator(end_mi, "activate", accel_group, GDK_KEY_Escape, 0, GTK_ACCEL_VISIBLE);
	gtk_widget_set_sensitive(end_mi,false);

	resume_mi = gtk_menu_item_new_with_label("Resume drawing path");
	gtk_menu_shell_append(GTK_MENU_SHELL(sketch_menu), resume_mi);
	g_signal_connect_swapped(G_OBJECT(resume_mi), "activate", G_CALLBACK (resume_drawing), window);
	gtk_widget_add_accelerator(resume_mi, "activate", accel_group, GDK_KEY_k, 0, GTK_ACCEL_VISIBLE);
	gtk_widget_set_sensitive(resume_mi,false);

	sep = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(sketch_menu), sep);

	//Drawing modes: curve/straight/circle
	GSList *group = NULL;

	GtkWidget *curve_mi = gtk_radio_menu_item_new_with_label (group,".. Curve mode");
	gtk_menu_shell_append(GTK_MENU_SHELL(sketch_menu), curve_mi);
	g_signal_connect(G_OBJECT(curve_mi), "activate", G_CALLBACK (curve_mode), NULL);
	gtk_widget_add_accelerator(curve_mi, "activate", accel_group, GDK_KEY_period, 0, GTK_ACCEL_VISIBLE);
	group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(curve_mi));

	GtkWidget *straight_mi = gtk_radio_menu_item_new_with_label (group,"-- Straight line mode");
	gtk_menu_shell_append(GTK_MENU_SHELL(sketch_menu), straight_mi);
	g_signal_connect(G_OBJECT(straight_mi), "activate", G_CALLBACK (straight_mode), NULL);
	gtk_widget_add_accelerator(straight_mi, "activate", accel_group, GDK_KEY_minus, 0, GTK_ACCEL_VISIBLE);

	GtkWidget *circle_mi = gtk_radio_menu_item_new_with_label (group,"Circle mode");
	gtk_menu_shell_append(GTK_MENU_SHELL(sketch_menu), circle_mi);
	g_signal_connect(G_OBJECT(circle_mi), "activate", G_CALLBACK (circle_mode), NULL);
	gtk_widget_add_accelerator(circle_mi, "activate", accel_group, GDK_KEY_c, 0, GTK_ACCEL_VISIBLE);


	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file_mi);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), edit_mi);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), view_mi);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), sketch_mi);
	gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);
}

void setup_darea(GtkWidget *vbox) {
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
	g_signal_connect(hadj,"value-changed",G_CALLBACK(save_scroll_position),NULL);
	g_signal_connect(vadj,"value-changed",G_CALLBACK(save_scroll_position),NULL);
}

static void activate (GtkApplication* app, gpointer user_data) {
	GtkWidget *window;

	initialise();

	window = gtk_application_window_new (app);
	gtk_window_set_title (GTK_WINDOW (window), "MPSketch");
	gtk_window_set_default_size (GTK_WINDOW (window), 500, 500);

	gtk_widget_add_events(window, 
		GDK_BUTTON_PRESS_MASK | 
		GDK_POINTER_MOTION_MASK |
		GDK_BUTTON_RELEASE_MASK |
		GDK_SCROLL_MASK);

	g_signal_connect(window, "button-press-event",		G_CALLBACK(button_press),	NULL);
	g_signal_connect(window, "button-release-event",	G_CALLBACK(button_release), NULL);
	g_signal_connect(window, "key-press-event",			G_CALLBACK(key_press),		NULL);
	g_signal_connect(window, "motion-notify-event",		G_CALLBACK(on_motion),		NULL);
	g_signal_connect(window, "check-resize",			G_CALLBACK(resize),			NULL);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	setup_menus(app,window,vbox);

	setup_darea(vbox);

	add_info_bar(vbox);

	gtk_widget_show_all (window);
	density = 100;
	scale = 1;
	pixels_per_point = scale*density/INCH;
	if (mp_filename) refresh(window);
}

static void open(GApplication *app,
                 GFile         **files,
                 gint          n_files,
                 gchar        *hint,
                 gpointer      user_data) {

	mp_filename = g_file_get_parse_name(files[0]);
	//check that it ends in ".mp"
	if (strcmp(mp_filename + strlen(mp_filename) - 3,".mp") == 0) {
		printf("Opening %s\n",mp_filename);
		activate(GTK_APPLICATION (app),NULL);
	} else {
		g_print("Argument is not an mp file\n");
	}
}

//Process command-line options
static gboolean get_unit(gchar *option_name,gchar *value,gpointer data,GError **error) {
	bool valid;
	unit = string_to_bp(value,&valid);
	if (!valid) {
		puts("Invalid measure in argument of -u");
		puts("Usage: mpsketch -u <number>[cm|mm|in|pt|bp]");
		puts("e.g. mpsketch -u 2cm");
		return FALSE;
	}

	//remember what units it was written in, for units preferences dialog
	char *unit_part; strtod(value,&unit_part);
	if (*unit_part != 0) {
		if (strcmp(unit_part,"mm") == 0)
			units_dropdown_index = 1;
		else if (strcmp(unit_part,"cm") == 0)
			units_dropdown_index = 2;
		else if (strcmp(unit_part,"in") == 0)
			units_dropdown_index = 3;
		else if (strcmp(unit_part,"pt") == 0)
			units_dropdown_index = 4;
	}
	
	//choose a sensible number of decimal places for coordinates
	coord_precision = ceil(log10(unit)); 
	//printf("unit=%f\n",unit);
	//printf("precision=%d\n",coord_precision);
	return TRUE;
}
static gboolean get_trace(gchar *option_name,gchar *value,gpointer data,GError **error) {
	show_trace = true;
	trace_x = 0;
	trace_y = 0;
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
	unit_name = malloc(100 * sizeof *unit_name);
	*unit_name = '\0';
	units_dropdown_index = 0;
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

	app = gtk_application_new (NULL, G_APPLICATION_HANDLES_OPEN);
	g_application_add_main_option_entries(G_APPLICATION(app),entries);
	g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
	g_signal_connect (app, "open", G_CALLBACK (open), NULL);
	status = g_application_run (G_APPLICATION (app), argc, argv);
	g_object_unref (app);
	g_free(mp_filename);

	cairo_surface_destroy(mp_png);
	cairo_surface_destroy(trace);

	cleanup();
	free(unit_name);

	return status;
}

