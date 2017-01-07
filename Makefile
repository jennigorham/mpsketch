mpsketch: mpsketch.c paths.c paths.h mptoraster.c mptoraster.h
	gcc -Wall mpsketch.c paths.c mptoraster.c -L/usr/X11R6/lib -lX11 -L mplib -l mplib -l kpathsea -l mputil -lcairo -lpixman -lpng -lz -lmpfr -lgmp -lm -o mpsketch

gtk-test: gtk-test.c
	gcc -Wall `pkg-config --cflags gtk+-3.0` gtk-test.c `pkg-config --libs gtk+-3.0` -o gtk-test

clean: 
	rm -f mpsketch test.[0-9] test.log test.mpx mpxerr.tex
