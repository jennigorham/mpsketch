#include "paths.h"

void init_path(struct path *p) {
	p->cycle = false;
	p->size = 10;
	p->points = malloc(p->size * sizeof *p->points);
	p->aug_mat = malloc(p->size * (p->size + 1) * sizeof(double));
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

/**************************************************************************************
I've implemented the algorithm on p131 of metafontbook (http://www.ctex.org/documents/shredder/src/mfbook.pdf)
so that there's no need to compile mplib to get this working.
For each curved section of the path, it finds the distances between the points (l) and the turning angles (psi),
as described in metafontbook (although I number them from 0 instead of 1), then taking equation (**) and setting
alpha=beta=1 (the default tension), and replacing phi with -psi-theta, the equation for the interior points becomes
l_{k+1} theta_k + (2 l_{k+1} + 2 l_{k}) theta_{k+1} + l_{k} theta_{k+2} = -l_{k} psi_{k+1} - 2 l_{k+1} psi_{k}
The coefficients are guaranteed to be non-zero as long as l>0 (which it is because coincident points are not
included in a curved section.)

If it's not cyclic then we also have to consider the end points.
Equation (***) with gamma=1 (the default curl) becomes
theta_0 + theta_1 = -psi_0
And equation (***') with psi for the final point arbitrarily set to zero (because it allows us to work in terms of
theta instead of phi at the final point), and with points labeled from 0 to n-1 instead of 0 to n, becomes
theta_{n-2} + theta_{n-1} = 0

Making an augmented matrix from these equations gives something that looks like:
*	*	*	0	0	|	*
0	*	*	*	0	|	*
0	0	*	*	*	|	*
*	0	0	*	*	|	*
*	*	0	0	*	|	*
for a cyclic path of 5 points, for example, or like:
*	*	*	0	0	|	*
0	*	*	*	0	|	*
0	0	*	*	*	|	*
0	0	0	1	1	|	0
1	1	0	0	0	|	*
for a non-cyclic path (the asterisks stand for real numbers). These matrices are almost in upper triangular form,
so all that's needed is to get rid of the numbers in the bottom left using row operations, then put into reduced
row echelon form to find theta at each point.
(The matrix has n rows and (n+1) columns and is addressed as aug_mat[row_num*(n+1) + column_num].)

Then we can use the equations at the bottom of p131 to solve for u and v, the control points.
**************************************************************************************/
void get_row_k(double *l, double *psi, int n, int k) {
	//equations are of the form l_{k+1} theta_k + (2 l_{k+1} + 2 l_{k}) theta_{k+1} + l_{k} theta_{k+2} = -l_{k} psi_{k+1} - 2 l_{k+1} psi_{k}
	cur_path->aug_mat[k*(n+1) + k] = l[(k+1)%n]; //row k column k of matrix: coefficient of theta_k
	cur_path->aug_mat[k*(n+1) + (k+1)%n] = 2*l[(k+1)%n] + 2*l[k]; //row k column k+1 of matrix: coefficient of theta_{k+1}
	cur_path->aug_mat[k*(n+1) + (k+2)%n] = l[k]; //row k column k+2 of matrix: coefficient of theta_{k+2}
	for (int j=3; j<n; j++)
		cur_path->aug_mat[k*(n+1) + (k+j)%n] = 0; //all other columns in this row should be 0
	cur_path->aug_mat[k*(n+1) + n] = -l[k]*psi[(k+1)%n] - 2*l[(k+1)%n]*psi[k]; //row k, last column of matrix: rhs of equation
}
void print_aug_matrix(int n) { //for troubleshooting
	printf("\n");
	for (int k=0; k<n; k++) {
		for (int j=0; j<n; j++) {
			printf("%f\t",cur_path->aug_mat[k*(n+1) + j]);
		}
		printf("| %f\n",cur_path->aug_mat[k*(n+1) + n]);
	}
	printf("\n");
}
double *get_aug_matrix(int i, int n, bool is_cycle) {
	double *l = malloc(n * sizeof(double)); //distances between adjacent points
	double *psi = malloc(n * sizeof(double)); //turning angles at interior points
	int k;
	for (k=0; k<n; k++) {
		double x1,y1,x2,y2,x3,y3;
		if (k<n-1 || is_cycle) {
			x1 = cur_path->points[(i+k)%cur_path->n].x;
			y1 = cur_path->points[(i+k)%cur_path->n].y;
			x2 = cur_path->points[(i+k+1)%cur_path->n].x;
			y2 = cur_path->points[(i+k+1)%cur_path->n].y;
			l[k] = sqrt((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1));
		}

		if (k<n-2 || is_cycle) {
			x3 = cur_path->points[(i+k+2)%cur_path->n].x;
			y3 = cur_path->points[(i+k+2)%cur_path->n].y;
			psi[k] = atan2((x2-x1)*(y3-y2) - (x3-x2)*(y2-y1), (x2-x1)*(x3-x2) + (y2-y1)*(y3-y2));
		}
	}
	if (!is_cycle) //arbitrarily set psi=0 at last point
		psi[n-2] = 0;

	/*printf("l: ");
	for (k=0; k<n-1; k++) printf("%f ",l[k]);
	printf("\n");

	printf("psi: ");
	for (k=0; k<n-2; k++) printf("%f ",psi[k]);
	printf("\n");*/

	for (k=0; k<n && (k<n-2 || is_cycle); k++) {
		get_row_k(l,psi,n,k);
	}
	if (!is_cycle) {
		//theta_{n-2} + theta_{n-1} = 0
		cur_path->aug_mat[(n-2)*(n+1) + n-2] = 1; //row n-2, column n-2: coefficient of theta_{n-2}
		cur_path->aug_mat[(n-2)*(n+1) + n-1] = 1; //row n-2, column n-1: coefficient of theta_{n-1}
		cur_path->aug_mat[(n-2)*(n+1) + n] = 0; //row n-2, last column: rhs
		for (k=0; k<n-2; k++)
			cur_path->aug_mat[(n-2)*(n+1) + k] = 0; //all other coefficients are 0

		//theta_0 + theta_1 = -psi_0
		cur_path->aug_mat[(n-1)*(n+1) + 0] = 1; //row n-1, column 0: coefficient of theta_0
		cur_path->aug_mat[(n-1)*(n+1) + 1] = 1; //row n-1, column 1: coefficient of theta_1
		cur_path->aug_mat[(n-1)*(n+1) + n] = -psi[0]; //row n-1, last column: rhs
		for (k=2; k<n; k++)
			cur_path->aug_mat[(n-1)*(n+1) + k] = 0; //all other coefficients are 0
	}

	//print_aug_matrix(n);

	free(l); //don't need this any more
	return(psi); //still need this to find phi once theta is solved for.
}

void rref(int n) { //get the augmented matrix into row reduced echelon form
	//make leading coefficients 1
	double *mat = cur_path->aug_mat;
	for (int k=0; k<n-2; k++) {
		double a = mat[k*(n+1) + k];
		mat[k*(n+1) + k] = 1;
		mat[k*(n+1) + k+1] /= a;
		mat[k*(n+1) + k+2] /= a;
		mat[k*(n+1) + n] /= a;
	}
	//print_aug_matrix(n);

	/* Matrix now looks like:
	1	*	*	0	0	|	*
	0	1	*	*	0	|	*
	0	0	1	*	*	|	*
	*	0	0	*	*	|	*
	*	*	0	0	*	|	*
	*/

	if (mat[(n-2)*(n+1) + 0] != 0) { //this only happens if we've got a cycle without any straight sections or coincident points
		//use kth row to get rid of leading term in penultimate row
		for (int k=0; k<n-2; k++) {
			double a = mat[(n-2)*(n+1) + k]; //penultimate row, kth column
			if (a != 0) {
				mat[(n-2)*(n+1) + k] = 0;
				mat[(n-2)*(n+1) + k+1] -= a*mat[k*(n+1) + k+1];
				mat[(n-2)*(n+1) + k+2] -= a*mat[k*(n+1) + k+2];
				mat[(n-2)*(n+1) + n]   -= a*mat[k*(n+1) + n];
			}
		}
		//make leading coefficient 1
		double a = mat[(n-2)*(n+1) + n-2];
		mat[(n-2)*(n+1) + n-2] = 1;
		mat[(n-2)*(n+1) + n-1] /= a;
		mat[(n-2)*(n+1) + n] /= a;
	}
	//print_aug_matrix(n);

	/* Matrix now looks like:
	1	*	*	0	0	|	*
	0	1	*	*	0	|	*
	0	0	1	*	*	|	*
	0	0	0	1	*	|	*
	*	*	0	0	*	|	*
	*/

	//use kth row to get rid of leading term in last row
	for (int k=0; k<n-1; k++) {
		double a = mat[(n-1)*(n+1) + k]; //last row, kth column
		if (a != 0) {
			mat[(n-1)*(n+1) + k] = 0;
			mat[(n-1)*(n+1) + k+1] -= a*mat[k*(n+1) + k+1];
			if (k+2 < n)
				mat[(n-1)*(n+1) + k+2] -= a*mat[k*(n+1) + k+2];
			mat[(n-1)*(n+1) + n]   -= a*mat[k*(n+1) + n];
			//print_aug_matrix(n);
		}
	}
	//make leading coefficient 1
	mat[(n-1)*(n+1) + n] /= mat[(n-1)*(n+1) + n-1];
	mat[(n-1)*(n+1) + n-1] = 1;

	/* Matrix now looks like:
	1	*	*	0	0	|	*
	0	1	*	*	0	|	*
	0	0	1	*	*	|	*
	0	0	0	1	*	|	*
	0	0	0	0	1	|	*
	*/

	for (int k=n-1; k>0; k--) {
		//use row k to make a zero in row k-1 above
		mat[(k-1)*(n+1) + n] -= mat[(k-1)*(n+1) + k]*mat[k*(n+1) + n];
		mat[(k-1)*(n+1) + k] = 0;
		//do the same with row k-2
		if (k>1) {
			mat[(k-2)*(n+1) + n] -= mat[(k-2)*(n+1) + k]*mat[k*(n+1) + n];
			mat[(k-2)*(n+1) + k] = 0;
		}
	}

	/* Matrix now in rref form:
	1	0	0	0	0	|	*
	0	1	0	0	0	|	*
	0	0	1	0	0	|	*
	0	0	0	1	0	|	*
	0	0	0	0	1	|	*
	*/

	//print_aug_matrix(n);
}

double john_hobby_f(double theta, double phi) { //ultimate formula on p131 of metafontbook
	return (2 + sqrt(2)*(sin(theta) - sin(phi)/16)*(sin(phi) - sin(theta)/16)*(cos(theta) - cos(phi)))/
		(3*(1 + (sqrt(5)-1)/2*cos(theta) + (3 - sqrt(5))/2*cos(phi)));
}

void get_u_v(int i, int k, int n, double *psi) {//penultimate formula. Find the control points
	//k is which point the curved section starts at, i is which segment in the curved section, n is number of points in curved section
	//e.g. in the path (0,0)--(1,1)--(2,2)..(3,3)..(4,4)..(5,5)--(6,6) k=2 and n=4; i=0 would refer to the section between (2,2) and (3,3)
	double x0,y0,x1,y1,theta,phi;
	theta = cur_path->aug_mat[i*(n+1) + n];
	phi = -psi[i]-cur_path->aug_mat[((i+1)%n)*(n+1) + n];
	x0 = cur_path->points[(i+k)%cur_path->n].x;
	y0 = cur_path->points[(i+k)%cur_path->n].y;
	x1 = cur_path->points[(i+k+1)%cur_path->n].x;
	y1 = cur_path->points[(i+k+1)%cur_path->n].y;
	cur_path->points[(i+k)%cur_path->n].right_x = (x0 + (cos(theta)*(x1-x0) - sin(theta)*(y1-y0))*john_hobby_f(theta,phi));
	cur_path->points[(i+k)%cur_path->n].right_y = (y0 + (cos(theta)*(y1-y0) + sin(theta)*(x1-x0))*john_hobby_f(theta,phi));
	cur_path->points[(i+k+1)%cur_path->n].left_x = (x1 - (cos(-phi)*(x1-x0) - sin(-phi)*(y1-y0))*john_hobby_f(phi,theta));
	cur_path->points[(i+k+1)%cur_path->n].left_y = (y1 - (cos(-phi)*(y1-y0) + sin(-phi)*(x1-x0))*john_hobby_f(phi,theta));
}
void set_curved_pair_control_points(int i) { //a curved section with only 2 points is really just a straight section. Set the control points accordingly.
	int n = cur_path->n;
	cur_path->points[i%n].right_x = cur_path->points[i%n].x;
	cur_path->points[i%n].right_y = cur_path->points[i%n].y;
	cur_path->points[(i+1)%n].left_x = cur_path->points[(i+1)%n].x;
	cur_path->points[(i+1)%n].left_y = cur_path->points[(i+1)%n].y;
}
//find the bezier control points for all the points on the path
void find_control_points() {
	int n = cur_path->n;
	int k=0;
	while (k<n) {
		if (cur_path->points[k].straight) {
			k++;
		} else {
			//find the end of the curved section
			int j; //number of points in the section
			bool is_cycle = cur_path->cycle;
			for (j=0; j<n-k || (cur_path->cycle && j<n); j++) {
				bool coincident_points = false;
				if (cur_path->points[(k+j)%n].x == cur_path->points[(k+j+1)%n].x &&
					cur_path->points[(k+j)%n].y == cur_path->points[(k+j+1)%n].y)
				{
					coincident_points = true;
					set_curved_pair_control_points(k+j);
				}
				if (cur_path->points[(k+j)%n].straight || coincident_points) {
					is_cycle = false; //the whole path may be a cycle, but this curved section is not
					j++;
					break;
				}
			}
			//printf("%d %d\n",k,j);

			if (j>2) {
				double *psi;
				psi = get_aug_matrix(k,j,is_cycle);
				rref(j);
				for (int i=0; i<j; i++) {
					if (i<j-1 || is_cycle)
						get_u_v(i,k,j,psi);
				}
				free(psi);
			} else if (is_cycle) { //a cyclic curved path with just 2 points
				double psi[2] = {M_PI,M_PI}; //turning angles are all 180 degrees
				cur_path->aug_mat[0*(n+1) + n] = M_PI/2; //theta = 90 degrees
				cur_path->aug_mat[1*(n+1) + n] = M_PI/2;
				get_u_v(0,0,2,psi);
				get_u_v(1,0,2,psi);
			} else { //curved section with just 2 points
				set_curved_pair_control_points(k);
			}
			k += j;
		}
	}
}

/*
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
}*/

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
		double *tmp_mat;
		tmp = realloc(cur_path->points,cur_path->size * sizeof *cur_path->points);
		tmp_mat = realloc(cur_path->aug_mat,cur_path->size * (cur_path->size + 1) * sizeof(double));
		if (tmp == NULL || tmp_mat == NULL) {
			free(tmp); free(tmp_mat); //in case one succeeds
			cur_path->size /= 2;
			return 1;
		}
		cur_path->points = tmp;
		cur_path->aug_mat = tmp_mat;
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
