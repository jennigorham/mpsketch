%input mpsketch-coords;
outputtemplate := "%j.%3c";
prologues:=2;

u:=1cm;
def tangent (expr curve,cut) = (point (xpart (curve intersectiontimes cut)) of curve){direction (xpart (curve intersectiontimes cut)) of curve} enddef;
def negtangent (expr curve,cut) = (point (xpart (curve intersectiontimes cut)) of curve){-direction (xpart (curve intersectiontimes cut)) of curve} enddef;

def join (expr curve, t) = (point t of curve){direction t of curve} enddef;
def negjoin (expr curve, t) = (point t of curve){-direction t of curve} enddef;

def mirror (expr curve) = ((curve xscaled -1) shifted (-97*2,0)) enddef;

beginfig(1)
	%vignette
	pickup pencircle scaled 1.5bp;
	path a[]; 
	picture leftside; leftside = image(
		a1=(-111,199)..(-102,199)..(-98,209)..(-110,221)..(-125,220)..(-148,205)..(-163,200)..(-175,208)..(-175,214)..(-165,220)..(-152,217)..(-139,207)..(-127,201)..(-125,201);
		filldraw negjoin(a1,0)..(-112,203)..(-111,205)..(-113,207)..(-114,205)..{direction 0 of a1}join(a1,0)--cycle;
		filldraw join(a1,infinity)..(-121,203)..(-121,208)..(-122,206)..(-121.5,204)..negjoin(a1,infinity)--cycle;
		draw a1;
		path cut[];
		cut1 = (-110,217)..(-108,225); cut2 = (-179,215)..(-171,212);
		a2 = (-172,200)..(-159,195)..(-143,200)--(-143,200)..(-140,195)..(-134,194)--(-134,194)..(-135,205)..(-131,212);
		filldraw ((a1 cutbefore cut1) cutafter cut2)--negtangent(a1,cut2)..a2..negtangent(a1,cut1)--cycle;

		a3 = negtangent(a2,(-156,190)..(-158,198))..(-178,200)..(-188,211)..(-200,219)..(-207,218)..(-211,214)..(-212,211)..(-210,210)..(-209,211)..(-209,212);
		cut1:=(-208,215)..(-213,217);
		filldraw (a3 cutbefore cut1)..negtangent(a3,cut1)--cycle;
		draw a3;
		cut1:=(-179,197)..(-175,203); cut2:=(-206,216)..(-210,221);
		filldraw ((a3 cutbefore cut1) cutafter cut2)--negtangent(a3,cut2)..(-197,221)..(-187,218)--(-187,218)..(-182,222)..(-177,221)--(-177,221)..(-183,214)..negtangent(a3,cut1)--cycle;
	);
	draw leftside; draw mirror(leftside);

	a4:=(-97,215)..(-107,224)..(-125,226)..(-145,217)..(-155,210)..(-162,209)..(-168,211)..(-170,212)--(-170,212)..(-161,210)..(-153,214)..(-142,223)..(-120,228)..(-110,226)--(-110,226)..(-109,229)..(-104,230)--(-104,230)..(-101,238)..(-97,242);
	a5:=(-97,230)..(-101,225)..(-97,218);
	filldraw a4--mirror(reverse a4)--mirror(reverse a5)--a5--cycle;

	a6:=(-97,200)..(-100,196)..(-106,193)..(-108,195)--(-108,195)..(-108,190)..(-104,188)..(-100,188)--(-100,188)..(-100,184)..(-97,180);
	filldraw a6--mirror(reverse a6)--cycle;

	a7:=(-100,211)..(-101,210)..(-100,209)--(-100,209)..(-100,208)..(-99,207);
	filldraw a7--mirror(reverse a7)--cycle;


	pickup pencircle scaled 0.7bp;
	path p;path q;

	%shell
	p := (22,-16)..(39,144)..(-91,65)..(-9,78)..(-37,101)..(-52,85)..(-35,71)..(-39,81)..(-40,78)..(-37,76); %spiral part
	path cut; cut=(-40,31)..(-39,47); %where on the spiral it joins up
	z0=(-20,10); 
	fill (p intersectionpoint cut)..z0..(point 0 of p)--(p cutafter cut)--cycle withcolor 0.9*white;
	draw (p intersectionpoint cut)..z0..(point 0 of p)--p;

	label.rt("shell",(122,148));
	drawarrow (122,148)..(84,148)..(59,136);


	label(btex $\int_0^1 x^2 dx$ etex,(-3,-138));
	p := (-63,-212)..(-8,-212)..(54,-153)..(32,-106)..(-31,-118)..(-32,-152)..(2,-166)..(24,-138)..(4,-128); draw p;
	cut := (32,-185)..(39,-191);
	cut := (24,-109)..(28,-99);
	draw thelabel.top("spiral",(0,0)) rotated angle (direction (xpart (p intersectiontimes cut)) of p) shifted (p intersectionpoint cut);


	dotlabel.bot("dot",(-217,110));
	label("label",(-298,42));
	drawarrow (-290,48)..(-262,85)..(-227,102);


	p := (-359,-40)..(-293,-31)..(-244,-70)..(-203,-148)..(-142,-183)..(-96,-148)..(-125,1)..(-187,-5)..(-185,-90)..(-151,-106)..(-136,-40)..(-171,-47)..(-159,-75)..(-149,-59)..(-156,-59)..(-155,-63); draw p;
	draw fullcircle scaled 51 shifted (-256,-20);
	draw fullcircle scaled 29 shifted (-339,-14);
	draw fullcircle scaled 17 shifted (-198,-137);
	draw fullcircle scaled 23 shifted (-107,-113);
	draw fullcircle scaled 10 shifted (-169,-36);
	draw fullcircle scaled 12 shifted (-179,-81);
	dotlabel.urt("point", ((-118,-11)..(-109,-2)) intersectionpoint p);
	draw tangent(p,(-118,-11)..(-109,-2))..(-182,46)..(-197,101)..(-169,128)..(-148,110)..(-156,100)..(-167,103)..(-162,110);

	drawarrow (-320,-14)..(-309,-13)..(-298,-14);
	drawarrow (-234,-44)..(-222,-57)..(-214,-78);
	drawarrow (-191,-148)..(-184,-158)..(-163,-168);
	drawarrow (-104,-98)..(-102,-91);
	drawarrow (-174,-42)..(-181,-50)..(-183,-60);
	drawarrow (-176,-90)..(-176,-93)..(-168,-96);

	%climber
	draw (-139,-49)--(-138,-59);
	draw (-142,-76)--(-138,-59)--(-143,-53)--(-149,-58);
	draw (-148,-37)--(-145,-48)--(-138,-54)--(-133,-47)--(-135,-40);
	draw fullcircle scaled 7 shifted (-140,-46);
endfig;

beginfig(5)
	label("test",(0,0));
endfig;
end
