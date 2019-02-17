/*
 * This file is part of the MicroPython K210 project, https://github.com/loboris/MicroPython_K210_LoBo
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 LoBo (https://github.com/loboris)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 *  TFT library
 *
 *  Author: LoBo, 01/2019
 *
 *  Library supporting SPI TFT displays based on ILI9341, ILI9488 & ST7789V controllers
*/

#include "mpconfigport.h"

#if MICROPY_USE_DISPLAY

#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "tft.h"
#include "time.h"
#include <math.h>
#include "tftspi.h"
#include "tjpgd.h"
#include "mphalport.h"

#include "py/mpprint.h"
#include "py/stream.h"
#include "extmod/vfs.h"

#define DEG_TO_RAD 0.01745329252
#define RAD_TO_DEG 57.295779513
#define deg_to_rad 0.01745329252 + 3.14159265359
#define swap(a, b) { int16_t t = a; a = b; b = t; }
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
#if !defined(max)
#define max(A,B) ( (A) > (B) ? (A):(B))
#endif
#if !defined(min)
#define min(A,B) ( (A) < (B) ? (A):(B))
#endif

// Embedded fonts
extern uint8_t tft_SmallFont[];
extern uint8_t tft_DefaultFont[];
extern uint8_t tft_Dejavu18[];
extern uint8_t tft_Dejavu24[];
extern uint8_t tft_Ubuntu16[];
extern uint8_t tft_Comic24[];
extern uint8_t tft_minya24[];
extern uint8_t tft_tooney32[];
extern uint8_t tft_def_small[];
extern uint8_t userFont1[];
extern uint8_t userFont2[];
extern uint8_t userFont3[];
extern uint8_t userFont4[];

// ==== Color definitions constants ==============
/*
const color_t TFT_BLACK       = {   0,   0,   0 };
const color_t TFT_NAVY        = {   0,   0, 128 };
const color_t TFT_DARKGREEN   = {   0, 128,   0 };
const color_t TFT_DARKCYAN    = {   0, 128, 128 };
const color_t TFT_MAROON      = { 128,   0,   0 };
const color_t TFT_PURPLE      = { 128,   0, 128 };
const color_t TFT_OLIVE       = { 128, 128,   0 };
const color_t TFT_LIGHTGREY   = { 192, 192, 192 };
const color_t TFT_DARKGREY    = { 128, 128, 128 };
const color_t TFT_BLUE        = {   0,   0, 255 };
const color_t TFT_GREEN       = {   0, 255,   0 };
const color_t TFT_CYAN        = {   0, 255, 255 };
const color_t TFT_RED         = { 252,   0,   0 };
const color_t TFT_MAGENTA     = { 252,   0, 255 };
const color_t TFT_YELLOW      = { 252, 252,   0 };
const color_t TFT_WHITE       = { 252, 252, 252 };
const color_t TFT_ORANGE      = { 252, 164,   0 };
const color_t TFT_GREENYELLOW = { 172, 252,  44 };
const color_t TFT_PINK        = { 252, 192, 202 };
*/

const color_t TFT_BLACK       = 0x0000;      /*   0,   0,   0 */
const color_t TFT_NAVY        = 0x000F;      /*   0,   0, 128 */
const color_t TFT_DARKGREEN   = 0x03E0;      /*   0, 128,   0 */
const color_t TFT_DARKCYAN    = 0x03EF;      /*   0, 128, 128 */
const color_t TFT_MAROON      = 0x7800;      /* 128,   0,   0 */
const color_t TFT_PURPLE      = 0x780F;      /* 128,   0, 128 */
const color_t TFT_OLIVE       = 0x7BE0;      /* 128, 128,   0 */
const color_t TFT_LIGHTGREY   = 0xC618;      /* 192, 192, 192 */
const color_t TFT_DARKGREY    = 0x7BEF;      /* 128, 128, 128 */
const color_t TFT_BLUE        = 0x001F;      /*   0,   0, 255 */
const color_t TFT_GREEN       = 0x07E0;      /*   0, 255,   0 */
const color_t TFT_CYAN        = 0x07FF;      /*   0, 255, 255 */
const color_t TFT_RED         = 0xF800;      /* 255,   0,   0 */
const color_t TFT_MAGENTA     = 0xF81F;      /* 255,   0, 255 */
const color_t TFT_YELLOW      = 0xFFE0;      /* 255, 255,   0 */
const color_t TFT_WHITE       = 0xFFFF;      /* 255, 255, 255 */
const color_t TFT_ORANGE      = 0xFD20;      /* 255, 165,   0 */
const color_t TFT_GREENYELLOW = 0xAFE5;      /* 173, 255,  47 */
const color_t TFT_PINK        = 0xF81F;
// ===============================================

// ==============================================================
// ==== Set default values of global variables ==================

uint8_t orientation = LANDSCAPE;// screen orientation
uint16_t font_rotate = 0;		// font rotation
uint8_t	font_transparent = 0;
uint8_t	font_forceFixed = 0;
uint8_t	text_wrap = 0;			// character wrapping to new line
color_t	_fg = cTFT_GREEN;
color_t _bg = cTFT_BLACK;
uint8_t image_debug = 0;

float _angleOffset = DEFAULT_ANGLE_OFFSET;

int	TFT_X = 0;
int	TFT_Y = 0;

uint32_t tp_calx = 0;
uint32_t tp_caly = 0;

dispWin_t dispWin = {
  .x1 = 0,
  .y1 = 0,
  .x2 = DEFAULT_TFT_DISPLAY_WIDTH,
  .y2 = DEFAULT_TFT_DISPLAY_HEIGHT,
};

Font cfont = {
	.font = tft_DefaultFont,
	.x_size = 0,
	.y_size = 0x0B,
	.offset = 0,
	.numchars = 95,
	.bitmap = 1,
};

uint8_t font_buffered_char = 1;
uint8_t font_line_space = 0;

// ------------------------
// ePaper display variables
// ------------------------
uint8_t *drawBuff = NULL;
uint8_t *gs_drawBuff = NULL;
uint8_t _gs = 0;
uint16_t gs_used_shades = 0;
uint8_t *drawBuff42 = NULL;
uint8_t epd_type = EPD_TYPE_2_9;

// ==============================================================


typedef struct {
      uint8_t charCode;
      int adjYOffset;
      int width;
      int height;
      int xOffset;
      int xDelta;
      uint16_t dataPtr;
} propFont;

static dispWin_t dispWinTemp;

static uint8_t *userfont = NULL;
static int TFT_OFFSET = 0;
static propFont	fontChar;
static float _arcAngleMax = DEFAULT_ARC_ANGLE_MAX;


// =========================================================================
// ** All drawings are clipped to 'dispWin' **
// ** All x,y coordinates in public functions are relative to clip window **
// =========== : Public functions
// ----------- : Local functions
// =========================================================================


// Compare two colors; return 0 if equal
//============================================
int TFT_compare_colors(color_t c1, color_t c2)
{
	return (c1 == c2) ? 0 : 1;
}

//-------------------------------------------------------------------------------------------------
static void TFT_send_data(int x1, int y1, int x2, int y2, uint32_t len, color_t *buf, uint8_t wait)
{
    send_data(x1, y1, x2, y2, len, buf);
}

// draw color pixel on screen
//----------------------------------------------------------------------------
static void TFT_drawPixe(int16_t x, int16_t y, color_t color, uint8_t sel)
{
    if ((x < dispWin.x1) || (y < dispWin.y1) || (x > dispWin.x2) || (y > dispWin.y2)) return;

    drawPixel(x, y, color);
}

//-------------------------------------------------------------------------------------------
static void TFT_pushRepColor(int x1, int y1, int x2, int y2, color_t color, uint32_t len)
{
    if (len == 0) return;

    TFT_pushColorRep(x1, y1, x2, y2, color, len);
}

//===========================================================================================


//====================================================================
void TFT_drawPixel(int16_t x, int16_t y, color_t color, uint8_t sel) {

	TFT_drawPixe(x+dispWin.x1, y+dispWin.y1, color, sel);
}

//--------------------------------------------------------------------------
static void _drawFastVLine(int16_t x, int16_t y, int16_t h, color_t color) {
	// clipping
	if ((x < dispWin.x1) || (x > dispWin.x2) || (y > dispWin.y2)) return;
	if (y < dispWin.y1) {
		h -= (dispWin.y1 - y);
		y = dispWin.y1;
	}
	if (h < 0) h = 0;
	if ((y + h) > (dispWin.y2+1)) h = dispWin.y2 - y + 1;
	if (h == 0) h = 1;
	TFT_pushRepColor(x, y, x, y+h-1, color, (uint32_t)h);
}

//--------------------------------------------------------------------------
static void _drawFastHLine(int16_t x, int16_t y, int16_t w, color_t color) {
	// clipping
	if ((y < dispWin.y1) || (x > dispWin.x2) || (y > dispWin.y2)) return;
	if (x < dispWin.x1) {
		w -= (dispWin.x1 - x);
		x = dispWin.x1;
	}
	if (w < 0) w = 0;
	if ((x + w) > (dispWin.x2+1)) w = dispWin.x2 - x + 1;
	if (w == 0) w = 1;

	TFT_pushRepColor(x, y, x+w-1, y, color, (uint32_t)w);
}

//======================================================================
void TFT_drawFastVLine(int16_t x, int16_t y, int16_t h, color_t color) {
	_drawFastVLine(x+dispWin.x1, y+dispWin.y1, h, color);
}

//======================================================================
void TFT_drawFastHLine(int16_t x, int16_t y, int16_t w, color_t color) {
	_drawFastHLine(x+dispWin.x1, y+dispWin.y1, w, color);
}

// Bresenham's algorithm, speed enhanced by Bodmer this uses
// the efficient FastH/V Line draw routine for segments of 2 pixels or more
//----------------------------------------------------------------------------------
static void _drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, color_t color)
{
  if (x0 == x1) {
	  if (y0 <= y1) _drawFastVLine(x0, y0, y1-y0, color);
	  else _drawFastVLine(x0, y1, y0-y1, color);
	  return;
  }
  if (y0 == y1) {
	  if (x0 <= x1) _drawFastHLine(x0, y0, x1-x0, color);
	  else _drawFastHLine(x1, y0, x0-x1, color);
	  return;
  }

  int steep = 0;
  if (abs(y1 - y0) > abs(x1 - x0)) steep = 1;
  if (steep) {
    swap(x0, y0);
    swap(x1, y1);
  }
  if (x0 > x1) {
    swap(x0, x1);
    swap(y0, y1);
  }

  int16_t dx = x1 - x0, dy = abs(y1 - y0);
  int16_t err = dx >> 1, ystep = -1, xs = x0, dlen = 0;

  if (y0 < y1) ystep = 1;

  // Split into steep and not steep for FastH/V separation
  if (steep) {
    for (; x0 <= x1; x0++) {
      dlen++;
      err -= dy;
      if (err < 0) {
        err += dx;
        if (dlen == 1) TFT_drawPixe(y0, xs, color, 1);
        else _drawFastVLine(y0, xs, dlen, color);
        dlen = 0; y0 += ystep; xs = x0 + 1;
      }
    }
    if (dlen) _drawFastVLine(y0, xs, dlen, color);
  }
  else
  {
    for (; x0 <= x1; x0++) {
      dlen++;
      err -= dy;
      if (err < 0) {
        err += dx;
        if (dlen == 1) TFT_drawPixe(xs, y0, color, 1);
        else _drawFastHLine(xs, y0, dlen, color);
        dlen = 0; y0 += ystep; xs = x0 + 1;
      }
    }
    if (dlen) _drawFastHLine(xs, y0, dlen, color);
  }
}

//==============================================================================
void TFT_drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, color_t color)
{
	_drawLine(x0+dispWin.x1, y0+dispWin.y1, x1+dispWin.x1, y1+dispWin.y1, color);
}

// fill a rectangle
//--------------------------------------------------------------------------------
static void _fillRect(int16_t x, int16_t y, int16_t w, int16_t h, color_t color) {
	// clipping
	if ((x >= dispWin.x2) || (y > dispWin.y2)) return;

	if (x < dispWin.x1) {
		w -= (dispWin.x1 - x);
		x = dispWin.x1;
	}
	if (y < dispWin.y1) {
		h -= (dispWin.y1 - y);
		y = dispWin.y1;
	}
	if (w < 0) w = 0;
	if (h < 0) h = 0;

	if ((x + w) > (dispWin.x2+1)) w = dispWin.x2 - x + 1;
	if ((y + h) > (dispWin.y2+1)) h = dispWin.y2 - y + 1;
	if (w == 0) w = 1;
	if (h == 0) h = 1;
	TFT_pushRepColor(x, y, x+w-1, y+h-1, color, (uint32_t)(h*w));
}

//============================================================================
void TFT_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, color_t color) {
	_fillRect(x+dispWin.x1, y+dispWin.y1, w, h, color);
}

//==================================
void TFT_fillScreen(color_t color) {
    TFT_pushRepColor(0, 0, _width-1, _height-1, color, (uint32_t)(_height*_width));
}

//==================================
void TFT_fillWindow(color_t color) {
    TFT_pushRepColor(dispWin.x1, dispWin.y1, dispWin.x2, dispWin.y2,
			color, (uint32_t)((dispWin.x2-dispWin.x1+1) * (dispWin.y2-dispWin.y1+1)));
}

// ^^^============= Basics drawing functions ================================^^^


// ================ Graphics drawing functions ==================================

//-----------------------------------------------------------------------------------
static void _drawRect(uint16_t x1,uint16_t y1,uint16_t w,uint16_t h, color_t color) {
  _drawFastHLine(x1,y1,w, color);
  _drawFastVLine(x1+w-1,y1,h, color);
  _drawFastHLine(x1,y1+h-1,w, color);
  _drawFastVLine(x1,y1,h, color);
}

//===============================================================================
void TFT_drawRect(uint16_t x1,uint16_t y1,uint16_t w,uint16_t h, color_t color) {
	_drawRect(x1+dispWin.x1, y1+dispWin.y1, w, h, color);
}

//-------------------------------------------------------------------------------------------------
static void drawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, color_t color)
{
	int16_t f = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x = 0;
	int16_t y = r;

	while (x < y) {
		if (f >= 0) {
			y--;
			ddF_y += 2;
			f += ddF_y;
		}
		x++;
		ddF_x += 2;
		f += ddF_x;
		if (cornername & 0x4) {
			TFT_drawPixe(x0 + x, y0 + y, color, 0);
			TFT_drawPixe(x0 + y, y0 + x, color, 0);
		}
		if (cornername & 0x2) {
			TFT_drawPixe(x0 + x, y0 - y, color, 0);
			TFT_drawPixe(x0 + y, y0 - x, color, 0);
		}
		if (cornername & 0x8) {
			TFT_drawPixe(x0 - y, y0 + x, color, 0);
			TFT_drawPixe(x0 - x, y0 + y, color, 0);
		}
		if (cornername & 0x1) {
			TFT_drawPixe(x0 - y, y0 - x, color, 0);
			TFT_drawPixe(x0 - x, y0 - y, color, 0);
		}
	}
}

// Used to do circles and roundrects
//----------------------------------------------------------------------------------------------------------------
static void fillCircleHelper(int16_t x0, int16_t y0, int16_t r,	uint8_t cornername, int16_t delta, color_t color)
{
	int16_t f = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x = 0;
	int16_t y = r;
	int16_t ylm = x0 - r;

	while (x < y) {
		if (f >= 0) {
			if (cornername & 0x1) _drawFastVLine(x0 + y, y0 - x, 2 * x + 1 + delta, color);
			if (cornername & 0x2) _drawFastVLine(x0 - y, y0 - x, 2 * x + 1 + delta, color);
			ylm = x0 - y;
			y--;
			ddF_y += 2;
			f += ddF_y;
		}
		x++;
		ddF_x += 2;
		f += ddF_x;

		if ((x0 - x) > ylm) {
			if (cornername & 0x1) _drawFastVLine(x0 + x, y0 - y, 2 * y + 1 + delta, color);
			if (cornername & 0x2) _drawFastVLine(x0 - x, y0 - y, 2 * y + 1 + delta, color);
		}
	}
}

// Draw a rounded rectangle
//=============================================================================================
void TFT_drawRoundRect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t r, color_t color)
{
	x += dispWin.x1;
	y += dispWin.y1;

	// smarter version
	_drawFastHLine(x + r, y, w - 2 * r, color);			// Top
	_drawFastHLine(x + r, y + h - 1, w - 2 * r, color);	// Bottom
	_drawFastVLine(x, y + r, h - 2 * r, color);			// Left
	_drawFastVLine(x + w - 1, y + r, h - 2 * r, color);	// Right

	// draw four corners
	drawCircleHelper(x + r, y + r, r, 1, color);
	drawCircleHelper(x + w - r - 1, y + r, r, 2, color);
	drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4, color);
	drawCircleHelper(x + r, y + h - r - 1, r, 8, color);
}

// Fill a rounded rectangle
//=============================================================================================
void TFT_fillRoundRect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t r, color_t color)
{
	x += dispWin.x1;
	y += dispWin.y1;

	// smarter version
	_fillRect(x + r, y, w - 2 * r, h, color);

	// draw four corners
	fillCircleHelper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, color);
	fillCircleHelper(x + r, y + r, r, 2, h - 2 * r - 1, color);
}

//-----------------------------------------------------------------------------------------------
static void _drawLineByAngle(int16_t x, int16_t y, int16_t angle, uint16_t length, color_t color)
{
	_drawLine(
		x,
		y,
		x + length * cos((angle + _angleOffset) * DEG_TO_RAD),
		y + length * sin((angle + _angleOffset) * DEG_TO_RAD), color);
}

//---------------------------------------------------------------------------------------------------------------
static void _DrawLineByAngle(int16_t x, int16_t y, int16_t angle, uint16_t start, uint16_t length, color_t color)
{
	_drawLine(
		x + start * cos((angle + _angleOffset) * DEG_TO_RAD),
		y + start * sin((angle + _angleOffset) * DEG_TO_RAD),
		x + (start + length) * cos((angle + _angleOffset) * DEG_TO_RAD),
		y + (start + length) * sin((angle + _angleOffset) * DEG_TO_RAD), color);
}

//===========================================================================================================
void TFT_drawLineByAngle(uint16_t x, uint16_t y, uint16_t start, uint16_t len, uint16_t angle, color_t color)
{
	x += dispWin.x1;
	y += dispWin.y1;

	if (start == 0) _drawLineByAngle(x, y, angle, len, color);
	else _DrawLineByAngle(x, y, angle, start, len, color);
}


// Draw a triangle
//--------------------------------------------------------------------------------------------------------------------
static void _drawTriangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, color_t color)
{
	_drawLine(x0, y0, x1, y1, color);
	_drawLine(x1, y1, x2, y2, color);
	_drawLine(x2, y2, x0, y0, color);
}

//================================================================================================================
void TFT_drawTriangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, color_t color)
{
	x0 += dispWin.x1;
	y0 += dispWin.y1;
	x1 += dispWin.x1;
	y1 += dispWin.y1;
	x2 += dispWin.x1;
	y2 += dispWin.y1;

	_drawLine(x0, y0, x1, y1, color);
	_drawLine(x1, y1, x2, y2, color);
	_drawLine(x2, y2, x0, y0, color);
}

// Fill a triangle
//--------------------------------------------------------------------------------------------------------------------
static void _fillTriangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, color_t color)
{
  int16_t a, b, y, last;

  // Sort coordinates by Y order (y2 >= y1 >= y0)
  if (y0 > y1) {
    swap(y0, y1); swap(x0, x1);
  }
  if (y1 > y2) {
    swap(y2, y1); swap(x2, x1);
  }
  if (y0 > y1) {
    swap(y0, y1); swap(x0, x1);
  }

  if(y0 == y2) { // Handle awkward all-on-same-line case as its own thing
    a = b = x0;
    if(x1 < a)      a = x1;
    else if(x1 > b) b = x1;
    if(x2 < a)      a = x2;
    else if(x2 > b) b = x2;
    _drawFastHLine(a, y0, b-a+1, color);
    return;
  }

  int16_t
    dx01 = x1 - x0,
    dy01 = y1 - y0,
    dx02 = x2 - x0,
    dy02 = y2 - y0,
    dx12 = x2 - x1,
    dy12 = y2 - y1;
  int32_t
    sa   = 0,
    sb   = 0;

  // For upper part of triangle, find scanline crossings for segments
  // 0-1 and 0-2.  If y1=y2 (flat-bottomed triangle), the scanline y1
  // is included here (and second loop will be skipped, avoiding a /0
  // error there), otherwise scanline y1 is skipped here and handled
  // in the second loop...which also avoids a /0 error here if y0=y1
  // (flat-topped triangle).
  if(y1 == y2) last = y1;   // Include y1 scanline
  else         last = y1-1; // Skip it

  for(y=y0; y<=last; y++) {
    a   = x0 + sa / dy01;
    b   = x0 + sb / dy02;
    sa += dx01;
    sb += dx02;
    /* longhand:
    a = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
    b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
    */
    if(a > b) swap(a,b);
    _drawFastHLine(a, y, b-a+1, color);
  }

  // For lower part of triangle, find scanline crossings for segments
  // 0-2 and 1-2.  This loop is skipped if y1=y2.
  sa = dx12 * (y - y1);
  sb = dx02 * (y - y0);
  for(; y<=y2; y++) {
    a   = x1 + sa / dy12;
    b   = x0 + sb / dy02;
    sa += dx12;
    sb += dx02;
    /* longhand:
    a = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
    b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
    */
    if(a > b) swap(a,b);
    _drawFastHLine(a, y, b-a+1, color);
  }
}

//================================================================================================================
void TFT_fillTriangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, color_t color)
{
	_fillTriangle(
			x0 + dispWin.x1, y0 + dispWin.y1,
			x1 + dispWin.x1, y1 + dispWin.y1,
			x2 + dispWin.x1, y2 + dispWin.y1,
			color);
}

//====================================================================
void TFT_drawCircle(int16_t x, int16_t y, int radius, color_t color) {
	x += dispWin.x1;
	y += dispWin.y1;
	int f = 1 - radius;
	int ddF_x = 1;
	int ddF_y = -2 * radius;
	int x1 = 0;
	int y1 = radius;

	TFT_drawPixe(x, y + radius, color, 0);
	TFT_drawPixe(x, y - radius, color, 0);
	TFT_drawPixe(x + radius, y, color, 0);
	TFT_drawPixe(x - radius, y, color, 0);
	while(x1 < y1) {
		if (f >= 0) {
			y1--;
			ddF_y += 2;
			f += ddF_y;
		}
		x1++;
		ddF_x += 2;
		f += ddF_x;
		TFT_drawPixe(x + x1, y + y1, color, 0);
		TFT_drawPixe(x - x1, y + y1, color, 0);
		TFT_drawPixe(x + x1, y - y1, color, 0);
		TFT_drawPixe(x - x1, y - y1, color, 0);
		TFT_drawPixe(x + y1, y + x1, color, 0);
		TFT_drawPixe(x - y1, y + x1, color, 0);
		TFT_drawPixe(x + y1, y - x1, color, 0);
		TFT_drawPixe(x - y1, y - x1, color, 0);
	}
}

//====================================================================
void TFT_fillCircle(int16_t x, int16_t y, int radius, color_t color) {
	x += dispWin.x1;
	y += dispWin.y1;

	_drawFastVLine(x, y-radius, 2*radius+1, color);
	fillCircleHelper(x, y, radius, 3, 0, color);
}

//----------------------------------------------------------------------------------------------------------------
static void _draw_ellipse_section(uint16_t x, uint16_t y, uint16_t x0, uint16_t y0, color_t color, uint8_t option)
{
    // upper right
    if ( option & TFT_ELLIPSE_UPPER_RIGHT ) TFT_drawPixe(x0 + x, y0 - y, color, 0);
    // upper left
    if ( option & TFT_ELLIPSE_UPPER_LEFT ) TFT_drawPixe(x0 - x, y0 - y, color, 0);
    // lower right
    if ( option & TFT_ELLIPSE_LOWER_RIGHT ) TFT_drawPixe(x0 + x, y0 + y, color, 0);
    // lower left
    if ( option & TFT_ELLIPSE_LOWER_LEFT ) TFT_drawPixe(x0 - x, y0 + y, color, 0);
}

//=====================================================================================================
void TFT_drawEllipse(uint16_t x0, uint16_t y0, uint16_t rx, uint16_t ry, color_t color, uint8_t option)
{
	x0 += dispWin.x1;
	y0 += dispWin.y1;

	uint16_t x, y;
	int32_t xchg, ychg;
	int32_t err;
	int32_t rxrx2;
	int32_t ryry2;
	int32_t stopx, stopy;

	rxrx2 = rx;
	rxrx2 *= rx;
	rxrx2 *= 2;

	ryry2 = ry;
	ryry2 *= ry;
	ryry2 *= 2;

	x = rx;
	y = 0;

	xchg = 1;
	xchg -= rx;
	xchg -= rx;
	xchg *= ry;
	xchg *= ry;

	ychg = rx;
	ychg *= rx;

	err = 0;

	stopx = ryry2;
	stopx *= rx;
	stopy = 0;

	while( stopx >= stopy ) {
		_draw_ellipse_section(x, y, x0, y0, color, option);
		y++;
		stopy += rxrx2;
		err += ychg;
		ychg += rxrx2;
		if ( 2*err+xchg > 0 ) {
			x--;
			stopx -= ryry2;
			err += xchg;
			xchg += ryry2;
		}
	}

	x = 0;
	y = ry;

	xchg = ry;
	xchg *= ry;

	ychg = 1;
	ychg -= ry;
	ychg -= ry;
	ychg *= rx;
	ychg *= rx;

	err = 0;

	stopx = 0;

	stopy = rxrx2;
	stopy *= ry;

	while( stopx <= stopy ) {
		_draw_ellipse_section(x, y, x0, y0, color, option);
		x++;
		stopx += ryry2;
		err += xchg;
		xchg += ryry2;
		if ( 2*err+ychg > 0 ) {
			y--;
			stopy -= rxrx2;
			err += ychg;
			ychg += rxrx2;
		}
	}
}

//-----------------------------------------------------------------------------------------------------------------------
static void _draw_filled_ellipse_section(uint16_t x, uint16_t y, uint16_t x0, uint16_t y0, color_t color, uint8_t option)
{
    // upper right
    if ( option & TFT_ELLIPSE_UPPER_RIGHT ) _drawFastVLine(x0+x, y0-y, y+1, color);
    // upper left
    if ( option & TFT_ELLIPSE_UPPER_LEFT ) _drawFastVLine(x0-x, y0-y, y+1, color);
    // lower right
    if ( option & TFT_ELLIPSE_LOWER_RIGHT ) _drawFastVLine(x0+x, y0, y+1, color);
    // lower left
    if ( option & TFT_ELLIPSE_LOWER_LEFT ) _drawFastVLine(x0-x, y0, y+1, color);
}

//=====================================================================================================
void TFT_fillEllipse(uint16_t x0, uint16_t y0, uint16_t rx, uint16_t ry, color_t color, uint8_t option)
{
	x0 += dispWin.x1;
	y0 += dispWin.y1;

	uint16_t x, y;
	int32_t xchg, ychg;
	int32_t err;
	int32_t rxrx2;
	int32_t ryry2;
	int32_t stopx, stopy;

	rxrx2 = rx;
	rxrx2 *= rx;
	rxrx2 *= 2;

	ryry2 = ry;
	ryry2 *= ry;
	ryry2 *= 2;

	x = rx;
	y = 0;

	xchg = 1;
	xchg -= rx;
	xchg -= rx;
	xchg *= ry;
	xchg *= ry;

	ychg = rx;
	ychg *= rx;

	err = 0;

	stopx = ryry2;
	stopx *= rx;
	stopy = 0;

	while( stopx >= stopy ) {
		_draw_filled_ellipse_section(x, y, x0, y0, color, option);
		y++;
		stopy += rxrx2;
		err += ychg;
		ychg += rxrx2;
		if ( 2*err+xchg > 0 ) {
			x--;
			stopx -= ryry2;
			err += xchg;
			xchg += ryry2;
		}
	}

	x = 0;
	y = ry;

	xchg = ry;
	xchg *= ry;

	ychg = 1;
	ychg -= ry;
	ychg -= ry;
	ychg *= rx;
	ychg *= rx;

	err = 0;

	stopx = 0;

	stopy = rxrx2;
	stopy *= ry;

	while( stopx <= stopy ) {
		_draw_filled_ellipse_section(x, y, x0, y0, color, option);
		x++;
		stopx += ryry2;
		err += xchg;
		xchg += ryry2;
		if ( 2*err+ychg > 0 ) {
			y--;
			stopy -= rxrx2;
			err += ychg;
			ychg += rxrx2;
		}
	}
}


// ==== ARC DRAWING ===================================================================

//---------------------------------------------------------------------------------------------------------------------------------
static void _fillArcOffsetted(uint16_t cx, uint16_t cy, uint16_t radius, uint16_t thickness, float start, float end, color_t color)
{
	//float sslope = (float)cos_lookup(start) / (float)sin_lookup(start);
	//float eslope = (float)cos_lookup(end) / (float)sin_lookup(end);
	float sslope = (cos(start/_arcAngleMax * 2 * PI) * _arcAngleMax) / (sin(start/_arcAngleMax * 2 * PI) * _arcAngleMax) ;
	float eslope = (cos(end/_arcAngleMax * 2 * PI) * _arcAngleMax) / (sin(end/_arcAngleMax * 2 * PI) * _arcAngleMax);

	if (end == 360) eslope = -1000000;

	int ir2 = (radius - thickness) * (radius - thickness);
	int or2 = radius * radius;

	for (int x = -radius; x <= radius; x++) {
		for (int y = -radius; y <= radius; y++) {
			int x2 = x * x;
			int y2 = y * y;

			if (
				(x2 + y2 < or2 && x2 + y2 >= ir2) &&
				(
				(y > 0 && start < 180 && x <= y * sslope) ||
				(y < 0 && start > 180 && x >= y * sslope) ||
				(y < 0 && start <= 180) ||
				(y == 0 && start <= 180 && x < 0) ||
				(y == 0 && start == 0 && x > 0)
				) &&
				(
				(y > 0 && end < 180 && x >= y * eslope) ||
				(y < 0 && end > 180 && x <= y * eslope) ||
				(y > 0 && end >= 180) ||
				(y == 0 && end >= 180 && x < 0) ||
				(y == 0 && start == 0 && x > 0)
				)
				)
				TFT_drawPixe(cx+x, cy+y, color, 0);
		}
	}
}


//===========================================================================================================================
void TFT_drawArc(uint16_t cx, uint16_t cy, uint16_t r, uint16_t th, float start, float end, color_t color, color_t fillcolor)
{
	cx += dispWin.x1;
	cy += dispWin.y1;

	if (th < 1) th = 1;
	if (th > r) th = r;

	int f = TFT_compare_colors(fillcolor, color);

	float astart = fmodf(start, _arcAngleMax);
	float aend = fmodf(end, _arcAngleMax);

	astart += _angleOffset;
	aend += _angleOffset;

	if (astart < 0) astart += (float)360;
	if (aend < 0) aend += (float)360;

	if (aend == 0) aend = (float)360;

	if (astart > aend) {
		_fillArcOffsetted(cx, cy, r, th, astart, _arcAngleMax, fillcolor);
		_fillArcOffsetted(cx, cy, r, th, 0, aend, fillcolor);
		if (f) {
			_fillArcOffsetted(cx, cy, r, 1, astart, _arcAngleMax, color);
			_fillArcOffsetted(cx, cy, r, 1, 0, aend, color);
			_fillArcOffsetted(cx, cy, r-th, 1, astart, _arcAngleMax, color);
			_fillArcOffsetted(cx, cy, r-th, 1, 0, aend, color);
		}
	}
	else {
		_fillArcOffsetted(cx, cy, r, th, astart, aend, fillcolor);
		if (f) {
			_fillArcOffsetted(cx, cy, r, 1, astart, aend, color);
			_fillArcOffsetted(cx, cy, r-th, 1, astart, aend, color);
		}
	}
	if (f) {
		_drawLine(cx + (r-th) * cos(astart * DEG_TO_RAD), cy + (r-th) * sin(astart * DEG_TO_RAD),
			cx + (r-1) * cos(astart * DEG_TO_RAD), cy + (r-1) * sin(astart * DEG_TO_RAD), color);
		_drawLine(cx + (r-th) * cos(aend * DEG_TO_RAD), cy + (r-th) * sin(aend * DEG_TO_RAD),
			cx + (r-1) * cos(aend * DEG_TO_RAD), cy + (r-1) * sin(aend * DEG_TO_RAD), color);
	}
}

//=============================================================================================================
void TFT_drawPolygon(int cx, int cy, int sides, int diameter, color_t color, color_t fill, int rot, uint8_t th)
{
	cx += dispWin.x1;
	cy += dispWin.y1;

	int deg = rot - _angleOffset;
	int f = TFT_compare_colors(fill, color);

	if (sides < MIN_POLIGON_SIDES) sides = MIN_POLIGON_SIDES;	// This ensures the minimum side number
	if (sides > MAX_POLIGON_SIDES) sides = MAX_POLIGON_SIDES;	// This ensures the maximum side number

	int Xpoints[sides], Ypoints[sides];							// Set the arrays based on the number of sides entered
	int rads = 360 / sides;										// This equally spaces the points.

	for (int idx = 0; idx < sides; idx++) {
		Xpoints[idx] = cx + sin((float)(idx*rads + deg) * deg_to_rad) * diameter;
		Ypoints[idx] = cy + cos((float)(idx*rads + deg) * deg_to_rad) * diameter;
	}

	// Draw the polygon on the screen.
	if (f) {
		for(int idx = 0; idx < sides; idx++) {
			if((idx+1) < sides) _fillTriangle(cx,cy,Xpoints[idx],Ypoints[idx],Xpoints[idx+1],Ypoints[idx+1], fill);
			else _fillTriangle(cx,cy,Xpoints[idx],Ypoints[idx],Xpoints[0],Ypoints[0], fill);
		}
	}

	if (th) {
		for (int n=0; n<th; n++) {
			if (n > 0) {
				for (int idx = 0; idx < sides; idx++) {
					Xpoints[idx] = cx + sin((float)(idx*rads + deg) * deg_to_rad) * (diameter-n);
					Ypoints[idx] = cy + cos((float)(idx*rads + deg) * deg_to_rad) * (diameter-n);
				}
			}
			for(int idx = 0; idx < sides; idx++) {
				if( (idx+1) < sides)
					_drawLine(Xpoints[idx],Ypoints[idx],Xpoints[idx+1],Ypoints[idx+1], color); // draw the lines
				else
					_drawLine(Xpoints[idx],Ypoints[idx],Xpoints[0],Ypoints[0], color); // finishes the last line to close up the polygon.
			}
		}
	}
}

/*
// Similar to the Polygon function.
//=====================================================================================
void TFT_drawStar(int cx, int cy, int diameter, color_t color, bool fill, float factor)
{
	cx += dispWin.x1;
	cy += dispWin.y1;

	factor = constrain(factor, 1.0, 4.0);
	uint8_t sides = 5;
	uint8_t rads = 360 / sides;

	int Xpoints_O[sides], Ypoints_O[sides], Xpoints_I[sides], Ypoints_I[sides];//Xpoints_T[5], Ypoints_T[5];

	for(int idx = 0; idx < sides; idx++) {
		// makes the outer points
		Xpoints_O[idx] = cx + sin((float)(idx*rads + 72) * deg_to_rad) * diameter;
		Ypoints_O[idx] = cy + cos((float)(idx*rads + 72) * deg_to_rad) * diameter;
		// makes the inner points
		Xpoints_I[idx] = cx + sin((float)(idx*rads + 36) * deg_to_rad) * ((float)(diameter)/factor);
		// 36 is half of 72, and this will allow the inner and outer points to line up like a triangle.
		Ypoints_I[idx] = cy + cos((float)(idx*rads + 36) * deg_to_rad) * ((float)(diameter)/factor);
	}

	for(int idx = 0; idx < sides; idx++) {
		if((idx+1) < sides) {
			if(fill) {// this part below should be self explanatory. It fills in the star.
				_fillTriangle(cx,cy,Xpoints_I[idx],Ypoints_I[idx],Xpoints_O[idx],Ypoints_O[idx], color);
				_fillTriangle(cx,cy,Xpoints_O[idx],Ypoints_O[idx],Xpoints_I[idx+1],Ypoints_I[idx+1], color);
			}
			else {
				_drawLine(Xpoints_O[idx],Ypoints_O[idx],Xpoints_I[idx+1],Ypoints_I[idx+1], color);
				_drawLine(Xpoints_I[idx],Ypoints_I[idx],Xpoints_O[idx],Ypoints_O[idx], color);
			}
		}
		else {
			if(fill) {
				_fillTriangle(cx,cy,Xpoints_I[0],Ypoints_I[0],Xpoints_O[idx],Ypoints_O[idx], color);
				_fillTriangle(cx,cy,Xpoints_O[idx],Ypoints_O[idx],Xpoints_I[idx],Ypoints_I[idx], color);
			}
			else {
				_drawLine(Xpoints_O[idx],Ypoints_O[idx],Xpoints_I[idx],Ypoints_I[idx], color);
				_drawLine(Xpoints_I[0],Ypoints_I[0],Xpoints_O[idx],Ypoints_O[idx], color);
			}
		}
	}
}
*/

// ================ Font and string functions ==================================

//----------------------------------------------------
static int load_file_font(mp_obj_t fontfile, int info)
{
	int err = 0;
	char err_msg[256] = {'\0'};

	if (userfont != NULL) {
		free(userfont);
		userfont = NULL;
	}

    mp_obj_t args[2];
    args[0] = fontfile;
    args[1] = mp_obj_new_str("rb", 2);

    // Open the file
    mp_obj_t ffd = mp_vfs_open(2, args, (mp_map_t*)&mp_const_empty_map);
    if (!ffd) {
        sprintf(err_msg, "Error opening font file");
        err = 1;
        goto exit;
    }

	// Get file size
    int fsize = mp_stream_posix_lseek((void *)ffd, 0, SEEK_END);
    int at_start = mp_stream_posix_lseek((void *)ffd, 0, SEEK_SET);
    if ((fsize <= 0) || (at_start != 0)) {
        sprintf(err_msg, "Error getting font file size");
        err = 2;
        goto exit;
    }

	if (fsize < 20) {
		sprintf(err_msg, "Wrong font size");
		err = 3;
		goto exit;
	}

	userfont = malloc(fsize+4);
	if (userfont == NULL) {
		sprintf(err_msg, "Font memory allocation error");
        mp_stream_close(ffd);
		err = 4;
		goto exit;
	}

	int read = mp_stream_posix_read((void *)ffd, userfont, fsize);

    mp_stream_close(ffd);

	if (read != fsize) {
		sprintf(err_msg, "Font read error");
		err = 5;
		goto exit;
	}

	userfont[read] = 0;
	if (strstr((char *)(userfont+read-8), "RPH_font") == NULL) {
		sprintf(err_msg, "Font ID not found");
		err = 6;
		goto exit;
	}

	// Check font size
	int size = 0;
	int numchar = 0;
	int width = userfont[0];
	int height = userfont[1];
	uint8_t first = 255;
	uint8_t last = 0;
	int pminwidth = 255;
	int pmaxwidth = 0;

	if (width != 0) {
		// Fixed font
		numchar = userfont[3];
		first = userfont[2];
		last = first + numchar - 1;
		size = ((width * height * numchar) / 8) + 4;
	}
	else {
		// Proportional font, calculate size from character data
		size = 4; // point at first char data
		uint8_t charCode;
		int charwidth;
		// scan all characters
		while (size < (read-14)) {
		    charCode = userfont[size];
            if (charCode == 0) break;
		    charwidth = userfont[size+2];

            numchar++;
            if (charwidth != 0) size += ((((charwidth * userfont[size+3])-1) / 8) + 7);
            else size += 6;

            if (info) {
                if (charwidth > pmaxwidth) pmaxwidth = charwidth;
                if (charwidth < pminwidth) pminwidth = charwidth;
                if (charCode < first) first = charCode;
                if (charCode > last) last = charCode;
            }
		  }
	}

	if (((read-8-size) > 2) || ((read-8-size) < 0)) {
		sprintf(err_msg, "Font size error: found %d expected %d)", size, (read-8));
		err = 7;
		goto exit;
	}
	userfont[size] = 0;

	if (info) {
		if (width != 0) {
			mp_printf(&mp_plat_print, "Fixed width font:\r\n  size: %d  width: %d  height: %d  characters: %d (%d~%d)\n",
					size, width, height, numchar, first, last);
		}
		else {
			mp_printf(&mp_plat_print, "Proportional font:\r\n  size: %d  width: %d~%d  height: %d  characters: %d (%d~%d)\n",
					size, pminwidth, pmaxwidth, height, numchar, first, last);
		}
	}

exit:
	if (err) {
		if (userfont) {
			free(userfont);
			userfont = NULL;
		}
		if (info) mp_printf(&mp_plat_print, "Error: %d [%s]\r\n", err, err_msg);
	}
	return err;
}

// Get max width & height of the proportional font
//-----------------------------
static void getMaxWidthHeight()
{
	uint16_t tempPtr = 4; // point at first char data
	uint8_t cc, cw, ch, cd, cy;

	cfont.numchars = 0;
	cfont.max_x_size = 0;

    cc = cfont.font[tempPtr++];
    while (cc)  {
    	cfont.numchars++;
        cy = cfont.font[tempPtr++];
        cw = cfont.font[tempPtr++];
        ch = cfont.font[tempPtr++];
        tempPtr++;
        cd = cfont.font[tempPtr++];
        cy += ch;
		if (cw > cfont.max_x_size) cfont.max_x_size = cw;
		if (cd > cfont.max_x_size) cfont.max_x_size = cd;
		if (ch > cfont.y_size) cfont.y_size = ch;
		if (cy > cfont.y_size) cfont.y_size = cy;
		if (cw != 0) {
			// packed bits
			tempPtr += (((cw * ch)-1) / 8) + 1;
		}
	    cc = cfont.font[tempPtr++];
	}
    cfont.size = tempPtr;
}

// Return the Glyph data for an individual character in the proportional font
//------------------------------------
static uint8_t getCharPtr(uint8_t c) {
  uint16_t tempPtr = 4; // point at first char data

  do {
	fontChar.charCode = cfont.font[tempPtr++];
    if (fontChar.charCode == 0xFF) return 0;

    fontChar.adjYOffset = cfont.font[tempPtr++];
    fontChar.width = cfont.font[tempPtr++];
    fontChar.height = cfont.font[tempPtr++];
    fontChar.xOffset = cfont.font[tempPtr++];
    fontChar.xOffset = fontChar.xOffset < 0x80 ? fontChar.xOffset : -(0xFF - fontChar.xOffset);
    fontChar.xDelta = cfont.font[tempPtr++];

    if ((c != fontChar.charCode) && (fontChar.charCode != 0)) {
      if (fontChar.width != 0) {
        // packed bits
        tempPtr += (((fontChar.width * fontChar.height)-1) / 8) + 1;
      }
    }
  } while ((c != fontChar.charCode) && (fontChar.charCode != 0));

  fontChar.dataPtr = tempPtr;
  if (c == fontChar.charCode) {
    if (font_forceFixed > 0) {
      // fix width & offset for forced fixed width
      fontChar.xDelta = cfont.max_x_size;
      fontChar.xOffset = (fontChar.xDelta - fontChar.width) / 2;
    }
  }
  else return 0;

  return 1;
}

//===========================================================
bool TFT_setFont(uint8_t font, mp_obj_t font_file, bool info)
{
  bool res = true;
  cfont.font = NULL;

  if (font == FONT_7SEG) {
    cfont.bitmap = 2;
    cfont.x_size = 24;
    cfont.y_size = 6;
    cfont.offset = 0;
    cfont.color  = _fg;
  }
  else {
      cfont.font = tft_DefaultFont;
	  if (font == USER_FONT) {
		  if (load_file_font(font_file, info) != 0) res = false;
		  else cfont.font = userfont;
	  }
	  else if (font == DEJAVU18_FONT) cfont.font = tft_Dejavu18;
	  else if (font == DEJAVU24_FONT) cfont.font = tft_Dejavu24;
	  else if (font == UBUNTU16_FONT) cfont.font = tft_Ubuntu16;
	  else if (font == COMIC24_FONT) cfont.font = tft_Comic24;
	  else if (font == MINYA24_FONT) cfont.font = tft_minya24;
	  else if (font == TOONEY32_FONT) cfont.font = tft_tooney32;
	  else if (font == SMALL_FONT) cfont.font = tft_SmallFont;
	  else if (font == DEF_SMALL_FONT) cfont.font = tft_def_small;
      else if (font == USER_FONT_1) {
          if (userFont1[1] != 0) cfont.font = userFont1;
          else res = false;
      }
      else if (font == USER_FONT_2) {
          if (userFont2[1] != 0) cfont.font = userFont2;
          else res = false;
      }
      else if (font == USER_FONT_3) {
          if (userFont3[1] != 0) cfont.font = userFont3;
          else res = false;
      }
      else if (font == USER_FONT_4) {
          if (userFont4[1] != 0) cfont.font = userFont4;
          else res = false;
      }
	  else res = false;

	  cfont.bitmap = 1;
	  cfont.x_size = cfont.font[0];
	  cfont.y_size = cfont.font[1];
	  if (cfont.x_size > 0) {
		  cfont.offset = cfont.font[2];
		  cfont.numchars = cfont.font[3];
		  cfont.size = cfont.x_size * cfont.y_size * cfont.numchars;
	  }
	  else {
		  cfont.offset = 4;
		  getMaxWidthHeight();
	  }
  }
  return res;
}

// -----------------------------------------------------------------------------------------
// Individual Proportional Font Character Format:
// -----------------------------------------------------------------------------------------
// Character Code
// yOffset				(start Y of visible pixels)
// Width				(width of the visible pixels)
// Height				(height of the visible pixels)
// xOffset				(start X of visible pixels)
// xDelta				(the distance to move the cursor. Effective width of the character.)
// Data[n]
// -----------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------
// Character drawing rectangle is (0, 0) (xDelta-1, cfont.y_size-1)
// Character visible pixels rectangle is (xOffset, yOffset) (xOffset+Width-1, yOffset+Height-1)
//---------------------------------------------------------------------------------------------

// print non-rotated proportional character
// character is already in fontChar
//----------------------------------------------
static int printProportionalChar(int x, int y) {
	uint8_t ch = 0;
	int i, j, char_width;
    int cx, cy;

	char_width = ((fontChar.width > fontChar.xDelta) ? fontChar.width : fontChar.xDelta);

	if ((font_buffered_char) && (!font_transparent)) {
		int len, bufPos;

		// === buffer Glyph data for faster sending ===
		len = (char_width+1) * cfont.y_size;
		color_t *color_line = malloc(len*3);
		if (color_line) {
			// fill with background color
			for (int n = 0; n < len; n++) {
				color_line[n] = _bg;
			}
			// set character pixels to foreground color
			uint8_t mask = 0x80;
			for (j=0; j < fontChar.height; j++) {
				for (i=0; i < fontChar.width; i++) {
					if (((i + (j*fontChar.width)) % 8) == 0) {
						mask = 0x80;
						ch = cfont.font[fontChar.dataPtr++];
					}
					if ((ch & mask) != 0) {
						// visible pixel
		                cx = (uint16_t)(fontChar.xOffset+i);
		                cy = (uint16_t)(j+fontChar.adjYOffset);
		                bufPos = ((char_width+1) * cy) + cx;
						//bufPos = ((j + fontChar.adjYOffset) * (char_width+1)) + (fontChar.xOffset + i);  // bufY + bufX
						color_line[bufPos] = _fg;
					}
					mask >>= 1;
				}
			}
			// send to display in one transaction
			TFT_send_data(x, y, x+char_width+1, y+cfont.y_size, len, color_line, 1);
			free(color_line);

			return char_width;
		}
	}

	if (!font_transparent) _fillRect(x, y, char_width+1, cfont.y_size, _bg);

	// draw Glyph
	uint8_t mask = 0x80;
	for (j=0; j < fontChar.height; j++) {
		for (i=0; i < fontChar.width; i++) {
			if (((i + (j*fontChar.width)) % 8) == 0) {
				mask = 0x80;
				ch = cfont.font[fontChar.dataPtr++];
			}

			if ((ch & mask) !=0) {
				cx = (uint16_t)(x+fontChar.xOffset+i);
				cy = (uint16_t)(y+j+fontChar.adjYOffset);
				TFT_drawPixe(cx, cy, _fg, 0);
			}
			mask >>= 1;
		}
	}

	return char_width;
}

// non-rotated fixed width character
//----------------------------------------------
static void printChar(uint8_t c, int x, int y) {
	uint8_t i, j, ch, fz, mask;
	uint16_t k, temp, cx, cy, len;

	// fz = bytes per char row
	fz = cfont.x_size/8;
	if (cfont.x_size % 8) fz++;

	// get character position in buffer
	temp = ((c-cfont.offset)*((fz)*cfont.y_size))+4;

	if ((font_buffered_char) && (!font_transparent)) {
		// === buffer Glyph data for faster sending ===
		len = cfont.x_size * cfont.y_size;
		color_t *color_line = malloc(len*3);
		if (color_line) {
			// fill with background color
			for (int n = 0; n < len; n++) {
				color_line[n] = _bg;
			}
			// set character pixels to foreground color
			for (j=0; j<cfont.y_size; j++) {
				for (k=0; k < fz; k++) {
					ch = cfont.font[temp+k];
					mask=0x80;
					for (i=0; i<8; i++) {
						if ((ch & mask) !=0) color_line[(j*cfont.x_size) + (i+(k*8))] = _fg;
						mask >>= 1;
					}
				}
				temp += (fz);
			}
			// send to display
			TFT_send_data(x, y, x+cfont.x_size, y+cfont.y_size, len, color_line, 1);
			free(color_line);

			return;
		}
	}

	if (!font_transparent) _fillRect(x, y, cfont.x_size, cfont.y_size, _bg);

	for (j=0; j<cfont.y_size; j++) {
		for (k=0; k < fz; k++) {
			ch = cfont.font[temp+k];
			mask=0x80;
			for (i=0; i<8; i++) {
				if ((ch & mask) !=0) {
					cx = (uint16_t)(x+i+(k*8));
					cy = (uint16_t)(y+j);
					TFT_drawPixe(cx, cy, _fg, 0);
				}
				mask >>= 1;
			}
		}
		temp += (fz);
	}
}

// print rotated proportional character
// character is already in fontChar
//---------------------------------------------------
static int rotatePropChar(int x, int y, int offset) {
  uint8_t ch = 0;
  double radian = font_rotate * DEG_TO_RAD;
  float cos_radian = cos(radian);
  float sin_radian = sin(radian);

  uint8_t mask = 0x80;
  for (int j=0; j < fontChar.height; j++) {
    for (int i=0; i < fontChar.width; i++) {
      if (((i + (j*fontChar.width)) % 8) == 0) {
        mask = 0x80;
        ch = cfont.font[fontChar.dataPtr++];
      }

      int newX = (int)(x + (((offset + i) * cos_radian) - ((j+fontChar.adjYOffset)*sin_radian)));
      int newY = (int)(y + (((j+fontChar.adjYOffset) * cos_radian) + ((offset + i) * sin_radian)));

      if ((ch & mask) != 0) TFT_drawPixe(newX,newY,_fg, 0);
      else if (!font_transparent) TFT_drawPixe(newX,newY,_bg, 0);

      mask >>= 1;
    }
  }

  return fontChar.xDelta+1;
}

// rotated fixed width character
//--------------------------------------------------------
static void rotateChar(uint8_t c, int x, int y, int pos) {
  uint8_t i,j,ch,fz,mask;
  uint16_t temp;
  int newx,newy;
  double radian = font_rotate*0.0175;
  float cos_radian = cos(radian);
  float sin_radian = sin(radian);
  int zz;

  if( cfont.x_size < 8 ) fz = cfont.x_size;
  else fz = cfont.x_size/8;
  temp=((c-cfont.offset)*((fz)*cfont.y_size))+4;

  for (j=0; j<cfont.y_size; j++) {
    for (zz=0; zz<(fz); zz++) {
      ch = cfont.font[temp+zz];
      mask = 0x80;
      for (i=0; i<8; i++) {
        newx=(int)(x+(((i+(zz*8)+(pos*cfont.x_size))*cos_radian)-((j)*sin_radian)));
        newy=(int)(y+(((j)*cos_radian)+((i+(zz*8)+(pos*cfont.x_size))*sin_radian)));

        if ((ch & mask) != 0) TFT_drawPixe(newx,newy,_fg, 0);
        else if (!font_transparent) TFT_drawPixe(newx,newy,_bg, 0);
        mask >>= 1;
      }
    }
    temp+=(fz);
  }
  // calculate x,y for the next char
  TFT_X = (int)(x + ((pos+1) * cfont.x_size * cos_radian));
  TFT_Y = (int)(y + ((pos+1) * cfont.x_size * sin_radian));
}

//----------------------
static int _7seg_width()
{
	return (2 * (2 * cfont.y_size + 1)) + cfont.x_size;
}

//-----------------------
static int _7seg_height()
{
	return (3 * (2 * cfont.y_size + 1)) + (2 * cfont.x_size);
}

// Returns the string width in pixels.
// Useful for positions strings on the screen.
//===============================
int TFT_getStringWidth(char* str)
{
    int strWidth = 0;

	if (cfont.bitmap == 2) strWidth = ((_7seg_width()+2) * strlen(str)) - 2;	// 7-segment font
	else if (cfont.x_size != 0) strWidth = strlen(str) * cfont.x_size;			// fixed width font
	else {
		// calculate the width of the string of proportional characters
		char* tempStrptr = str;
		while (*tempStrptr != 0) {
			if (getCharPtr(*tempStrptr++)) {
				strWidth += (((fontChar.width > fontChar.xDelta) ? fontChar.width : fontChar.xDelta) + 1);
			}
		}
		strWidth--;
	}
	return strWidth;
}

//===============================================
void TFT_clearStringRect(int x, int y, char *str)
{
	int w = TFT_getStringWidth(str);
	int h = TFT_getfontheight();
	TFT_fillRect(x+dispWin.x1, y+dispWin.y1, w, h, _bg);
}

//==============================================================================
/**
 * bit-encoded bar position of all digits' bcd segments
 *
 *           6
 * 		  +-----+
 * 		3 |  .	| 2
 * 		  +--5--+
 * 		1 |  .	| 0
 * 		  +--.--+
 * 		     4
 */
static const uint16_t font_bcd[] = {
  0x200, // 0010 0000 0000  // -
  0x080, // 0000 1000 0000  // .
  0x06C, // 0100 0110 1100  // / (degree sign)
  0x05f, // 0000 0101 1111, // 0
  0x005, // 0000 0000 0101, // 1
  0x076, // 0000 0111 0110, // 2
  0x075, // 0000 0111 0101, // 3
  0x02d, // 0000 0010 1101, // 4
  0x079, // 0000 0111 1001, // 5
  0x07b, // 0000 0111 1011, // 6
  0x045, // 0000 0100 0101, // 7
  0x07f, // 0000 0111 1111, // 8
  0x07d, // 0000 0111 1101  // 9
  0x900, // 1001 0000 0000  // :
  0x000  // 0000 0000 0000  // ; (space)
};

//-----------------------------------------------------------------------------------------------
static void barVert(int16_t x, int16_t y, int16_t w, int16_t l, color_t color, color_t outline) {
  _fillTriangle(x+1, y+2*w, x+w, y+w+1, x+2*w-1, y+2*w, color);
  _fillTriangle(x+1, y+2*w+l+1, x+w, y+3*w+l, x+2*w-1, y+2*w+l+1, color);
  _fillRect(x, y+2*w+1, 2*w+1, l, color);
  if (cfont.offset) {
    _drawTriangle(x+1, y+2*w, x+w, y+w+1, x+2*w-1, y+2*w, outline);
    _drawTriangle(x+1, y+2*w+l+1, x+w, y+3*w+l, x+2*w-1, y+2*w+l+1, outline);
    _drawRect(x, y+2*w+1, 2*w+1, l, outline);
  }
}

//----------------------------------------------------------------------------------------------
static void barHor(int16_t x, int16_t y, int16_t w, int16_t l, color_t color, color_t outline) {
  _fillTriangle(x+2*w, y+2*w-1, x+w+1, y+w, x+2*w, y+1, color);
  _fillTriangle(x+2*w+l+1, y+2*w-1, x+3*w+l, y+w, x+2*w+l+1, y+1, color);
  _fillRect(x+2*w+1, y, l, 2*w+1, color);
  if (cfont.offset) {
    _drawTriangle(x+2*w, y+2*w-1, x+w+1, y+w, x+2*w, y+1, outline);
    _drawTriangle(x+2*w+l+1, y+2*w-1, x+3*w+l, y+w, x+2*w+l+1, y+1, outline);
    _drawRect(x+2*w+1, y, l, 2*w+1, outline);
  }
}

//--------------------------------------------------------------------------------------------
static void _draw7seg(int16_t x, int16_t y, int8_t num, int16_t w, int16_t l, color_t color) {
  /* TODO: clipping */
  uint8_t nnum = num;
  if (num == 0x20) nnum = 0x3B; // to clear the space character
  if ((nnum < 0x2D) || (nnum > 0x3B)) return;

  int16_t c = font_bcd[nnum - 0x2D];
  int16_t d = 2*w+l+1;

  // === Clear unused segments ===
  if (!(c & 0x001)) barVert(x+d, y+d, w, l, _bg, _bg);
  if (!(c & 0x002)) barVert(x,   y+d, w, l, _bg, _bg);
  if (!(c & 0x004)) barVert(x+d, y, w, l, _bg, _bg);
  if (!(c & 0x008)) barVert(x,   y, w, l, _bg, _bg);
  if (!(c & 0x010)) barHor(x, y+2*d, w, l, _bg, _bg);
  if (!(c & 0x020)) barHor(x, y+d, w, l, _bg, _bg);
  if (!(c & 0x040)) barHor(x, y, w, l, _bg, _bg);

  if (!(c & 0x080)) {
    // low point
    _fillRect(x+(d/2), y+2*d, 2*w+1, 2*w+1, _bg);
    if (cfont.offset) _drawRect(x+(d/2), y+2*d, 2*w+1, 2*w+1, _bg);
  }
  if (!(c & 0x100)) {
    // down middle point
    _fillRect(x+(d/2), y+d+2*w+1, 2*w+1, l/2, _bg);
    if (cfont.offset) _drawRect(x+(d/2), y+d+2*w+1, 2*w+1, l/2, _bg);
  }
  if (!(c & 0x800)) {
	// up middle point
    _fillRect(x+(d/2), y+(2*w)+1+(l/2), 2*w+1, l/2, _bg);
    if (cfont.offset) _drawRect(x+(d/2), y+(2*w)+1+(l/2), 2*w+1, l/2, _bg);
  }
  if (!(c & 0x200)) {
    // middle, minus
    _fillRect(x+2*w+1, y+d, l, 2*w+1, _bg);
    if (cfont.offset) _drawRect(x+2*w+1, y+d, l, 2*w+1, _bg);
  }

  if (c == 0) return;
  // === Draw used segments ===
  if (c & 0x001) barVert(x+d, y+d, w, l, color, cfont.color);	// down right
  if (c & 0x002) barVert(x,   y+d, w, l, color, cfont.color);	// down left
  if (c & 0x004) barVert(x+d, y, w, l, color, cfont.color);		// up right
  if (c & 0x008) barVert(x,   y, w, l, color, cfont.color);		// up left
  if (c & 0x010) barHor(x, y+2*d, w, l, color, cfont.color);	// down
  if (c & 0x020) barHor(x, y+d, w, l, color, cfont.color);		// middle
  if (c & 0x040) barHor(x, y, w, l, color, cfont.color);		// up

  if (c & 0x080) {
    // low point
    _fillRect(x+(d/2), y+2*d, 2*w+1, 2*w+1, color);
    if (cfont.offset) _drawRect(x+(d/2), y+2*d, 2*w+1, 2*w+1, cfont.color);
  }
  if (c & 0x100) {
    // down middle point
    _fillRect(x+(d/2), y+d+2*w+1, 2*w+1, l/2, color);
    if (cfont.offset) _drawRect(x+(d/2), y+d+2*w+1, 2*w+1, l/2, cfont.color);
  }
  if (c & 0x800) {
	// up middle point
    _fillRect(x+(d/2), y+(2*w)+1+(l/2), 2*w+1, l/2, color);
    if (cfont.offset) _drawRect(x+(d/2), y+(2*w)+1+(l/2), 2*w+1, l/2, cfont.color);
  }
  if (c & 0x200) {
    // middle, minus
    _fillRect(x+2*w+1, y+d, l, 2*w+1, color);
    if (cfont.offset) _drawRect(x+2*w+1, y+d, l, 2*w+1, cfont.color);
  }
}
//==============================================================================

//======================================
void TFT_print(char *st, int x, int y) {
	int stl, i, tmpw, tmph, fh;
	uint8_t ch;

	if (cfont.bitmap == 0) return; // wrong font selected

	// ** Rotated strings cannot be aligned
	if ((font_rotate != 0) && ((x <= CENTER) || (y <= CENTER))) return;

	if ((x < LASTX) || (font_rotate == 0)) TFT_OFFSET = 0;

	if ((x >= LASTX) && (x < LASTY)) x = TFT_X + (x-LASTX);
	else if (x > CENTER) x += dispWin.x1;

	if (y >= LASTY) y = TFT_Y + (y-LASTY);
	else if (y > CENTER) y += dispWin.y1;

	// ** Get number of characters in string to print
	stl = strlen(st);

	// ** Calculate CENTER, RIGHT or BOTTOM position
	tmpw = TFT_getStringWidth(st);	// string width in pixels
	fh = cfont.y_size;			// font height
	if ((cfont.x_size != 0) && (cfont.bitmap == 2)) {
		// 7-segment font
		fh = (3 * (2 * cfont.y_size + 1)) + (2 * cfont.x_size);  // 7-seg character height
	}

	if (x == RIGHT) x = dispWin.x2 - tmpw + dispWin.x1;
	else if (x == CENTER) x = (((dispWin.x2 - dispWin.x1 + 1) - tmpw) / 2) + dispWin.x1;

	if (y == BOTTOM) y = dispWin.y2 - fh + dispWin.y1;
	else if (y==CENTER) y = (((dispWin.y2 - dispWin.y1 + 1) - (fh/2)) / 2) + dispWin.y1;

	if (x < dispWin.x1) x = dispWin.x1;
	if (y < dispWin.y1) y = dispWin.y1;
	if ((x > dispWin.x2) || (y > dispWin.y2)) return;

	TFT_X = x;
	TFT_Y = y;

	// ** Adjust y position
	tmph = cfont.y_size; // font height
	// for non-proportional fonts, char width is the same for all chars
	tmpw = cfont.x_size;
	if (cfont.x_size != 0) {
		if (cfont.bitmap == 2) {	// 7-segment font
			tmpw = _7seg_width();	// character width
			tmph = _7seg_height();	// character height
		}
	}
	else TFT_OFFSET = 0;	// fixed font; offset not needed

	if ((TFT_Y + tmph - 1) > dispWin.y2) return;

	int offset = TFT_OFFSET;

	for (i=0; i<stl; i++) {
		ch = st[i]; // get string character

		if (ch == 0x0D) { // === '\r', erase to eol ====
			if ((!font_transparent) && (font_rotate==0)) _fillRect(TFT_X, TFT_Y,  dispWin.x2+1-TFT_X, tmph, _bg);
		}

		else if (ch == 0x0A) { // ==== '\n', new line ====
			if (cfont.bitmap == 1) {
				TFT_Y += tmph + font_line_space;
				if (TFT_Y > (dispWin.y2-tmph)) break;
				TFT_X = dispWin.x1;
			}
		}

		else { // ==== other characters ====
			if (cfont.x_size == 0) {
				// for proportional font get character data to 'fontChar'
				if (getCharPtr(ch)) tmpw = fontChar.xDelta;
				else continue;
			}

			// check if character can be displayed in the current line
			if ((TFT_X+tmpw) > (dispWin.x2)) {
				if (text_wrap == 0) break;
				TFT_Y += tmph + font_line_space;
				if (TFT_Y > (dispWin.y2-tmph)) break;
				TFT_X = dispWin.x1;
			}

			// Let's print the character
			if (cfont.x_size == 0) {
				// == proportional font
				if (font_rotate == 0) TFT_X += printProportionalChar(TFT_X, TFT_Y) + 1;
				else {
					// rotated proportional font
					offset += rotatePropChar(x, y, offset);
					TFT_OFFSET = offset;
				}
			}
			else {
				if (cfont.bitmap == 1) {
					// == fixed font
					if ((ch < cfont.offset) || ((ch-cfont.offset) > cfont.numchars)) ch = cfont.offset;
					if (font_rotate == 0) {
						printChar(ch, TFT_X, TFT_Y);
						TFT_X += tmpw;
					}
					else rotateChar(ch, x, y, i);
				}
				else if (cfont.bitmap == 2) {
					// == 7-segment font ==
					_draw7seg(TFT_X, TFT_Y, ch, cfont.y_size, cfont.x_size, _fg);
					TFT_X += (tmpw + 2);
				}
			}
		}
	}
}


// ================ Service functions ==========================================

// Change the screen rotation.
// Input: m new rotation value (0 to 3)
//===============================
void TFT_setRotation(uint8_t rot)
{
    if (rot > 3) {
        uint8_t madctl = (rot & 0xF8); // for testing, manually set MADCTL register
        disp_spi_transfer_cmd_data(MEMORY_ACCESS_CTL, &madctl, 1);
    }
    else {
        orientation = rot;
        _tft_setRotation(rot);
    }

	dispWin.x1 = 0;
	dispWin.y1 = 0;
	dispWin.x2 = _width-1;
	dispWin.y2 = _height-1;

	TFT_fillScreen(_bg);
}

// Send the command to invert all of the colors.
// Input: i 0 to disable inversion; non-zero to enable inversion
//========================================
void TFT_invertDisplay(const uint8_t mode)
{
    if ( mode == INVERT_ON ) disp_spi_transfer_cmd(INVERSION_DISPALY_ON);
    else disp_spi_transfer_cmd(INVERSION_DISPALY_OFF);
}

// Select gamma curve
// Input: gamma = 0~3
//================================
void TFT_setGammaCurve(uint8_t gm)
{
    uint8_t gamma_curve = 1 << (gm & 0x03);
    disp_spi_transfer_cmd_data(GAMMA_SET, &gamma_curve, 1);
}

//===========================================================
color_t HSBtoRGB(float _hue, float _sat, float _brightness) {
 float red = 0.0;
 float green = 0.0;
 float blue = 0.0;

 if (_sat == 0.0) {
   red = _brightness;
   green = _brightness;
   blue = _brightness;
 } else {
   if (_hue == 360.0) {
     _hue = 0;
   }

   int slice = (int)(_hue / 60.0);
   float hue_frac = (_hue / 60.0) - slice;

   float aa = _brightness * (1.0 - _sat);
   float bb = _brightness * (1.0 - _sat * hue_frac);
   float cc = _brightness * (1.0 - _sat * (1.0 - hue_frac));

   switch(slice) {
     case 0:
         red = _brightness;
         green = cc;
         blue = aa;
         break;
     case 1:
         red = bb;
         green = _brightness;
         blue = aa;
         break;
     case 2:
         red = aa;
         green = _brightness;
         blue = cc;
         break;
     case 3:
         red = aa;
         green = bb;
         blue = _brightness;
         break;
     case 4:
         red = cc;
         green = aa;
         blue = _brightness;
         break;
     case 5:
         red = _brightness;
         green = aa;
         blue = bb;
         break;
     default:
         red = 0.0;
         green = 0.0;
         blue = 0.0;
         break;
   }
 }

 color_t color;
 color = (uint16_t)(((uint8_t)(blue * 255.0)) & 0xF8) << 8;
 color |= (uint16_t)(((uint8_t)(green * 255.0)) & 0xFC) << 3;
 color |= (uint16_t)(((uint8_t)(red * 255.0)) & 0xF8) >> 3;

 return color;
}
//=====================================================================
void TFT_setclipwin(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
	dispWin.x1 = x1;
	dispWin.y1 = y1;
	dispWin.x2 = x2;
	dispWin.y2 = y2;

	if (dispWin.x2 >= _width) dispWin.x2 = _width-1;
	if (dispWin.y2 >= _height) dispWin.y2 = _height-1;
	if (dispWin.x1 > dispWin.x2) dispWin.x1 = dispWin.x2;
	if (dispWin.y1 > dispWin.y2) dispWin.y1 = dispWin.y2;
}

//=====================
void TFT_resetclipwin()
{
	dispWin.x2 = _width-1;
	dispWin.y2 = _height-1;
	dispWin.x1 = 0;
	dispWin.y1 = 0;
}

//==========================================================================
void set_7seg_font_atrib(uint8_t l, uint8_t w, int outline, color_t color) {
	if (cfont.bitmap != 2) return;

	if (l < 6) l = 6;
    if (l > 40) l = 40;
    if (w < 1) w = 1;
    if (w > (l/2)) w = l/2;
    if (w > 12) w = 12;

    cfont.x_size = l;
	cfont.y_size = w;
	cfont.offset = outline;
	cfont.color  = color;
}

//==========================================
int TFT_getfontsize(int *width, int* height)
{
  if (cfont.bitmap == 1) {
    if (cfont.x_size != 0) *width = cfont.x_size;	// fixed width font
    else *width = cfont.max_x_size;					// proportional font
    *height = cfont.y_size;
  }
  else if (cfont.bitmap == 2) {
	// 7-segment font
    *width = _7seg_width();
    *height = _7seg_height();
  }
  else {
    *width = 0;
    *height = 0;
    return 0;
  }
  return 1;
}

//=====================
int TFT_getfontheight()
{
  if (cfont.bitmap == 1) return cfont.y_size;			// Bitmap font
  else if (cfont.bitmap == 2) return _7seg_height();	// 7-segment font
  return 0;
}

//====================
void TFT_saveClipWin()
{
	dispWinTemp.x1 = dispWin.x1;
	dispWinTemp.y1 = dispWin.y1;
	dispWinTemp.x2 = dispWin.x2;
	dispWinTemp.y2 = dispWin.y2;
}

//=======================
void TFT_restoreClipWin()
{
	dispWin.x1 = dispWinTemp.x1;
	dispWin.y1 = dispWinTemp.y1;
	dispWin.x2 = dispWinTemp.x2;
	dispWin.y2 = dispWinTemp.y2;
}


// ================ JPG SUPPORT ================================================
// User defined device identifier
typedef struct {
	mp_obj_t	fhndl;			// File object for input function
    int			x;				// image top left point X position
    int			y;				// image top left point Y position
    uint8_t		*membuff;		// memory buffer containing the image
    uint32_t	bufsize;		// size of the memory buffer
    uint32_t	bufptr;			// memory buffer current position
    uint32_t    spi_time;
    color_t		*linbuf;		// memory buffer used for display output
} JPGIODEV;


// User defined call-back function to input JPEG data from file
//---------------------
static UINT tjd_input (
	JDEC* jd,		// Decompression object
	BYTE* buff,		// Pointer to the read buffer (NULL:skip)
	UINT nd			// Number of bytes to read/skip from input stream
)
{
	int rb = 0;
	// Device identifier for the session (5th argument of jd_prepare function)
	JPGIODEV *dev = (JPGIODEV*)jd->device;

	if (buff) {	// Read nd bytes from the input strem
		rb = mp_stream_posix_read((void *)dev->fhndl, buff, nd);
		return rb;	// Returns actual number of bytes read
	}
	else {	// Remove nd bytes from the input stream
        if (mp_stream_posix_lseek((void *)dev->fhndl, nd, SEEK_CUR) >= 0) return nd;
		else return 0;
	}
}

// User defined call-back function to input JPEG data from memory buffer
//-------------------------
static UINT tjd_buf_input (
	JDEC* jd,		// Decompression object
	BYTE* buff,		// Pointer to the read buffer (NULL:skip)
	UINT nd			// Number of bytes to read/skip from input stream
)
{
	// Device identifier for the session (5th argument of jd_prepare function)
	JPGIODEV *dev = (JPGIODEV*)jd->device;
	if (!dev->membuff) return 0;
	if (dev->bufptr >= (dev->bufsize + 2)) return 0; // end of stream

	if ((dev->bufptr + nd) > (dev->bufsize + 2)) nd = (dev->bufsize + 2) - dev->bufptr;

	if (buff) {	// Read nd bytes from the input strem
		memcpy(buff, dev->membuff + dev->bufptr, nd);
		dev->bufptr += nd;
		return nd;	// Returns number of bytes read
	}
	else {	// Remove nd bytes from the input stream
		dev->bufptr += nd;
		return nd;
	}
}

// User defined call-back function to output RGB bitmap to display device
//----------------------
static UINT tjd_output (
	JDEC* jd,		// Decompression object of current session
	void* bitmap,	// Bitmap data to be output
	JRECT* rect		// Rectangular region to output
)
{
	// Device identifier for the session (5th argument of jd_prepare function)
	JPGIODEV *dev = (JPGIODEV*)jd->device;

	// ** Put the rectangular into the display device **
	int x;
	int y;
	int dleft, dtop, dright, dbottom;
	BYTE *src = (BYTE*)bitmap;
    uint16_t _color;

	int left = rect->left + dev->x;
	int top = rect->top + dev->y;
	int right = rect->right + dev->x;
	int bottom = rect->bottom + dev->y;
	int fbpos;

	if ((left > dispWin.x2) || (top > dispWin.y2)) return 1;	// out of screen area, return
	if ((right < dispWin.x1) || (bottom < dispWin.y1)) return 1;// out of screen area, return

	if (left < dispWin.x1) dleft = dispWin.x1;
	else dleft = left;
	if (top < dispWin.y1) dtop = dispWin.y1;
	else dtop = top;
	if (right > dispWin.x2) dright = dispWin.x2;
	else dright = right;
	if (bottom > dispWin.y2) dbottom = dispWin.y2;
	else dbottom = bottom;

	if ((dleft > dispWin.x2) || (dtop > dispWin.y2)) return 1;		// out of screen area, return
	if ((dright < dispWin.x1) || (dbottom < dispWin.y1)) return 1;	// out of screen area, return

	uint32_t len = ((dright-dleft+1) * (dbottom-dtop+1));	// calculate length of data

	if ((len > 0) && (len <= JPG_IMAGE_LINE_BUF_SIZE)) {
        int lbidx = 0;
        for (y = top; y <= bottom; y++) {
            for (x = left; x <= right; x++) {
                // Clip to display area
                if ((x >= dleft) && (y >= dtop) && (x <= dright) && (y <= dbottom)) {
                    _color = (uint16_t)((*src++) & 0xF8) << 8;  // R
                    _color |= (uint16_t)((*src++) & 0xFC) << 3; // G
                    _color |= (uint16_t)((*src++) & 0xF8) >> 3; // B
                    if (use_frame_buffer) {
                        fbpos = y*_width + x;
                        tft_frame_buffer[fbpos] = _color;
                    }
                    else {
                        dev->linbuf[lbidx] = _color;
                        lbidx++;
                    }
                }
                else src += 3; // skip
            }
        }
        if (!use_frame_buffer) {
            uint64_t spi_startt = mp_hal_ticks_us();
            send_data(dleft, dtop, dright+1, dbottom+1, len, dev->linbuf);
            dev->spi_time += (uint32_t)(mp_hal_ticks_us() - spi_startt);
        }
	}
	else {
		mp_printf(&mp_plat_print, "Data size error: %d jpg: (%d,%d,%d,%d) disp: (%d,%d,%d,%d)\r\n", len, left,top,right,bottom, dleft,dtop,dright,dbottom);
		return 0;  // stop decompression
	}

	return 1;	// Continue to decompression
}

// tft.jpgimage(X, Y, scale, file_name, buf, size]
// X & Y can be < 0 !
//=====================================================================================
void TFT_jpg_image(int x, int y, uint8_t scale, mp_obj_t fname, uint8_t *buf, int size)
{
	JPGIODEV dev;
	char *work = NULL;		// Pointer to the working buffer (must be 4-byte aligned)
	UINT sz_work = 4096;	// Size of the working buffer (must be power of 2)
	JDEC jd;				// Decompression object (70 bytes)
	JRESULT rc;

	dev.linbuf = NULL;
	dev.spi_time = 0;

   	dev.fhndl = mp_const_none;
    if (fname == mp_const_none) {
    	// image from buffer
        dev.membuff = buf;
        dev.bufsize = size;
        dev.bufptr = 0;
    }
    else {
        const char *file_name = mp_obj_str_get_str(fname);
        if (image_debug) mp_printf(&mp_plat_print, "Opening file: %s\n", file_name);
    	// image from file
        dev.membuff = NULL;
        dev.bufsize = 0;
        dev.bufptr = 0;
        mp_obj_t args[2];
        args[0] = fname;
        args[1] = mp_obj_new_str("rb", 2);

        // Open the file
        mp_obj_t ffd = mp_vfs_open(2, args, (mp_map_t*)&mp_const_empty_map);
        if (!ffd) {
            if (image_debug) mp_printf(&mp_plat_print, "Error opening file: %s\r\n", strerror(errno));
            goto exit;
        }

       if (image_debug) mp_printf(&mp_plat_print, "File opened\n");
       dev.fhndl = ffd;
    }

    if (scale > 3) scale = 3;

	work = malloc(sz_work);
	if (work) {
        if (image_debug) mp_printf(&mp_plat_print, "Preparing JPG\n");
		if (dev.membuff) rc = jd_prepare(&jd, tjd_buf_input, (void *)work, sz_work, &dev);
		else rc = jd_prepare(&jd, tjd_input, (void *)work, sz_work, &dev);
		if (rc == JDR_OK) {
	        if (image_debug) mp_printf(&mp_plat_print, "Prepared.\n");
			if (x == CENTER) x = ((dispWin.x2 - dispWin.x1 + 1 - (int)(jd.width >> scale)) / 2) + dispWin.x1;
			else if (x == RIGHT) x = dispWin.x2 + 1 - (int)(jd.width >> scale);

			if (y == CENTER) y = ((dispWin.y2 - dispWin.y1 + 1 - (int)(jd.height >> scale)) / 2) + dispWin.y1;
			else if (y == BOTTOM) y = dispWin.y2 + 1 - (int)(jd.height >> scale);

			if (x < ((dispWin.x2-1) * -1)) x = (dispWin.x2-1) * -1;
			if (y < ((dispWin.y2-1)) * -1) y = (dispWin.y2-1) * -1;
			if (x > (dispWin.x2-1)) x = dispWin.x2 - 1;
			if (y > (dispWin.y2-1)) y = dispWin.y2-1;

			dev.x = x;
			dev.y = y;

            if (!use_frame_buffer) {
                dev.linbuf = malloc(JPG_IMAGE_LINE_BUF_SIZE*2);
                if (dev.linbuf == NULL) {
                    if (image_debug) mp_printf(&mp_plat_print, "Error allocating line buffer\r\n");
                    goto exit;
                }
            }

			// Start to decode the JPEG file
			rc = jd_decomp(&jd, tjd_output, scale);

			if (rc != JDR_OK) {
				if (image_debug) mp_printf(&mp_plat_print, "jpg decompression error %d\r\n", rc);
			}
			if (image_debug) mp_printf(&mp_plat_print, "Jpg size: %dx%d, position; %d,%d, scale: %d, bytes used: %d, spitime=%u\r\n",
			        jd.width, jd.height, x, y, scale, jd.sz_pool, dev.spi_time);
		}
		else {
			if (image_debug) mp_printf(&mp_plat_print, "jpg prepare error %d\r\n", rc);
		}
	}
	else {
		if (image_debug) mp_printf(&mp_plat_print, "work buffer allocation error\r\n");
	}

exit:
	if (work) free(work);  // free work buffer
	if (dev.linbuf) free(dev.linbuf);
    if (dev.fhndl) mp_stream_close(dev.fhndl);  // close input file
}


//=======================================================================================
int TFT_bmp_image(int x, int y, uint8_t scale, mp_obj_t fname, uint8_t *imgbuf, int size)
{
    mp_obj_t fhndl = mp_const_none;
	int i, err=0;
	int img_xsize, img_ysize, img_xstart, img_xlen, img_ystart, img_ylen;
	int img_pos, img_pix_pos, scan_lines, rd_len;
	uint16_t wtemp;
	uint32_t temp;
	int disp_xstart, disp_xend, disp_ystart, disp_yend;
	uint8_t buf[56];
	char err_buf[64];
	uint8_t *line_buf = NULL;
	uint8_t *scale_buf = NULL;
	uint8_t scale_pix;
	uint16_t co[3] = {0,0,0};			// RGB sum
	uint8_t npix;
	uint16_t _color;
	int idx16;

	if (scale > 7) scale = 7;
	scale_pix = scale+1;	// scale factor ( 1~8 )

    if (fname) {
    	// * File name is given, reading image from file
        mp_obj_t args[2];
        args[0] = fname;
        args[1] = mp_obj_new_str("rb", 2);

        // Open the file
        mp_obj_t ffd = mp_vfs_open(2, args, (mp_map_t*)&mp_const_empty_map);
        if (!ffd) {
            sprintf(err_buf, "opening file");
            goto exit;
        }

        // Get file size
        int fsize = mp_stream_posix_lseek((void *)ffd, 0, SEEK_END);
        int at_start = mp_stream_posix_lseek((void *)ffd, 0, SEEK_SET);
        if ((fsize <= 0) || (at_start != 0)) {
            sprintf(err_buf, "getting file size");
        }
        size = fsize;
        fhndl = ffd;
        i = mp_stream_posix_read((void *)fhndl, buf, 54); // read header
    }
    else {
    	// * Reading image from buffer
    	if ((imgbuf) && (size > 54)) {
    		memcpy(buf, imgbuf, 54);
    		i = 54;
    	}
    	else i = 0;
    }

    sprintf(err_buf, "reading header");
	if (i != 54) {err = -3;	goto exit;}

	// ** Check image header and get image properties
	if ((buf[0] != 'B') || (buf[1] != 'M')) {err=-4; goto exit;} // accept only images with 'BM' id

	memcpy(&temp, buf+2, 4);				// file size
	if (temp != size) {
        printf("file size from file = %u\n", temp);
	    err=-5;
	    goto exit;
	}

	memcpy(&img_pos, buf+10, 4);			// start of pixel data

	memcpy(&temp, buf+14, 4);				// BMP header size
	if (temp != 40) {err=-6; goto exit;}

	memcpy(&wtemp, buf+26, 2);				// the number of color planes
	if (wtemp != 1) {err=-7; goto exit;}

	memcpy(&wtemp, buf+28, 2);				// the number of bits per pixel
	if (wtemp != 24) {err=-8; goto exit;}

	memcpy(&temp, buf+30, 4);				// the compression method being used
	if (temp != 0) {err=-9; goto exit;}

	memcpy(&img_xsize, buf+18, 4);			// the bitmap width in pixels
	memcpy(&img_ysize, buf+22, 4);			// the bitmap height in pixels


	// * scale image dimensions

	img_xlen = img_xsize / scale_pix;		// image display horizontal size
	img_ylen = img_ysize / scale_pix;		// image display vertical size

	if (x == CENTER) x = ((dispWin.x2 - dispWin.x1 + 1 - img_xlen) / 2) + dispWin.x1;
	else if (x == RIGHT) x = dispWin.x2 + 1 - img_xlen;

	if (y == CENTER) y = ((dispWin.y2 - dispWin.y1 + 1 - img_ylen) / 2) + dispWin.y1;
	else if (y == BOTTOM) y = dispWin.y2 + 1 - img_ylen;

	if ((x < ((dispWin.x2 + 1) * -1)) || (x > (dispWin.x2 + 1)) || (y < ((dispWin.y2 + 1) * -1)) || (y > (dispWin.y2 + 1))) {
		sprintf(err_buf, "out of display area (%d,%d", x, y);
		err = -10;
		goto exit;
	}

	// ** set display and image areas
	if (x < dispWin.x1) {
		disp_xstart = dispWin.x1;
		img_xstart = -x;	// image pixel line X offset
		img_xlen += x;
	}
	else {
		disp_xstart = x;
		img_xstart = 0;
	}
	if (y < dispWin.y1) {
		disp_ystart = dispWin.y1;
		img_ystart = -y;	// image pixel line Y offset
		img_ylen += y;
	}
	else {
		disp_ystart = y;
		img_ystart = 0;
	}
	disp_xend = disp_xstart + img_xlen - 1;
	disp_yend = disp_ystart + img_ylen - 1;
	if (disp_xend > dispWin.x2) {
		disp_xend = dispWin.x2;
		img_xlen = disp_xend - disp_xstart + 1;
	}
	if (disp_yend > dispWin.y2) {
		disp_yend = dispWin.y2;
		img_ylen = disp_yend - disp_ystart + 1;
	}

	if ((img_xlen < 8) || (img_ylen < 8) || (img_xstart >= (img_xsize-2)) || ((img_ysize - img_ystart) < 2)) {
		sprintf(err_buf, "image too small");
		err = -11;
		goto exit;
	}

	// ** Allocate memory for 2 lines of image pixels
	line_buf = malloc(img_xsize*3);
	if (line_buf == NULL) {
	    sprintf(err_buf, "allocating line buffer");
		err=-12;
		goto exit;
	}

	if (scale) {
		// Allocate memory for scale buffer
		rd_len = img_xlen * 3 * scale_pix;
		scale_buf = malloc(rd_len*scale_pix);
		if (scale_buf == NULL) {
			sprintf(err_buf, "allocating scale buffer");
			err=-14;
			goto exit;
		}
	}
	else rd_len = img_xlen * 3;

	// ** ***************************************************** **
	// ** BMP images are stored in file from LAST to FIRST line **
	// ** ***************************************************** **

	/* Used variables:
		img_xsize		horizontal image size in pixels
		img_ysize		number of image lines
		img_xlen 		image display horizontal scaled size in pixels
		img_ylen		image display vertical scaled size in pixels
		img_xstart		first pixel in line to be displayed
		img_ystart		first image line to be displayed
		img_xlen		number of pixels in image line to be displayed, starting with 'img_xstart'
		img_ylen		number of lines in image to be displayed, starting with 'img_ystart'
		rd_len			length of color data which are read from image line in bytes
	 */

	// Set position in image to the first color data (beginning of the LAST line)
	img_pos += (img_ystart * (img_xsize*3));
	if (fhndl != mp_const_none) {
        if (mp_stream_posix_lseek((void *)fhndl, img_pos, SEEK_CUR) < 0) {
			sprintf(err_buf, "file seek at %d", img_pos);
			err = -15;
			goto exit;
		}
	}

	if (image_debug) mp_printf(&mp_plat_print, "BMP: image size: (%d,%d) scale: %d disp size: (%d,%d) img xofs: %d img yofs: %d at: %d,%d; line buf: 2* %d scale buf: %d\r\n",
			img_xsize, img_ysize, scale_pix, img_xlen, img_ylen, img_xstart, img_ystart, disp_xstart, disp_ystart, img_xsize*2, ((scale) ? (rd_len*scale_pix) : 0));

	while ((disp_yend >= disp_ystart) && ((img_pos + (img_xsize*3)) <= size)) {
		if (img_pos > size) {
			sprintf(err_buf, "EOF reached: %d > %d", img_pos, size);
			err = -16;
			goto exit;
		}
		if (scale == 0) {
			// Read the line of color data into color buffer
			if (fhndl != mp_const_none) {
		        i = mp_stream_posix_read((void *)fhndl, line_buf, img_xsize*3); // read line of BGR pixels from file
				if (i != (img_xsize*3)) {
					sprintf(err_buf, "file read at %d (%d<>%d)", img_pos, i, img_xsize*3);
					err = -16;
					goto exit;
				}
			}
			else memcpy(line_buf, imgbuf+img_pos, img_xsize*3);

			if (img_xstart > 0)	memmove(line_buf, line_buf+(img_xstart*3), rd_len);
			// Convert colors BGR-888 (BMP) -> 565color (DISPLAY) ===
			idx16 = 0;
			for (i=0; i < rd_len; i += 3) {
                _color = (uint16_t)(line_buf[i+2] & 0xF8) << 8;  // R
                _color |= (uint16_t)(line_buf[i+1] & 0xFC) << 3; // G
                _color |= (uint16_t)(line_buf[i] & 0xF8) >> 3; // B
                line_buf[idx16] = _color >> 8;
                idx16++;
                line_buf[idx16+1] = _color & 0xFF;
                idx16++;
			}
			img_pos += (img_xsize*3);
		}
		else {
			// scale image, read 'scale_pix' lines and find the average color
			for (scan_lines=0; scan_lines<scale_pix; scan_lines++) {
				if (img_pos > size) break;
				if (fhndl) {
	                i = mp_stream_posix_read((void *)fhndl, line_buf, img_xsize*3); // read line from file
					if (i != (img_xsize*3)) {
						sprintf(err_buf, "file read at %d (%d<>%d)", img_pos, i, img_xsize*3);
						err = -17;
						goto exit;
					}
				}
				else memcpy(line_buf, imgbuf+img_pos, img_xsize*3);
				img_pos += (img_xsize*3);

				// copy only data which are displayed to scale buffer
				memcpy(scale_buf + (rd_len * scan_lines), line_buf+img_xstart, rd_len);
			}

			// Populate display line buffer
			idx16 = 0;
			for (int n=0;n<(img_xlen*3);n += 3) {
				memset(co, 0, sizeof(co));	// initialize color sum
				npix = 0;					// initialize number of pixels in scale rectangle

				// sum all pixels in scale rectangle
				for (int sc_line=0; sc_line<scan_lines; sc_line++) {
					// Get colors position in scale buffer
					img_pix_pos = (rd_len * sc_line) + (n * scale_pix);

					for (int sc_col=0; sc_col<scale_pix; sc_col++) {
						co[0] += scale_buf[img_pix_pos];
						co[1] += scale_buf[img_pix_pos + 1];
						co[2] += scale_buf[img_pix_pos + 2];
						npix++;
					}
				}
				// Place the average in display buffer, convert BGR-888 (BMP) -> 565color (DISPLAY)
                _color = (uint16_t)((uint8_t)(co[0] / npix) & 0xF8) >> 3;   // R
                _color |= (uint16_t)((uint8_t)(co[1] / npix) & 0xFC) << 3;  // G
                _color |= (uint16_t)((uint8_t)(co[2] / npix) & 0xF8) << 8;  // B
                line_buf[idx16] = _color >> 8;
                line_buf[idx16+1] = _color & 0xFF;
                idx16 += 2;
			}
		}

		send_data(disp_xstart, disp_yend, disp_xend, disp_yend, img_xlen, (color_t *)line_buf);

		disp_yend--;
	}
	err = 0;
exit:
	if (scale_buf) free(scale_buf);
	if (line_buf) free(line_buf);
    if (fhndl != mp_const_none) mp_stream_close(fhndl);  // close input file
	if ((err) && (image_debug)) mp_printf(&mp_plat_print, "Error: %d [%s]\r\n", err, err_buf);

	return err;
}

#endif // MICROPY_USE_DISPLAY
