#include "common.h"

void draw_path() {
	if (cur_path->n > 1) { //path with multiple points
		find_control_points();
		
		int i;
		struct point *p,*q;
		for (i=0; i<cur_path->n-1; i++) {
			p = &cur_path->points[i];
			q = &cur_path->points[i+1];
			draw_point(p->x,p->y);
			link_point_pair(p,q);
		}
		p = &cur_path->points[cur_path->n-1];
		draw_point(p->x,p->y);
		if (cur_path->cycle) {
			q = &cur_path->points[0];
			link_point_pair(p,q);
		}
	} else if (cur_path->n == 1 && finished_drawing) { //single point
		draw_point(cur_path->points[0].x,cur_path->points[0].y); 
	} else if ((mode == CIRCLE_MODE && !finished_drawing) || cur_path->n == -1) {//a circle is being drawn or has just been drawn
		double delta_x,delta_y, r;
		delta_x = cur_path->points[0].x - cur_path->points[1].x;
		delta_y = cur_path->points[0].y - cur_path->points[1].y;
		r = sqrt(delta_x*delta_x + delta_y*delta_y) * pixels_per_point;

		draw_circle(cur_path->points[0].x,cur_path->points[0].y,(int) r);
		draw_point(cur_path->points[0].x,cur_path->points[0].y);
		draw_point(cur_path->points[1].x,cur_path->points[1].y);
	}
}

//Converting between metapost coords and pixels from upper left corner
int mp_x_coord_to_pxl(double x) {
	return round((x - ll_x)*pixels_per_point - x_offset);
}
int mp_y_coord_to_pxl(double y) {
	return round(sketch_height - y_offset - (y - ll_y)*pixels_per_point);
}
double pxl_to_mp_x_coord(int x) {
	return ((x_offset + ((float) x))/pixels_per_point + ll_x);
}
double pxl_to_mp_y_coord(int y) {
	return ((-y_offset + sketch_height-((float) y))/pixels_per_point + ll_y);
}

void output_path() {
	char *s = path_to_string();
	if (s == NULL) {
		fprintf(stderr,"Couldn't allocate memory.\n");
	} else {
		copy_to_clipboard(s);
		if (cur_path->n > 1 || cur_path->n == -1)
			printf("draw %s;\n",s);
		else if (cur_path->n == 1)
			printf("drawdot %s;\n",s);
		free(s);
	}
}

void click_point(int x, int y) {
	if (dragging_point) {
		dragging_point = false;
		set_coords(
			edit_point,
			pxl_to_mp_x_coord(x),
			pxl_to_mp_y_coord(y)
		);
		redraw_screen();
	} else if (mode == CIRCLE_MODE) {
		if (finished_drawing) { //start a new circle
			cur_path->n = 0;
			set_coords(0,pxl_to_mp_x_coord(x),pxl_to_mp_y_coord(y));
			set_coords(1,pxl_to_mp_x_coord(x),pxl_to_mp_y_coord(y));
			finished_drawing = false;
			mode_change();
		} else {
			set_coords(1,pxl_to_mp_x_coord(x),pxl_to_mp_y_coord(y));
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
			if (append_point( pxl_to_mp_x_coord(x), pxl_to_mp_y_coord(y), false) != 0) 
				fprintf(stderr,"Couldn't allocate memory for extra point.\n");
		}
		//point under cursor
		if (append_point( pxl_to_mp_x_coord(x), pxl_to_mp_y_coord(y), false) != 0) 
			fprintf(stderr,"Couldn't allocate memory for extra point.\n");
		mode_change();
	}
	redraw_screen();
}

void end_path() {
	if (mode == CIRCLE_MODE) cur_path->n = -1;
	output_path();
	finished_drawing=true;
	redraw_screen();
}

void path_mode_change(bool is_straight) {
	if (!finished_drawing) {
		if (mode==CIRCLE_MODE) end_path();
		else set_straight(cur_path->n-2,is_straight);
	}
	else if (edit) set_straight(edit_point,is_straight);

	if (is_straight) mode=STRAIGHT_MODE;
	else mode = CURVE_MODE;

	redraw_screen();
}

void pointer_move(int x,int y) {
	if (!finished_drawing) {
		set_coords(mode==CIRCLE_MODE ? 1 : cur_path->n-1,pxl_to_mp_x_coord(x),pxl_to_mp_y_coord(y));
		redraw_screen();
	} else if (dragging_point) {
		set_coords(
			edit_point,
			pxl_to_mp_x_coord(x),
			pxl_to_mp_y_coord(y)
		);
		redraw_screen();
	} else {
		//if the user mouses over a point on the path, they can edit it (drag it, change the section after it to straight/curved)
		int i;
		bool found_point=false;
		for (i=0;i<(cur_path->n == -1 ? 2 : cur_path->n);i++) {
			int delta_x, delta_y;
			delta_x = mp_x_coord_to_pxl(cur_path->points[i].x) - x;
			delta_y = mp_y_coord_to_pxl(cur_path->points[i].y) - y;
			if (delta_x*delta_x + delta_y*delta_y < POINT_RADIUS*POINT_RADIUS) {
				found_point=true;
				break;
			}
		}
		//redraw screen if edit or edit_point have changed
		if (found_point != edit || edit_point != i) {
			edit = found_point;
			edit_point = i;
			mode_change(); //for gtk to update info bar
			redraw_screen();
		}
	}
}

void initialise() {
	finished_drawing=true;
	mode = CURVE_MODE;
	edit=false;

	pid_t pid = getpid();
	snprintf(tmp_job_name,sizeof tmp_job_name,"mpsketch-tmp-%d",pid);

	cur_path = malloc(sizeof(struct path));
	if (cur_path == NULL) {
		fprintf(stderr,"Couldn't allocate memory.\n");
		exit(EXIT_FAILURE);
	} else {
		init_path(cur_path);
		if (cur_path->points == NULL) {
			fprintf(stderr,"Couldn't allocate memory.\n");
			free(cur_path);
			exit(EXIT_FAILURE);
		}
	}
}
void cleanup() {
	if (cur_path) {
		if (cur_path->points) free(cur_path->points);
		free(cur_path);
	}

	//Get rid of temporary files. Not portable, but it's the easiest way for now
	if (*tmp_job_name != '\0') {
		char cmd[6 + strlen(tmp_job_name)];
		sprintf(cmd,"rm %s.*",tmp_job_name);
		system(cmd);
	}
}
