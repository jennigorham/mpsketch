#include "common.h"

void draw_path() {
	if (cur_path->n > 1) { //path with multiple points
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
	} else if (cur_path->n == 1 && finished_drawing) { //single point
		draw_circle(cur_path->points[0].x,cur_path->points[0].y,POINT_RADIUS); 
	} else if ((mode == CIRCLE_MODE && !finished_drawing) || cur_path->n == -1) {//a circle is being drawn or has just been drawn
		double delta_x,delta_y, r;
		delta_x = cur_path->points[0].x - cur_path->points[1].x;
		delta_y = cur_path->points[0].y - cur_path->points[1].y;
		r = sqrt(delta_x*delta_x + delta_y*delta_y) / INCH * density;

		draw_circle(cur_path->points[0].x,cur_path->points[0].y,(int) r);
		draw_circle(cur_path->points[0].x,cur_path->points[0].y,POINT_RADIUS);
		draw_circle(cur_path->points[1].x,cur_path->points[1].y,POINT_RADIUS);
	}
}

//Converting between metapost coords and pixels from upper left corner
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

void output_path() {
	char *s = path_to_string();
	copy_to_clipboard(s);
	if (cur_path->n > 1 || cur_path->n == -1)
		printf("draw %s;\n",s);
	else if (cur_path->n == 1)
		printf("drawdot %s;\n",s);
}

void click_point(int x, int y) {
	if (mode == CIRCLE_MODE) {
		if (finished_drawing) { //start a new circle
			cur_path->n = 0;
			set_coords(0,pxl_to_mp_x_coord(x),pxl_to_mp_y_coord(y));
			set_coords(1,pxl_to_mp_x_coord(x),pxl_to_mp_y_coord(y));
			finished_drawing = false;
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
			append_point(
				pxl_to_mp_x_coord(x),
				pxl_to_mp_y_coord(y),
				false
			);
		}
		//point under cursor
		append_point(
			pxl_to_mp_x_coord(x),
			pxl_to_mp_y_coord(y),
			false
		);
	}
	redraw_screen();
}

void end_path() {
	cur_path->n--; //remove the extra point under the cursor. for circles, sets n to -1 so we know it's a circle
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
