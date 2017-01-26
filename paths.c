#include "paths.h"

void init_path(struct path *p) {
	p->cycle = false;
	p->size = 10;
	p->points = malloc(p->size * sizeof *p->points);
	p->n = 0;
}

//return number of postscript points (aka big points, bp) from strings like 'cm', '5.2in', etc
//string can have leading spaces, but not trailing spaces
double string_to_bp(char *s,bool *valid) {
	if (*s == '\0') {
		if (valid) *valid = false;
		return 0;
	}
	double bp;
	char *unit_part; //pointer to the rest of the string following a valid number
	if (valid) *valid = true;
	bp = strtod(s,&unit_part);
	if (unit_part == s) bp = 1; //string can be just 'cm' for example
	if (*unit_part != 0) {
		if (strcmp(unit_part,"mm") == 0)
			bp *= INCH/25.4;
		else if (strcmp(unit_part,"cm") == 0)
			bp *= INCH/2.54;
		else if (strcmp(unit_part,"in") == 0)
			bp *= INCH;
		else if (strcmp(unit_part,"pt") == 0)
			bp *= INCH/72.27; //conversion between printer's points and postscript points
		else if (strcmp(unit_part,unit_name) == 0)
			bp *= unit;
		else if (strcmp(unit_part,"bp") != 0)
			if (valid) *valid = false;
	//if there's no units on the number then assume they're in the default units. But if unit_name is set then assume they're in postscript points and leave as is
	} else if (*unit_name == '\0') bp *= unit;
	return bp;
}

//parse something like "(5cm,-3cm)". Returns pointer to next character after valid point string
char *string_to_point(char *s, double *x, double *y) {
	//check that it starts with '(' and has a comma
	if (*s != '(') return s;
	char *comma = strchr(s+1,',');
	if (comma == NULL) return s;

	//get x coord
	*comma = '\0'; //mark end of first coord
	bool valid;
	*x = string_to_bp(s+1,&valid);
	if (!valid) return s;

	//check for closing bracket
	char *bracket = strchr(comma+1,')');
	if (bracket == NULL) return s;

	//get y coord
	*bracket = '\0'; //mark end of 2nd coord
	*y = string_to_bp(comma+1,&valid);
	if (!valid) return s;

	return bracket + 1;
}

//Get a path from a string (store in cur_path). Returns pointer to next char after valid path string
char *string_to_path(char *s) {
	char *end;
	double x,y;
	cur_path->n = 0;
	cur_path->cycle = false;

	//check for circle strings, eg "fullcircle scaled 2cm shifted (-1cm,1cm)"
	char *substring = "fullcircle scaled ";
	if (strncmp(s,substring,strlen(substring)) == 0) {
		//find the radius
		char *diameter = s + strlen(substring);
		char *space = strchr(diameter,' ');
		if (space == NULL) return s;
		*space = '\0'; //mark end of diameter string
		bool valid;
		double r = string_to_bp(diameter,&valid) / 2;
		if (!valid) return s;

		//find the centre
		substring = "shifted ";
		if (strncmp(space+1,substring,strlen(substring)) != 0) return s;
		char *centre = space + strlen(substring) + 1; //start of centre point coords
		end = string_to_point(centre,&x,&y);
		if (end == centre) //invalid point
			return s;

		append_point(x,y,false); //centre
		append_point(x+r,y,false); //a point on the circumference
		cur_path->n = -1; //signifies a circle
		return end;
	} else {
		/*Examples of valid paths:
		curved, closed path in cm: "(1cm,2cm)..(0,1cm)..(-1.1cm,0.5cm)..cycle"
		straight path in custom units: "(-0.36u,0.85u)--(-0.92u,-1.43u)--(1.29u,0.52u)"
		pacman: "(30,-33)..(-21,-7)..(32,10)--(11,-13)--cycle"
		*/
		while (true) {
			end = string_to_point(s,&x,&y);
			if (end == s) //invalid point
				return s;
			append_point(x,y, *end == '-');

			if (strlen(end) < 5) //not enough chars for another point
				break;
			s = end + 2; //go to next point
			if (strcmp(s,"cycle") == 0) {
				cur_path->cycle = true;
				end = s+5;
				break;
			}
		}
	}
	return end;
}

//find the bezier control points for all the points on the path
void find_control_points() {
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

int append_point(double x, double y, bool is_straight) {
	//allocate more space for points if necessary
	if (cur_path->n == cur_path->size) {
		cur_path->size *= 2;
		struct point *tmp;
		tmp = realloc(cur_path->points,cur_path->size * sizeof *cur_path->points);
		if (tmp == NULL) {
			cur_path->size /= 2;
			return 1;
		}
		cur_path->points = tmp;
	}
	cur_path->n++;
	set_last_point(x,y,is_straight);
	return 0;
}
void remove_point(int i) {
	if (i >= 0 && i < cur_path->n) {
		for (; i<cur_path->n-1; i++) {
			set_point(i, cur_path->points[i+1].x, cur_path->points[i+1].y, cur_path->points[i+1].straight);
		}
		cur_path->n--;
	}
}
void insert_point(int i, double x, double y, bool is_straight) {
	if (i >= 0 && i <= cur_path->n) {
		struct point last_point = cur_path->points[cur_path->n-1];
		append_point(last_point.x, last_point.y, last_point.straight);
		int j;
		for (j=cur_path->n-2; j>=i; j--) {
			set_point(j+1, cur_path->points[j].x, cur_path->points[j].y, cur_path->points[j].straight);
		}
		set_point(i,x,y,is_straight);
	}
}

//return the metapost string describing the path, eg (0,0)..(5,23)..(16,-27)
char *path_to_string() {
	size_t size = 1000; //initial size of string. enough to hold "fullcircle ..."
	char *s = malloc(size * sizeof *s);
	if (s == NULL) return NULL;
	strcpy(s,"");
	if (cur_path->n == -1) { //circle
		double r,delta_x,delta_y;
		delta_x = cur_path->points[0].x - cur_path->points[1].x;
		delta_y = cur_path->points[0].y - cur_path->points[1].y;
		r = sqrt(delta_x*delta_x + delta_y*delta_y);
		snprintf(s, size, "fullcircle scaled %.*f%s shifted (%.*f%s,%.*f%s)",
			coord_precision, 2*r/unit,
			unit_name,
			coord_precision, cur_path->points[0].x/unit,
			unit_name,
			coord_precision, cur_path->points[0].y/unit,
			unit_name
		);
	} else if (cur_path->n == 1) { //point
		snprintf(s,size,"(%.*f%s,%.*f%s)",
			coord_precision,cur_path->points[0].x/unit,
			unit_name,
			coord_precision,cur_path->points[0].y/unit,
			unit_name
		);
	} else if (cur_path->n > 1) { //path
		char point[30 + 2*strlen(unit_name)];
		int i;
		for (i=0;i<cur_path->n;i++) {
			snprintf(point,sizeof(point),"(%.*f%s,%.*f%s)",
				coord_precision, cur_path->points[i].x/unit,
				unit_name,
				coord_precision, cur_path->points[i].y/unit,
				unit_name
			); //should i print a warning here if there isn't enough space to fit the string? but that will never happen and wouldn't be a big problem if it did

			if (strlen(s) + sizeof(point) + 20 > size) { //maybe not enough space to fit, so grow s
				size *= 2;
				s = realloc(s,size * sizeof *s);
				if (s == NULL) return NULL;
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

//Find X(t) or Y(t) given coords of two points on curve and control points between them
double bezier (double start, double start_right, double end_left, double end, double t) {
	return (1-t)*(1-t)*(1-t)*start + 3*t*(1-t)*(1-t)*start_right + 3*t*t*(1-t)*end_left + t*t*t*end;
}

void point_before(int i) {
	find_control_points();
	if (i==0) {//before first point
		struct point p = cur_path->points[0];
		insert_point(i,
			2*p.x - p.right_x,
			2*p.y - p.right_y,
			p.straight);
	} else if (i==cur_path->n) {//after last point
		struct point p = cur_path->points[cur_path->n-1];
		append_point(
			2*p.x - p.left_x,
			2*p.y - p.left_y,
			p.straight);
	} else {
		//choose point halfway along path between the two points
		struct point p = cur_path->points[i-1];
		struct point q = cur_path->points[i];
		insert_point(i,
			bezier(p.x,p.right_x,q.left_x,q.x,0.5),
			bezier(p.y,p.right_y,q.left_y,q.y,0.5),
			p.straight);
	}
}
