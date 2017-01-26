#include "mptoraster.h"

//run metapost on the file, generating a ps or pdf output
int run_mpost(char *job_name) {
	char cmd[strlen(job_name) + strlen("mpost --interaction=nonstopmode .mp") + 1];
	if (USE_MPTOPDF)
		sprintf(cmd,"mptopdf %s.mp",job_name);
	else
		sprintf(cmd,"mpost --halt-on-error %s.mp",job_name);
		//sprintf(cmd,"mpost --interaction=nonstopmode %s.mp",job_name);
	printf("\nRunning \"%s\"...\n",cmd);
	return system(cmd);
}

int create_mp_file(char *job_name_in, char *job_name_out) {
	char filename_in[strlen(job_name_in)+4];
	char filename_out[strlen(job_name_out)+4];
	sprintf(filename_in,"%s.mp",job_name_in);
	sprintf(filename_out,"%s.mp",job_name_out);

	FILE *file_in = fopen(filename_in,"r");
	if (file_in == NULL) return 1;
	FILE *file_out = fopen(filename_out,"w");
	if (file_out == NULL) return 2;

	//tell metapost to show the coords in the log
	fputs("def save_coords = ",file_out);
	fputs("show \"Figure \" & decimal(charcode) & \" coordinates: (\" & decimal((xpart llcorner bbox currentpicture) + bboxmargin) & \",\" & decimal((ypart llcorner bbox currentpicture) + bboxmargin) & \")\";",file_out);
	//override prologues and outputtemplate
	fputs("prologues:=3;",file_out);
	fputs("outputtemplate:=\"%j.%c\";",file_out);
	fputs("enddef;",file_out);
	//code will be run after every figure
	fputs("extra_endfig := extra_endfig & \"save_coords;\";\n\n",file_out);

	//tack on the original mp file
	char ch;
	while( ( ch = fgetc(file_in) ) != EOF ) 
		fputc(ch,file_out);
	
	fclose(file_out);
	fclose(file_in);
	return 0;
}

int get_coords(char *job_name) {
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
	//line will be something like '>> "Figure 1 coordinates: (-359.35,-215.52448)"'
	FILE *log;
	char log_filename[strlen(job_name)+5];
	sprintf(log_filename,"%s.log",job_name);
	log = fopen(log_filename,"r");
	if (log == NULL) {
		fprintf(stderr,"ERROR: couldn't open '%s.log'.\n",job_name);
		return 1;
	} else {
		char *substring = ">> \"Figure ";
		n_fig = 0;
		while (fgets(buffer,sizeof(buffer),log) != NULL) {
			if (strncmp(buffer,substring,strlen(substring)) == 0) {
				//make a record that this figure number is available
				char *num = buffer + strlen(substring);
				char *space;
				figures[n_fig] = strtod(num,&space);
				n_fig++;

				if (figures[n_fig-1] == fig_num) {
					//get x-coord
					char *x = space + strlen("coordinates: (") + 1;
					char *comma = strchr(x,',');
					if (comma == NULL) continue;
					*comma = '\0';

					//get y-coord
					char *y = comma + 1;
					char *bracket = strchr(y,')');
					if (bracket == NULL) continue;
					*bracket = '\0';

					ll_x = strtof(x,NULL);
					ll_y = strtof(y,NULL);

					if (!USE_MPTOPDF) ll_y -= 0.8; //The vertical coordinate seems to be off by about 0.8pt when using mpost. I don't know why. 

					found_coords = true;
				}
			}
		}
		fclose(log);

		if (!found_coords) {
			fprintf(stderr,"ERROR: figure %d coordinates not found in '%s.log'.\n",fig_num,job_name);
			puts("Ensure that the figure number is correct.");
			return 2;
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

int make_bitmap(char *job_name) {
	char cmd[
		strlen("convert -density  -.pdf .xbm") +
		(int) floor(log10(density)) + 1 + //chars in density
		strlen(job_name)*2 +
		(int) floor(log10(fig_num)) + 1 + //chars in fig_num
		1
	];
	if (USE_MPTOPDF)
		sprintf(cmd,"convert -density %d %s-%d.pdf %s.xbm",density,job_name,fig_num,job_name);
	else
		sprintf(cmd,"convert -density %d %s.%d %s.xbm",density,job_name,fig_num,job_name);
	printf("\nRunning \"%s\"...\n\n",cmd);
	int ret = system(cmd);
	if (ret != 0) {
		//printf("%d\n",ret);
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
		Can be fixed by running system("GS_OPTIONS=-dNONATIVEFONTMAP convert ... but it doesn't matter now because I've put prologues:=3 in the save_coords macro so if the user sets prologues:=1 it'll be overridden
		*/
	}
	return ret;
}

int make_png(char *job_name) {
	char cmd[
		strlen("convert -density  -.pdf .png") +
		(int) floor(log10(density)) + 1 + //chars in density
		strlen(job_name)*2 +
		(int) floor(log10(fig_num)) + 1 + //chars in fig_num
		1
	];
	if (USE_MPTOPDF)
		sprintf(cmd,"convert -density %d %s-%d.pdf %s.png",density,job_name,fig_num,job_name);
	else
		sprintf(cmd,"convert -density %d %s.%d %s.png",density,job_name,fig_num,job_name);
	printf("\nRunning \"%s\"...\n\n",cmd);
	int ret = system(cmd);
	if (ret != 0) {
		//printf("%d\n",ret);
		fprintf(stderr,"ERROR: couldn't create png file.");
		puts("Ensure that imagemagick is installed.");
	}
	return ret;
}
