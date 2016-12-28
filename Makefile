mpsketch: mpsketch.c paths.c paths.h
	gcc -Wall mpsketch.c paths.c -L/usr/X11R6/lib -lX11 -L ../mplib -l mplib -l kpathsea -l mputil -lcairo -lpixman -lpng -lz -lmpfr -lgmp -lm -o mpsketch

clean: 
	rm -f mpsketch test.[0-9] test.log test.mpx mpxerr.tex
