// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2026 Cry of Fear: Enhanced contributors
// Shared, allocation-free text style math for the engine and FreeVGUI.
#ifndef COF_TEXT_STYLE_H
#define COF_TEXT_STYLE_H
static void CoF_TextStyleColor( int r, int g, int b, int opacity, int rgbFade, int out[4] )
{
	int level = r > g ? r : g;
	if( b > level ) level = b;
	if( level < 0 ) level = 0;
	if( level > 255 ) level = 255;
	if( opacity < 0 ) opacity = 0;
	if( opacity > 255 ) opacity = 255;
	out[0] = 191; out[1] = 193; out[2] = 194;
	out[3] = rgbFade ? opacity * level / 255 : opacity;
}
static void CoF_TextShadowTap( int tall, int tap, float *x, float *y, int *weight )
{
	float offset = tall * 0.055f, radius = tall * 0.035f;
	if( offset < 1.0f ) offset = 1.0f;
	if( radius < 1.0f ) radius = 1.0f;
	*x = offset; *y = offset; *weight = tap == 0 ? 56 : 18;
	if( tap == 1 ) *x -= radius;
	if( tap == 2 ) *x += radius;
	if( tap == 3 ) *y -= radius;
	if( tap == 4 ) *y += radius;
}
#endif
