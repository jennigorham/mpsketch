#include "mptoraster.h"

//run metapost on the file, generating a ps or pdf output
int run_mpost() {
	char cmd[strlen(job_name) + strlen("mpost --interaction=nonstopmode .mp") + 1];
	if (USE_MPTOPDF)
		sprintf(cmd,"mptopdf %s.mp",job_name);
	else
		sprintf(cmd,"mpost --halt-on-error %s.mp",job_name);
		//sprintf(cmd,"mpost --interaction=nonstopmode %s.mp",job_name);
	printf("\nRunning \"%s\"...\n",cmd);
	return system(cmd);
}

int get_coords() {
	//Get the metapost coordinates of the lower left corner of the image
	//There are three ways I can think of doing this.
	//Method 1: use "show ..." in the metapost to write the coords to stdout then read them in with popen
	//Method 2: similar, but read them from the log file afterwards. (This doesn't work with mptopdf - it doesn't seem to write show commands to the log.)
	//Method 3: use "write ..." in the metapost to write the coords to a separate file. The advantage of this is that the file will only contain those coords so we don't have to search through it like with the log file. We'd have to write a separate file for each fig_num, or write the fig_nums to the file as well

	char buffer[100];
	bool found_coords = false;

	//Method 1: get coords from stdout
	/*FILE *mp_out = popen("mptopdf test.mp | grep '>>'", "r");
	fgets(buffer, sizeof(buffer), mp_out);
	puts(buffer);
	pclose(mp_out);

	int i = 4; 
	while (buffer[i] != ',') i++;
	buffer[i] = '\0';
	ll_x = strtof(buffer+4,NULL);
	int j = i+1;
	while (buffer[j] != ')') j++;
	buffer[j] = '\0';
	ll_y = strtof(buffer+i+1,NULL);*/

	//Method 2: get coords from the log file
	//Doesn't check that metapost is actually outputting sensible coordinates, just searches for '>> "Figure 1 x-coordinate:' and takes whatever comes after (up to another double-quote) as the x-coord, then whatever's on the next line in the right position as the y-coord.
	FILE *log;
	char log_filename[strlen(job_name)+5];
	sprintf(log_filename,"%s.log",job_name);
	log = fopen(log_filename,"r");
	if (log == NULL) {
		fprintf(stderr,"ERROR: couldn't open '%s.log'.\n",job_name);
		return 1;
	} else {
		char substring[50];
		sprintf(substring,">> \"Figure %d x-coordinate:",fig_num);

		while (fgets(buffer,sizeof(buffer),log) != NULL) {
			buffer[strlen(substring)] = '\0';
			if (strcmp(buffer,substring) == 0) {
				int i = strlen(substring) + 1;
				while (buffer[i] != '"' && i < sizeof(buffer)-1) i++;
				buffer[i] = '\0';
				ll_x = strtof(buffer+strlen(substring)+1,NULL);

				fgets(buffer,sizeof(buffer),log); //y-coordinate will be on next line
				i = strlen(substring) + 1;
				while (buffer[i] != '"') i++;
				buffer[i] = '\0';
				ll_y = strtof(buffer+strlen(substring)+1,NULL);
				if (!USE_MPTOPDF) ll_y -= 0.8; //The vertical coordinate seems to be off by about 0.8pt when using mpost. I don't know why. 
				found_coords = true;
				break;
			}
		}
		fclose(log);

		if (!found_coords) {
			fprintf(stderr,"ERROR: coordinates of lower left corner of diagram not found in '%s.log'.\n",job_name);
			puts("Ensure that the figure number is correct,");
			printf("and that %s.mp contains the following lines at the start:\n\n",job_name);
			puts("prologues:=3;");
			puts("def save_coords =");
			puts("	show \"Figure \" & decimal(charcode) & \" x-coordinate: \" & decimal((xpart llcorner bbox currentpicture) + bboxmargin);");
			puts("	show \"Figure \" & decimal(charcode) & \" y-coordinate: \" & decimal((ypart llcorner bbox currentpicture) + bboxmargin);");
			puts("enddef;");
			puts("extra_endfig := extra_endfig & \"save_coords;\";");
			return 1;
		} else return 0;
	}

	//Method 3: get coords from "coords.txt"
	//Metapost must run 'write decimal ((xpart llcorner bbox currentpicture) + bboxmargin) to "coords.txt";' after the figure.
	/*FILE *coords;
	coords = fopen("coords.txt","r");
	if (coords == NULL) {
		puts("coords.txt not found.");
		puts("Please put the following two lines after endfig:\n");
		puts("write decimal ((xpart llcorner bbox currentpicture) + bboxmargin) to \"coords.txt\";\nwrite decimal ((ypart llcorner bbox currentpicture) + bboxmargin) to \"coords.txt\";");
		//copy_to_clipboard("write decimal ((xpart llcorner bbox currentpicture) + bboxmargin) to \"coords.txt\";\n write decimal ((ypart llcorner bbox currentpicture) + bboxmargin) to \"coords.txt\";");
		error();
	} else {
		fgets(buffer,sizeof(buffer),coords);
		ll_x = strtof(buffer,NULL);
		fgets(buffer,sizeof(buffer),coords);
		ll_y = strtof(buffer,NULL);
		if (!USE_MPTOPDF) ll_y -= 0.8; //The vertical coordinate seems to be off by about 0.8pt when using mpost. I don't know why. 
		fclose(coords);
		system("rm coords.txt");
	}*/
}

int make_bitmap(char *filename) {
	//TODO: if ps/pdf file not found, show message about leaving outputtemplate as default
	char cmd[
		strlen("convert -density  -.pdf ") +
		(int) floor(log10(density)) + 1 + //chars in density
		strlen(job_name) +
		(int) floor(log10(fig_num)) + 1 + //chars in fig_num
		strlen(filename) + 1
	];
	if (USE_MPTOPDF)
		sprintf(cmd,"convert -density %d %s-%d.pdf %s",density,job_name,fig_num,filename);
	else
		sprintf(cmd,"convert -density %d %s.%d %s",density,job_name,fig_num,filename);
	printf("\nRunning \"%s\"...\n\n",cmd);
	int ret = system(cmd);
	if (ret != 0) {
		printf("%d\n",ret);
		fprintf(stderr,"ERROR: couldn't create bitmap.");
		puts("Ensure that imagemagick is installed.");
		//now that mpsketch-coords.mp overrides prologues and outputtemplate, we don't need the following:
		/*puts("This could be due to one of the following:");
		puts("You're using a non-default outputtemplate."); //ret = 256
		puts("Imagemagick is not installed on your system."); //ret = 32512
		puts("You haven't set \"prologues:=3;\" in your metapost file."); //ret = 256 if prologues=0, hangs if prologues=1 or 2*/
		/*You can see more info on why it hangs when prologues is 1 or 2 by running "gs [psfile]". It says:
		"Can't find (or can't open) font file /usr/share/ghostscript/9.10/Resource/Font/CMR10.
		Can't find (or can't open) font file CMR10.
		Querying operating system for font files..."
		This seems to be a problem with ghostscript 9.10: https://bugs.ghostscript.com/show_bug.cgi?id=695787
		Can be fixed by running system("GS_OPTIONS=-dNONATIVEFONTMAP convert ... but it doesn't matter now because I've put prologues:=3 in the save_coords macro in mpsketch-coords.mp so if the user sets prologues:=1 it'll be overridden
		*/
	}
	return ret;
}

int get_bitmap(char *filename, Display *d, Window w, Pixmap *bitmap, unsigned int *bitmap_width, unsigned int *bitmap_height) {
	int hotspot_x, hotspot_y;
	int ret = XReadBitmapFile(d, w,
							 filename,
							 bitmap_width, bitmap_height,
							 bitmap,
							 &hotspot_x, &hotspot_y);
	if (ret != BitmapSuccess) {
		switch (ret) {
			case BitmapOpenFailed:
				fprintf(stderr, "XReadBitmapFile - could not open file '%s'.\n",filename);
				break;
			case BitmapFileInvalid:
				fprintf(stderr, "XReadBitmapFile - file '%s' doesn't contain a valid bitmap.\n", filename);
				break;
			case BitmapNoMemory:
				fprintf(stderr, "XReadBitmapFile - not enough memory.\n");
				break;
		}
	}
	return ret;
}
