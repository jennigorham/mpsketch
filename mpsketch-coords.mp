%metapost code necessary to make mpsketch work
%can be installed into whatever directory metapost looks for macro files, eg /usr/share/texlive/texmf-dist/metapost/base/ on my system


%output the coordinates of the lower left corner of the image
def save_coords =
	show "Figure " & decimal(charcode) & " x-coordinate: " & decimal((xpart llcorner bbox currentpicture) + bboxmargin);
	show "Figure " & decimal(charcode) & " y-coordinate: " & decimal((ypart llcorner bbox currentpicture) + bboxmargin);
	%run the following commands after every figure, then the user can't override them and stuff up mptoraster
	prologues:=3; %include fonts in the postscript output
	outputtemplate:="%j.%c"
enddef;
extra_endfig := extra_endfig & "save_coords;";
