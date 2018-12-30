LDFLAGS = 
#LDFLAGS = -L mplib
LDLIBS = -lm
#LDLIBS = -lmplibcore -lmplibbackends -lkpathsea -lmputil -lcairo -lpixman-1 -lpng -lz -lmpfr -lgmp -lm

mpsketch: mpsketch.c paths.c paths.h mptoraster.c mptoraster.h common.c common.h
	gcc -g -Wall mpsketch.c paths.c common.c mptoraster.c -L/usr/X11R6/lib -lX11 $(LDFLAGS) $(LDLIBS) -o mpsketch

gtk-test: gtk-test.c paths.c paths.h mptoraster.c mptoraster.h common.c common.h
	gcc -g -Wall `pkg-config --cflags gtk+-3.0` gtk-test.c paths.c common.c mptoraster.c `pkg-config --libs gtk+-3.0` $(LDFLAGS) $(LDLIBS) -o gtk-test

clean: 
	rm -f mpsketch test.[0-9] test.log test.mpx mpxerr.tex

mplib/mplib.h:
	#This requires downloading roughly 700MB of stuff, which then grows to 2GB once you build it, so make sure you have disk space
	#Building mplib requires a few libraries. On my system I had to:
	#sudo apt-get install libpoppler-dev libxmu-dev libxaw7-dev flex bison
	#If the "Build" script fails, it will tell you what libraries it's missing. Install them and try running it again.
	mkdir -p mplib
	svn checkout svn://tug.org/texlive/branches/branch2018/Build/source #~700MB
	cd source; ./Build &&
	(cp source/Work/libs/mpfr/libmpfr.a mplib; \
	cp source/Work/libs/pixman/libpixman.a mplib; \
	cp source/Work/libs/libpng/libpng.a mplib; \
	cp source/Work/libs/zlib/libz.a mplib; \
	cp source/Work/texk/web2c/libmp*.a mplib; \
	cp source/Work/texk/kpathsea/.libs/libkpathsea.a mplib; \
	cp source/Work/texk/web2c/mplib.h mplib)
	##It used to be at foundry.supelec.fr but this seems to be down now
	#svn checkout --username anonsvn --password anonsvn https://foundry.supelec.fr/svn/metapost/trunk
	#cd trunk; ./build.sh &&
	#(cp trunk/build/texk/web2c/mplib.h mplib; \
	#cp trunk/build/texk/web2c/libmp*.a mplib; \
	#cp trunk/build/libs/pixman/libpixman.a mplib; \
	#cp trunk/build/libs/libpng/libpng.a mplib; \
	#cp trunk/build/libs/zlib/libz.a mplib)
