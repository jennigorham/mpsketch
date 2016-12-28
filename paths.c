#include "paths.h"

void init_path(struct path *p) {
	p->cycle = false;
	p->size = 10;
	p->points = malloc(p->size * sizeof *p->points);
	p->n = 0;
}

//get a path from a string. Experimental, currently does no checking
void get_path(char *s) {
	int i=1;
	int j=i;
	double x,y;
	cur_path->n = 0;
	cur_path->cycle = false;
	int l = strlen(s);
	while (true) {
		while (j<l-1 && s[j] != ',') j++;
		s[j] = '\0';
		x = strtod(s+i,NULL);

		i=j+1;
		if (i >= l) break;
		while (j<l-1 && s[j] != ')') j++;
		s[j] = '\0';
		y = strtod(s+i,NULL);

		append_point(x,y,false);

		if (j+1>=l || s[j+1] == '\0') break;
		set_last_straight(s[j+1] != '.');
		i = j+4;
		if (i>=l) break;
		if (s[i] == 'y') {
			cur_path->cycle = true;
			break;
		}
	}
}

//find the bezier control points for all the points on the path
void get_controls(struct path *p) {
	MP mp;
	mp_knot first_knot, last_knot;
	MP_options * opt = mp_options () ;
	opt -> command_line = NULL;
	opt -> noninteractive = 1 ;
	mp = mp_initialize ( opt ) ;
	if ( ! mp ) exit ( EXIT_FAILURE ) ;

	first_knot = mp_append_knot(mp,NULL,cur_path->points[0].x,cur_path->points[0].y);
	last_knot = first_knot;
	int i;
	for (i=1;i<cur_path->n;i++) {
		last_knot = mp_append_knot(mp,last_knot,cur_path->points[i].x,cur_path->points[i].y);
		//knot curls must be set to 1 for knots on either side of a straight section
		if (cur_path->points[i].straight || cur_path->points[i-1].straight)
			mp_set_knot_curl(mp,last_knot,1.0);
	}

	if (cur_path->cycle) {
		mp_close_path_cycle(mp, last_knot, first_knot);
		//knot curls for first and last knot need to be set AFTER the path is closed
		if (cur_path->points[0].straight) mp_set_knot_curl(mp,first_knot,1.0);
		if (cur_path->points[cur_path->n-1].straight) mp_set_knotpair_curls(mp,first_knot,last_knot,1.0,1.0); //straight line from last knot to first
	} else {
		mp_set_knotpair_curls(mp, first_knot, last_knot, 1.0,1.0); //standard curl of end points on curved path
		mp_close_path(mp, last_knot, first_knot);
	}

	if (mp_solve_path(mp, first_knot)) {
		int i;
		mp_knot p; p = first_knot;
		for (i=0; i<cur_path->n; i++) {
			cur_path->points[i].left_x  = mp_number_as_double(mp,p->left_x);
			cur_path->points[i].left_y  = mp_number_as_double(mp,p->left_y);
			cur_path->points[i].right_x = mp_number_as_double(mp,p->right_x);
			cur_path->points[i].right_y = mp_number_as_double(mp,p->right_y);
			p = mp_knot_next(mp,p);
		}
	}
	mp_free_path(mp, first_knot);
	
	mp_finish ( mp ) ;
	free(opt);
}

//commands for setting properties of a point
void set_straight(int i,bool is_straight) {
	cur_path->points[i].straight = is_straight;
}
void set_coords(int i,double x,double y) {
	cur_path->points[i].x = x;
	cur_path->points[i].y = y;
}
void set_point(int i, double x, double y, bool is_straight) {
	set_coords(i,x,y);
	set_straight(i,is_straight);
}

//set properties of the last point in the path
void set_last_straight(bool is_straight) {
	set_straight(cur_path->n-1,is_straight);
}
void set_last_coords(double x,double y) {
	set_coords(cur_path->n-1,x,y);
}
void set_last_point(double x, double y, bool is_straight) {
	set_point(cur_path->n-1,x,y,is_straight);
}

void append_point(double x, double y, bool is_straight) {
	//allocate more space for points if necessary
	if (cur_path->n == cur_path->size) {
		cur_path->size *= 2;
		cur_path->points = realloc(cur_path->points,cur_path->size * sizeof *cur_path->points);
	}
	cur_path->n++;
	set_last_point(x,y,is_straight);
}

//return the metapost string describing the path, eg (0,0)..(5,23)..(16,-27)
char *path_string() {
	size_t size = 1000; //initial size of string. enough to hold "fullcircle ..."
	char *s = malloc(size * sizeof *s);
	strcpy(s,"");
	if (cur_path->n == -1) { //circle
		double r,delta_x,delta_y;
		delta_x = cur_path->points[0].x - cur_path->points[1].x;
		delta_y = cur_path->points[0].y - cur_path->points[1].y;
		r = sqrt(delta_x*delta_x + delta_y*delta_y);
		snprintf(s, size, "fullcircle scaled %.*f shifted (%.*f,%.*f)",
			PRECISION, 2*r,
			PRECISION, cur_path->points[0].x,
			PRECISION, cur_path->points[0].y
		);
	} else if (cur_path->n == 1) { //point
		snprintf(s,size,"(%.*f,%.*f)",PRECISION,cur_path->points[0].x,PRECISION,cur_path->points[0].y);
	} else if (cur_path->n > 1) { //path
		char point[30];
		int i;
		for (i=0;i<cur_path->n;i++) {
			snprintf(point,sizeof(point),"(%.*f,%.*f)",
				PRECISION, cur_path->points[i].x,
				PRECISION, cur_path->points[i].y
			); //should i print a warning here if there isn't enough space to fit the string? but that will never happen and wouldn't be a big problem if it did

			if (strlen(s) + sizeof(point) + 20 > size) { //maybe not enough space to fit, so grow s
				size *= 2;
				s = realloc(s,size * sizeof *s);
			}
			strcat(s,point);
			if (i < cur_path->n-1 || cur_path->cycle) {
				if (cur_path->points[i].straight) strcat(s,"--");
				else strcat(s,"..");
			}
		}
		if (cur_path->cycle) strcat(s,"cycle");
	}
	return s;
}
