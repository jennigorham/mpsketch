%metapost code necessary to make mpsketch work
%can be installed into whatever directory metapost looks for macro files, eg /usr/share/texlive/texmf-dist/metapost/base/ on my system

%include fonts in the postscript output
prologues:=3;

%output the coordinates of the lower left corner of the image
def save_coords =
	show "Figure " & decimal(charcode) & " x-coordinate: " & decimal((xpart llcorner bbox currentpicture) + bboxmargin);
	show "Figure " & decimal(charcode) & " y-coordinate: " & decimal((ypart llcorner bbox currentpicture) + bboxmargin);
enddef;
extra_endfig := extra_endfig & "save_coords;";
