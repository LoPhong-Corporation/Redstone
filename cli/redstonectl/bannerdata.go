package main

// Redstone dust sprite (15x12), generated from resources/redstone_sprite.png (quantised to 16 colours).
//
// bannerGrid: one letter per pixel, an index into bannerPalette ('.' = transparent).
// The banner paints every pixel as TWO SPACES with a BACKGROUND colour (solid pixel-art
// blocks, not coloured text). bannerChars is the plain-ASCII fallback used when colour is off.

var bannerPalette = [][3]uint8{
	{92, 5, 1},
	{115, 0, 1},
	{172, 16, 4},
	{143, 55, 49},
	{241, 1, 1},
	{64, 5, 1},
	{46, 0, 2},
	{92, 16, 12},
	{110, 1, 0},
	{43, 0, 1},
	{174, 13, 2},
	{61, 5, 1},
	{52, 6, 7},
	{45, 1, 2},
}

var bannerGrid = []string{
	"......aaa......",
	".....abcca.....",
	"...daceecfgh...",
	"..aacecibefjj..",
	".dakckciiealmn.",
	"aikccbeeebllljj",
	"abbecbfbibfclmg",
	"acbicekiblfaafn",
	"jffcecbffcfffmj",
	".nflcbflaffmjn.",
	"..dnnmfffmnnj..",
	".....nnnnn.....",
}

var bannerChars = []string{
	"            ++++++            ",
	"          ++++####++          ",
	"      @@++##%%%%##==--**      ",
	"    ==++##%%##++++%%==----    ",
	"  %%==######**++++%%++====--  ",
	"==++##**##++%%%%%%++======----",
	"++++++%%##++==++++++==##====--",
	"++##++++##%%**++++====++++==--",
	"--====##%%##++====##========--",
	"  --====##++====++======----  ",
	"    %%----==========------    ",
	"          ----------          ",
}
