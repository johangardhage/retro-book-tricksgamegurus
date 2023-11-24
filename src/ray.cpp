//
// RAY.CPP
//
#include "lib/retro.h"
#include "lib/retromain.h"
#include "lib/retrogfx.h"
#include "lib/retrofont.h"
#include "graphics.h"

// D E F I N E S /////////////////////////////////////////////////////////////

#define OVERBOARD          48 // the absolute closest a player can get to a wall

#define INTERSECTION_FOUND 1

// constants used to represent angles
#define ANGLE_0     0
#define ANGLE_1     5
#define ANGLE_2     10
#define ANGLE_4     20
#define ANGLE_5     25
#define ANGLE_6     30
#define ANGLE_15    80
#define ANGLE_30    160
#define ANGLE_45    240
#define ANGLE_60    320
#define ANGLE_90    480
#define ANGLE_135   720
#define ANGLE_180   960
#define ANGLE_225   1200
#define ANGLE_270   1440
#define ANGLE_315   1680
#define ANGLE_360   1920

#define WORLD_ROWS    16        // number of rows in the game world
#define WORLD_COLUMNS 16        // number of columns in the game world
#define CELL_X_SIZE   64        // size of a cell in the gamw world
#define CELL_Y_SIZE   64

// size of overall game world
#define WORLD_X_SIZE  (WORLD_COLUMNS * CELL_X_SIZE)
#define WORLD_Y_SIZE  (WORLD_ROWS    * CELL_Y_SIZE)

// G L O B A L S /////////////////////////////////////////////////////////////

// world map of nxn cells, each cell is 64x64 pixels
unsigned char world[WORLD_ROWS][WORLD_COLUMNS + 1];       // pointer to matrix of cells that make up world

float tan_table[ANGLE_360 + 1];              // tangent tables used to compute initial
float inv_tan_table[ANGLE_360 + 1];          // intersections with ray

float y_step[ANGLE_360 + 1];                 // x and y steps, used to find intersections
float x_step[ANGLE_360 + 1];                 // after initial one is found

float cos_table[ANGLE_360 + 1];              // used to cacell out fishbowl effect

float inv_cos_table[ANGLE_360 + 1];          // used to compute distances by calculating
float inv_sin_table[ANGLE_360 + 1];          // the hypontenuse

// F U N C T I O N S /////////////////////////////////////////////////////////

void Build_Tables(void)
{
	// create tables, sit back for a sec!
	for (int ang = ANGLE_0; ang <= ANGLE_360; ang++) {
		float rad_angle = (3.272e-4) + ang * 2 * 3.141592654 / ANGLE_360;
		tan_table[ang] = tan(rad_angle);
		inv_tan_table[ang] = 1 / tan_table[ang];

		// tangent has the incorrect signs in all quadrants except 1, so
		// manually fix the signs of each quadrant since the tangent is
		// equivalent to the slope of a line and if the tangent is wrong
		// then the ray that is case will be wrong
		if (ang >= ANGLE_0 && ang < ANGLE_180) {
			y_step[ang] = fabs(tan_table[ang] * CELL_Y_SIZE);
		} else
			y_step[ang] = -fabs(tan_table[ang] * CELL_Y_SIZE);

		if (ang >= ANGLE_90 && ang < ANGLE_270) {
			x_step[ang] = -fabs(inv_tan_table[ang] * CELL_X_SIZE);
		} else {
			x_step[ang] = fabs(inv_tan_table[ang] * CELL_X_SIZE);
		}

		// create the sin and cosine tables to copute distances
		inv_cos_table[ang] = 1 / cos(rad_angle);
		inv_sin_table[ang] = 1 / sin(rad_angle);
	}

	// create view filter table.  There is a cosine wave modulated on top of
	// the view distance as a side effect of casting from a fixed point.
	// to cancell this effect out, we multiple by the inverse of the cosine
	// and the result is the proper scale.  Without this we would see a
	// fishbowl effect, which might be desired in some cases?
	for (int ang = -ANGLE_30; ang <= ANGLE_30; ang++) {
		float rad_angle = (3.272e-4) + ang * 2 * 3.141592654 / ANGLE_360;
		cos_table[ang + ANGLE_30] = 1 / cos(rad_angle);
	}
}

/////////////////////////////////////////////////////////////////////////////

void Load_World(const char *file)
{
	// this function opens the input file and loads the world data from it

	FILE *fp;
	char ch;

	// open the file
	if (!(fp = fopen(file, "r"))) {
		return;
	}

	// load in the data
	for (int row = 0; row < WORLD_ROWS; row++) {
		// load in the next row
		for (int column = 0; column < WORLD_COLUMNS; column++) {
			while ((ch = getc(fp)) == 10) {} // filter out CR

			// translate character to integer
			if (ch == ' ')
				ch = 0;
			else
				ch = ch - '0';

			// insert data into world
			world[row][column] = ch;
		}
	}

	// print map
	for (int row = 0; row < WORLD_ROWS; row++) {
		for (int column = 0; column < WORLD_COLUMNS; column++) {
			if (world[row][column] == 1) {
				printf("1");
			} else {
				printf("0");
			}
		}
		printf("\n");
	}

	// close the file
	fclose(fp);
}

/////////////////////////////////////////////////////////////////////////////

void sline(long x1, long y1, long x2, long y2, int color)
{
	// used a a diagnostic function to draw a scaled line

	x1 = x1 / 4;
	y1 = 256 - (y1 / 4);

	x2 = x2 / 4;
	y2 = 256 - (y2 / 4);

	RETRO_DrawLine(x1, y1, x2, y2, color);
}

/////////////////////////////////////////////////////////////////////////////

void splot(long x, long y, int color)
{
	// used as a diagnostic function to draw a scaled point

	x = x / 4;
	y = 256 - (y / 4);

	RETRO_PutPixel(x, y, color);
	RETRO_PutPixel(x + 1, y, color);
	RETRO_PutPixel(x, y + 1, color);
	RETRO_PutPixel(x + 1, y + 1, color);
}

/////////////////////////////////////////////////////////////////////////////

void Draw_2D_Map(void)
{
	// draw 2-D map of world

	int row, column, block;

	for (row = 0; row < WORLD_ROWS; row++) {
		for (column = 0; column < WORLD_COLUMNS; column++) {
			block = world[row][column];

			// test if there is a solid block there
			if (block == 0) {
				RETRO_DrawRectangle(column * CELL_X_SIZE / 4,
									row * CELL_Y_SIZE / 4,
									column * CELL_X_SIZE / 4 + CELL_X_SIZE / 4 - 1,
									row * CELL_Y_SIZE / 4 + CELL_Y_SIZE / 4 - 1,
									15);
			} else {
				RETRO_DrawFilledRectangle(column * CELL_X_SIZE / 4,
										row * CELL_Y_SIZE / 4,
										column * CELL_X_SIZE / 4 + CELL_X_SIZE / 4,
										row * CELL_Y_SIZE / 4 + CELL_Y_SIZE / 4,
										2);
			}
		}
	}
}

/////////////////////////////////////////////////////////////////////////////

void Ray_Caster(long x, long y, long view_angle)
{
	// This function casts out 320 rays from the viewer and builds up the video
	// display based on the intersections with the walls. The 320 rays are
	// cast in such a way that they all fit into a 60 degree field of view
	// a ray is cast and then the distance to the first horizontal and vertical
	// edge that has a cell in it is recorded.  The intersection that has the
	// closer distance to the user is the one that is used to draw the bitmap.
	// the distance is used to compute the height of the "sliver" of texture
	// or line that will be drawn on the screen

	// note: this function uses floating point (slow), no optimizations (slower)
	// and finally it makes calls to Microsofts Graphics libraries (slowest!)
	// however, writing it in this manner makes it many orders of magnitude
	// easier to understand.

	int rcolor;

	long xray = 0,        // tracks the progress of a ray looking for Y interesctions
		yray = 0,        // tracks the progress of a ray looking for X interesctions
		next_y_cell,   // used to figure out the quadrant of the ray
		next_x_cell,
		cell_x,        // the current cell that the ray is in
		cell_y,
		x_bound,       // the next vertical and horizontal intersection point
		y_bound,
		xb_save,       // storage to record intersections cell boundaries
		yb_save,
		x_delta,       // the amount needed to move to get to the next cell
		y_delta,       // position
		ray,           // the current ray being cast 0-320
		casting = 2,     // tracks the progress of the X and Y component of the ray
		x_hit_type,    // records the block that was intersected, used to figure
		y_hit_type,    // out which texture to use
		top,           // used to compute the top and bottom of the sliver that
		bottom;        // is drawn symetrically around the bisecting plane of the screens vertical extents

	float xi,           // used to track the x and y intersections
		yi,
		xi_save,      // used to save exact x and y intersection points
		yi_save,
		dist_x,       // the distance of the x and y ray intersections from
		dist_y,       // the viewpoint
		scale;        // the final scale to draw the "sliver" in

	// S E C T I O N  1 /////////////////////////////////////////////////////////v

	// initialization

	// compute starting angle from player.  Field of view is 60 degrees, so
	// subtract half of that current view angle

	if ((view_angle -= ANGLE_30) < 0) {
		// wrap angle around

		view_angle = ANGLE_360 + view_angle;
	} // end if

	rcolor = 1;

	// loop through all 320 rays

	// section 2

	for (ray = 0; ray < 320; ray++) {
		// S E C T I O N  2 /////////////////////////////////////////////////////////

		// compute first x intersection

		// need to know which half plane we are casting from relative to Y axis

		if (view_angle >= ANGLE_0 && view_angle < ANGLE_180) {

			// compute first horizontal line that could be intersected with ray
			// note: it will be above player

			y_bound = CELL_Y_SIZE + CELL_Y_SIZE * (y / CELL_Y_SIZE);

			// compute delta to get to next horizontal line

			y_delta = CELL_Y_SIZE;

			// based on first possible horizontal intersection line, compute X
			// intercept, so that casting can begin

			xi = inv_tan_table[view_angle] * (y_bound - y) + x;

			// set cell delta

			next_y_cell = 0;

		} // end if upper half plane
		else {

			// compute first horizontal line that could be intersected with ray
			// note: it will be below player

			y_bound = CELL_Y_SIZE * (y / CELL_Y_SIZE);

			// compute delta to get to next horizontal line

			y_delta = -CELL_Y_SIZE;

			// based on first possible horizontal intersection line, compute X
			// intercept, so that casting can begin

			xi = inv_tan_table[view_angle] * (y_bound - y) + x;

			// set cell delta

			next_y_cell = -1;

		} // end else lower half plane

		// S E C T I O N  3 /////////////////////////////////////////////////////////

		// compute first y intersection

		// need to know which half plane we are casting from relative to X axis

		if (view_angle < ANGLE_90 || view_angle >= ANGLE_270) {

			// compute first vertical line that could be intersected with ray
			// note: it will be to the right of player

			x_bound = CELL_X_SIZE + CELL_X_SIZE * (x / CELL_X_SIZE);

			// compute delta to get to next vertical line

			x_delta = CELL_X_SIZE;

			// based on first possible vertical intersection line, compute Y
			// intercept, so that casting can begin

			yi = tan_table[view_angle] * (x_bound - x) + y;

			// set cell delta

			next_x_cell = 0;

		} // end if right half plane
		else {

			// compute first vertical line that could be intersected with ray
			// note: it will be to the left of player

			x_bound = CELL_X_SIZE * (x / CELL_X_SIZE);

			// compute delta to get to next vertical line

			x_delta = -CELL_X_SIZE;

			// based on first possible vertical intersection line, compute Y
			// intercept, so that casting can begin

			yi = tan_table[view_angle] * (x_bound - x) + y;

			// set cell delta

			next_x_cell = -1;

		} // end else right half plane

		// begin cast

		casting = 2;                // two rays to cast simultaneously
		xray = yray = 0;                // reset intersection flags

		// S E C T I O N  4 /////////////////////////////////////////////////////////

		while (casting) {

			// continue casting each ray in parallel

			if (xray != INTERSECTION_FOUND) {

				// test for asymtotic ray

				// if (view_angle==ANGLE_90 || view_angle==ANGLE_270)

				if (fabs(y_step[view_angle]) == 0) {
					xray = INTERSECTION_FOUND;
					casting--;
					dist_x = 1e+8;
				}

				// compute current map position to inspect

				cell_x = ((x_bound + next_x_cell) / CELL_X_SIZE);
				cell_y = (long)(yi / CELL_Y_SIZE);

				// Make sure values are within bounds

				cell_x = CLAMP(cell_x, 0, WORLD_COLUMNS);
				cell_y = CLAMP(cell_y, 0, WORLD_ROWS);

				// test if there is a block where the current x ray is intersecting

				if ((x_hit_type = world[(WORLD_ROWS - 1) - cell_y][cell_x]) != 0) {
					// compute distance

					dist_x = (yi - y) * inv_sin_table[view_angle];
					yi_save = yi;
					xb_save = x_bound;

					// terminate X casting

					xray = INTERSECTION_FOUND;
					casting--;

				} // end if a hit
				else {
					// compute next Y intercept

					yi += y_step[view_angle];

				}

			} // end if x ray has intersected

			// S E C T I O N  5 /////////////////////////////////////////////////////////

			if (yray != INTERSECTION_FOUND) {

				// test for asymtotic ray

				// if (view_angle==ANGLE_0 || view_angle==ANGLE_180)

				if (fabs(x_step[view_angle]) == 0) {
					yray = INTERSECTION_FOUND;
					casting--;
					dist_y = 1e+8;

				} // end if asymtotic ray

				// compute current map position to inspect

				cell_x = (long)(xi / CELL_X_SIZE);
				cell_y = ((y_bound + next_y_cell) / CELL_Y_SIZE);

				// Make sure values are within bounds

				cell_x = CLAMP(cell_x, 0, WORLD_COLUMNS);
				cell_y = CLAMP(cell_y, 0, WORLD_ROWS);

				// test if there is a block where the current y ray is intersecting

				if ((y_hit_type = world[(WORLD_ROWS - 1) - cell_y][cell_x]) != 0) {
					// compute distance

					dist_y = (xi - x) * inv_cos_table[view_angle];
					xi_save = xi;
					yb_save = y_bound;

					// terminate Y casting

					yray = INTERSECTION_FOUND;
					casting--;

				} // end if a hit
				else {
					// compute next X intercept

					xi += x_step[view_angle];

				} // end else

			} // end if y ray has intersected

			// move to next possible intersection points

			x_bound += x_delta;
			y_bound += y_delta;
		}

		// S E C T I O N  6 /////////////////////////////////////////////////////////

		// at this point, we know that the ray has succesfully hit both a
		// vertical wall and a horizontal wall, so we need to see which one
		// was closer and then render it

		// note: latter we will replace the crude monochrome line with a sliver
		// of texture, but this is good enough for now

		if (dist_x < dist_y) {

			sline(x, y, (long)xb_save, (long)yi_save, rcolor);

			// there was a vertical wall closer than the horizontal

			// compute actual scale and multiply by view filter so that spherical
			// distortion is cancelled

			scale = cos_table[ray] * 15000 / (1e-10 + dist_x);

			// compute top and bottom and do a very crude clip

			if ((top = 100 - scale / 2) < 1)
				top = 1;

			if ((bottom = top + scale) > 200)
				bottom = 200;

			// draw wall sliver and place some dividers up

			if (((long)yi_save) % CELL_Y_SIZE <= 1) {
				RETRO_DrawLine(638 - ray, top, 638 - ray, bottom, 15);
			} else {
				RETRO_DrawLine(638 - ray, top, 638 - ray, bottom, 10);
			}

		} else // must of hit a horizontal wall first
		{
			sline(x, y, (long)xi_save, (long)yb_save, rcolor);

			// compute actual scale and multiply by view filter so that spherical
			// distortion is cancelled

			scale = cos_table[ray] * 15000 / (1e-10 + dist_y);

			// compute top and bottom and do a very crude clip

			if ((top = 100 - scale / 2) < 1) {
				top = 1;
			}

			if ((bottom = top + scale) > 200) {
				bottom = 200;
			}

			// draw wall sliver and place some dividers up

			if (((long)xi_save) % CELL_X_SIZE <= 1) {
				RETRO_DrawLine(638 - ray, top, 638 - ray, bottom, 15);
			} else {
				RETRO_DrawLine(638 - ray, top, 638 - ray, bottom, 2);
			}
		}

		// S E C T I O N  7 /////////////////////////////////////////////////////////

		// cast next ray

		if (++view_angle >= ANGLE_360) {
			// reset angle back to zero

			view_angle = 0;
		}
	}
}

// M A I N ///////////////////////////////////////////////////////////////////

void DEMO_Render(double deltatime)
{
	static long x = 8 * 64 + 25;
	static long y = 3 * 64 + 25;
	static long view_angle = ANGLE_60;

	// reset deltas
	float dx = 0;
	float dy = 0;

	// draw top view of world
	Draw_2D_Map();

	// what is user doing

	if (RETRO_KeyState(SDL_SCANCODE_RIGHT)) {
		if ((view_angle -= ANGLE_6) < ANGLE_0) {
			view_angle = ANGLE_360;
		}
	}
	if (RETRO_KeyState(SDL_SCANCODE_LEFT)) {
		if ((view_angle += ANGLE_6) >= ANGLE_360) {
			view_angle = ANGLE_0;
		}
	}
	if (RETRO_KeyState(SDL_SCANCODE_UP)) {
		dx = cos(6.28 * view_angle / ANGLE_360) * 10;
		dy = sin(6.28 * view_angle / ANGLE_360) * 10;
	}
	if (RETRO_KeyState(SDL_SCANCODE_DOWN)) {
		dx = -cos(6.28 * view_angle / ANGLE_360) * 10;
		dy = -sin(6.28 * view_angle / ANGLE_360) * 10;
	}

	// move player
	x += dx;
	y += dy;

	// test if user has bumped into a wall i.e. test if there
	// is a cell within the direction of motion, if so back up !

	// compute cell position
	long x_cell = x / CELL_X_SIZE;
	long y_cell = y / CELL_Y_SIZE;

	// compute position relative to cell
	long x_sub_cell = x % CELL_X_SIZE;
	long y_sub_cell = y % CELL_Y_SIZE;

	// resolve motion into it's x and y components
	if (dx > 0) {
		// moving right
		if ((world[(WORLD_ROWS - 1) - y_cell][x_cell + 1] != 0) && (x_sub_cell > (CELL_X_SIZE - OVERBOARD))) {
			// back player up amount he steped over the line
			x -= (x_sub_cell - (CELL_X_SIZE - OVERBOARD));
		}
	} else {
		// moving left
		if ((world[(WORLD_ROWS - 1) - y_cell][x_cell - 1] != 0) && (x_sub_cell < (OVERBOARD))) {
			// back player up amount he steped over the line
			x += (OVERBOARD - x_sub_cell);
		}
	}

	if (dy > 0) {
		// moving up
		if ((world[(WORLD_ROWS - 1) - (y_cell + 1)][x_cell] != 0) && (y_sub_cell > (CELL_Y_SIZE - OVERBOARD))) {
			// back player up amount he steped over the line
			y -= (y_sub_cell - (CELL_Y_SIZE - OVERBOARD));
		}
	} else {
		// moving down
		if ((world[(WORLD_ROWS - 1) - (y_cell - 1)][x_cell] != 0) && (y_sub_cell < (OVERBOARD))) {
			// back player up amount he steped over the line
			y += (OVERBOARD - y_sub_cell);
		}
	}

	// render the view
	Ray_Caster(x, y, view_angle);
}

void DEMO_Initialize(void)
{
	Build_Tables();
	Load_World("assets/raymap.dat");

	RETRO_SetPalette(RETRO_Default8bitPalette);
}