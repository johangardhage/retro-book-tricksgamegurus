//
// WARLOCK.CPP
//
#include "lib/retro.h"
#include "lib/retromain.h"
#include "lib/retrogfx.h"
#include "lib/retrofont.h"
#include "graphics.h"

// T Y P E S ////////////////////////////////////////////////////////////////

typedef long fixed;    // fixed point is 32 bits

// S T R U C T U R E S //////////////////////////////////////////////////////

// this structure is used for the worm or melt effect
typedef struct worm_typ
{
	int y;       // current y position of worm
	int color;   // color of worm
	int speed;   // speed of worm
	int counter; // counts time until movement
} worm, *worm_ptr;

// D E F I N E S /////////////////////////////////////////////////////////////

// #define MAKING_DEMO 1     // this flag is used to turn on the demo record option.  this is for developers only

// indices into arrow key state table

#define INDEX_UP        0
#define INDEX_DOWN      1
#define INDEX_RIGHT     2
#define INDEX_LEFT      3

// these dataums are used as the records for the demo data

#define DEMO_RIGHT      1
#define DEMO_LEFT       2
#define DEMO_UP         4
#define DEMO_DOWN       8

// these are for the door system

#define DOOR_DEAD            0     // the door is gone
#define DOOR_DYING           1     // the door is phasing
#define PROCESS_DOOR_DESTROY 0     // tells the door engine to continue processing
#define START_DOOR_DESTROY   1     // telsl the door engine to begin

#define MAX_LENGTH_DEMO      2048  // maximum length a demo sequence can be
#define END_OF_DEMO          255   // used in the demo file to flag EOF

#define OVERBOARD              52  // the closest a player can get to a wall

#define INTERSECTION_FOUND      1  // used by ray caster to flag an intersection

#define MAX_SCALE             201  // maximum size and wall "sliver" can be

#define WINDOW_HEIGHT         152  // height of the game view window

#define WINDOW_MIDDLE          76  // the center or horizon of the view window

#define VERTICAL_SCALE      13312  // used to scale the "slivers" to get proper perspective and aspect ratio

// constants used to represent angles for the ray caster

#define ANGLE_0     0
#define ANGLE_1     5
#define ANGLE_2     10
#define ANGLE_3     15
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
#define ANGLE_360   1920     // note: the circle has been broken up into 1920 sub-arcs

#define STEP_LENGTH 5       // number of units player moves foward or backward

#define WORLD_ROWS    64     // number of rows in the game world
#define WORLD_COLUMNS 64     // number of columns in the game world

#define CELL_X_SIZE   64     // size of a cell in the gamw world
#define CELL_Y_SIZE   64

#define CELL_X_SIZE_FP   6   // log base 2 of 64 (used for quick division)
#define CELL_Y_SIZE_FP   6

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

int *scale_table[MAX_SCALE + 1];     // table with pre-computed scale indices

worm worms[SCREEN_WIDTH];                   // used to make the screen melt

sprite object;                     // general sprite object used by everyone

pcx_picture walls_pcx,             // holds the wall textures
controls_pcx,             // holds the control panel at bottom of screen
intro_pcx;                // holds the intro screen

int demo_mode = 0;                   // toogles demo mode on and off.  Note: this must be 0 to record a demo

// parmeter block used by assembly language sliver engine

unsigned char *sliver_texture; // pointer to texture being rendered
int sliver_column;        // index into texture i.e. which column of texture
int sliver_top;           // starting Y position to render at
int sliver_scale;         // overall height of sliver
int sliver_ray;           // current ray being cast
int sliver_clip;          // index into texture after clipping
int *scale_row;           // row of scale value look up table to use

// the player
int player_x,                 // the players X position
player_y,                 // the players Y position
player_view_angle;        // the current view angle of the player

unsigned char *demo;      // table of data for demo mode

// if the code gets enabled it allocates various data to create a demo file

#if MAKING_DEMO
unsigned char demo_out[MAX_LENGTH_DEMO];  // digitized output file
unsigned char demo_word = 0;                // packed demo packet
int demo_out_index = 0;                     // number of motions in file
FILE *fp;                       // general file stuff
#endif

// used for color FX

RGB_color red_glow;                       // red glowing objects
int red_glow_index = 254;                 // index of color register to glow

// variables to track status of a door

int door_state = DOOR_DEAD;               // state of door
int door_clock = 0;                       // global door clock, counts number of frames to do door animation

// F U N C T I O N S /////////////////////////////////////////////////////////

void Render_Sliver1(sprite_ptr sprite, long scale, int column)
{
	// this function will scale a single sliver of texture data.  it uses fixed point numbers.

	int work_offset = 0;

	int scale_int = scale;
	scale = (scale << 8);

	fixed scale_index = 0;
	fixed scale_step = (fixed)(((fixed)64) << 16) / scale;

	// alias a pointer to sprite for ease of access
	unsigned char *work_sprite = sprite->frames[sprite->curr_frame];

	// compute offset of sprite in video buffer
	int offset = (sprite->y * SCREEN_WIDTH) + sprite->x;

	for (int y = 0; y < scale_int; y++) {
		RETRO.framebuffer[offset] = work_sprite[work_offset + column];
		scale_index += scale_step;
		offset += SCREEN_WIDTH;
		work_offset = ((scale_index & 0xff00) >> 2);
	}
}

///////////////////////////////////////////////////////////////////////////////

void Render_Sliver2(sprite_ptr sprite, int scale, int column)
{
	// this is yet another version of the sliver scaler, however it uses look up
	// tables with pre-computed scale indices.  in the end I converted this to
	// assembly for speed

	int work_offset = 0, scale_off = 0;

	// alias proper data row
	int *row = scale_table[scale];

	if (scale > (WINDOW_HEIGHT - 1)) {
		scale_off = (scale - (WINDOW_HEIGHT - 1)) >> 1;
		scale = (WINDOW_HEIGHT - 1);
		sprite->y = 0;
	}

	// alias a pointer to sprite for ease of access
	unsigned char *work_sprite = sprite->frames[sprite->curr_frame];

	// compute offset of sprite in video buffer
	int offset = (sprite->y * SCREEN_WIDTH) + sprite->x;

	for (int y = 0; y < scale; y++) {
		RETRO.framebuffer[offset] = work_sprite[work_offset + column];
		offset += SCREEN_WIDTH;
		work_offset = row[y + scale_off];
	}
}

///////////////////////////////////////////////////////////////////////////////

void Create_Scale_Data(int scale, int *row)
{
	// this function synthesizes the scaling of a texture sliver to all possible
	// sizes and creates a huge look up table of the data.

	float y_scale_index = 0,
		y_scale_step;

	// compute scale step or number of source pixels to map to destination/cycle
	y_scale_step = (float)64 / (float)scale;
	y_scale_index += y_scale_step;

	for (int y = 0; y < scale; y++) {
		// place data into proper array position for later use
		row[y] = ((int)(y_scale_index + .5)) * CELL_X_SIZE;

		// test if we slightly went overboard
		if (row[y] > 63 * CELL_X_SIZE) row[y] = 63 * CELL_X_SIZE;

		// next index please
		y_scale_index += y_scale_step;
	}
}

///////////////////////////////////////////////////////////////////////////////

void Build_Tables(void)
{
	// this function builds all the look up tables for the system

	// create the lookup tables for the scaler
	// there have the form of an array of pointers, where each pointer points
	// another another array of data where the 'data' are the scale indices
	for (int scale = 0; scale <= MAX_SCALE; scale++) {
		scale_table[scale] = (int *)malloc(scale * sizeof(int) + 1);
	}

	// create tables, sit back for a sec!
	for (int ang = ANGLE_0; ang <= ANGLE_360; ang++) {
		float rad_angle = (float)((3.272e-4) + ang * 2 * 3.141592654 / ANGLE_360);
		tan_table[ang] = (float)tan(rad_angle);
		inv_tan_table[ang] = (float)(1 / tan_table[ang]);

		// tangent has the incorrect signs in all quadrants except 1, so
		// manually fix the signs of each quadrant since the tangent is
		// equivalent to the slope of a line and if the tangent is wrong
		// then the ray that is case will be wrong

		if (ang >= ANGLE_0 && ang < ANGLE_180) {
			y_step[ang] = (float)(fabs(tan_table[ang] * CELL_Y_SIZE));
		} else
			y_step[ang] = (float)(-fabs(tan_table[ang] * CELL_Y_SIZE));

		if (ang >= ANGLE_90 && ang < ANGLE_270) {
			x_step[ang] = (float)(-fabs(inv_tan_table[ang] * CELL_X_SIZE));
		} else {
			x_step[ang] = (float)(fabs(inv_tan_table[ang] * CELL_X_SIZE));
		}

		// create the sin and cosine tables to copute distances
		inv_cos_table[ang] = (float)(1 / cos(rad_angle));
		inv_sin_table[ang] = (float)(1 / sin(rad_angle));
	}

	// create view filter table.  There is a cosine wave modulated on top of
	// the view distance as a side effect of casting from a fixed point.
	// to cancell this effect out, we multiple by the inverse of the cosine
	// and the result is the proper scale.  Without this we would see a
	// fishbowl effect, which might be desired in some cases?
	for (int ang = -ANGLE_30; ang <= ANGLE_30; ang++) {
		float rad_angle = (float)((3.272e-4) + ang * 2 * 3.141592654 / ANGLE_360);
		cos_table[ang + ANGLE_30] = (float)(VERTICAL_SCALE / cos(rad_angle));
	}

	// build the scaler table.  This table holds MAX_SCALE different arrays.  Each
	// array consists of the pre-computed indices for an object to be scaled
	for (int scale = 1; scale <= MAX_SCALE; scale++) {
		// create the indices for this scale
		Create_Scale_Data(scale, (int *)scale_table[scale]);
	}
}

////////////////////////////////////////////////////////////////////////////////

int Load_World(const char *file)
{
	// this function opens the input file and loads the world data from it

	FILE *fp;
	int row, column;
	char ch;

	// open the file
	if (!(fp = fopen(file, "r"))) {
		return(0);
	}

	// load in the data
	for (row = 0; row < WORLD_ROWS; row++) {
		// load in the next row
		for (column = 0; column < WORLD_COLUMNS; column++) {
			while ((ch = getc(fp)) == 10) {} // filter out CR
			// translate character to integer
			if (ch == ' ')
				ch = 0;
			else
				ch = ch - '0';

			// insert data into world
			world[(WORLD_ROWS - 1) - row][column] = ch;
		}
	}

	// close the file
	fclose(fp);

	return(1);
}

/////////////////////////////////////////////////////////////////////////////

void sline(long x1, long y1, long x2, long y2, int color)
{
	// used a a diagnostic function to draw a scaled line

	x1 = x1 / 16;
	y1 = (y1 / 16);

	x2 = x2 / 16;
	y2 = (y2 / 16);

	RETRO_DrawLine(x1, y1, x2, y2, color);
}

////////////////////////////////////////////////////////////////////////////////

void Demo_Setup(void)
{
	// this function allocates the demo mode storage area and loads the demo mode data

	FILE *fp_demo;
	int index = 0;
	unsigned char data;

	// allocate storage for demo mode
	demo = (unsigned char *)malloc(MAX_LENGTH_DEMO);

	// open up demo file
	fp_demo = fopen("assets/wardemo.dat", "rb");

	// load data
	while ((data = getc(fp_demo)) != END_OF_DEMO) {
		demo[index++] = data;
	}

	// place end of demo flag in data
	demo[index] = END_OF_DEMO;

	// close file
	fclose(fp_demo);
}

/////////////////////////////////////////////////////////////////////////////

void Draw_Ground(void)
{
	RETRO_DrawFilledRectangle(RETRO_WIDTH / 2 - 1, 0, RETRO_WIDTH - 1, 80, 0);
	RETRO_DrawFilledRectangle(RETRO_WIDTH / 2 - 1, 80, RETRO_WIDTH - 1, 160, 8);
}

/////////////////////////////////////////////////////////////////////////////

void Draw_2D_Map(void)
{
	// draw 2-D map of world

	for (int row = 0; row < WORLD_ROWS; row++) {
		for (int column = 0; column < WORLD_COLUMNS; column++) {
			int block = world[row][column];

			// test if there is a solid block there
			if (block == 0) {
				RETRO_DrawRectangle(column * CELL_X_SIZE / 16,
									row * CELL_Y_SIZE / 16,
									column * CELL_X_SIZE / 16 + CELL_X_SIZE / 16 - 1,
									row * CELL_Y_SIZE / 16 + CELL_Y_SIZE / 16 - 1,
									15);
			} else {
				RETRO_DrawFilledRectangle(column * CELL_X_SIZE / 16,
										row * CELL_Y_SIZE / 16,
										column * CELL_X_SIZE / 16 + CELL_X_SIZE / 16,
										row * CELL_Y_SIZE / 16 + CELL_Y_SIZE / 16,
										2);
			}
		}
	}
}

/////////////////////////////////////////////////////////////////////////////

void Ray_Caster(long x, long y, long view_angle)
{
	// This is the heart of the system.  it casts out 320 rays and builds the
	// 3-D image from their intersections with the walls.  It was derived from
	// the previous version used in "RAY.C", however, it has been extremely
	// optimized for speed by the use of many more lookup tables and fixed
	// point math

	int
		cell_x,       // the current cell that the ray is in
		cell_y,
		ray,          // the current ray being cast 0-320
		casting = 2,    // tracks the progress of the X and Y component of the ray
		x_hit_type,   // records the block that was intersected, used to figure
		y_hit_type,   // out which texture to use
		x_bound,      // the next vertical and horizontal intersection point
		y_bound,
		next_y_cell,  // used to figure out the quadrant of the ray
		next_x_cell,
		xray = 0,       // tracks the progress of a ray looking for Y interesctions
		yray = 0,       // tracks the progress of a ray looking for X interesctions

		x_delta,      // the amount needed to move to get to the next cell
		y_delta,      // position
		xi_save,      // used to save exact x and y intersection points
		yi_save,
		scale;

	long
		dist_x,  // the distance of the x and y ray intersections from
		dist_y;  // the viewpoint

	float xi,     // used to track the x and y intersections
		yi;

	// S E C T I O N  1 /////////////////////////////////////////////////////////v

	// initialization

	// compute starting angle from player.  Field of view is 60 degrees, so
	// subtract half of that current view angle

	if ((view_angle -= ANGLE_30) < 0) {
		view_angle = ANGLE_360 + view_angle;
	}

	// loop through all 320 rays

	for (ray = 319; ray >= 0; ray--) {

		// S E C T I O N  2 /////////////////////////////////////////////////////////

		// compute first x intersection

		// need to know which half plane we are casting from relative to Y axis

		if (view_angle >= ANGLE_0 && view_angle < ANGLE_180) {

			// compute first horizontal line that could be intersected with ray
			// note: it will be above player

			y_bound = (CELL_Y_SIZE + (y & 0xffc0));

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

			y_bound = (int)(y & 0xffc0);

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

			x_bound = (int)(CELL_X_SIZE + (x & 0xffc0));

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

			x_bound = (int)(x & 0xffc0);

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

				// compute current map position to inspect

				cell_x = ((x_bound + next_x_cell) >> CELL_X_SIZE_FP);

				cell_y = (int)yi;
				cell_y >>= CELL_Y_SIZE_FP;

				// Make sure values are within bounds

				bool oob = false;
				if (cell_x < 0 || cell_x > WORLD_COLUMNS || cell_y < 0 || cell_y > WORLD_ROWS) {
					oob = true;
				}

				// test if there is a block where the current x ray is intersecting

				if (oob || (x_hit_type = world[cell_y][cell_x]) != 0) {
					// compute distance

					dist_x = (long)((yi - y) * inv_sin_table[view_angle]);
					yi_save = (int)yi;

					// terminate X casting

					xray = INTERSECTION_FOUND;
					casting--;

				} // end if a hit
				else {

					// compute next Y intercept

					yi += y_step[view_angle];

					// find next possible x intercept point

					x_bound += x_delta;

				} // end else

			} // end if x ray has intersected

			// S E C T I O N  5 /////////////////////////////////////////////////////////

			if (yray != INTERSECTION_FOUND) {

				// compute current map position to inspect

				cell_x = xi;
				cell_x >>= CELL_X_SIZE_FP;

				cell_y = ((y_bound + next_y_cell) >> CELL_Y_SIZE_FP);

				// Make sure values are within bounds

				bool oob = false;
				if (cell_x < 0 || cell_x > WORLD_COLUMNS || cell_y < 0 || cell_y > WORLD_ROWS) {
					oob = true;
				}

				// test if there is a block where the current y ray is intersecting

				if (oob || (y_hit_type = world[cell_y][cell_x]) != 0) {

					// compute distance

					dist_y = (long)((xi - x) * inv_cos_table[view_angle]);
					xi_save = (int)xi;

					yray = INTERSECTION_FOUND;
					casting--;

				} // end if a hit
				else {

					// terminate Y casting

					xi += x_step[view_angle];

					// compute next possible y intercept

					y_bound += y_delta;

				} // end else

			} // end if y ray has intersected

		} // end while not done

		// S E C T I O N  6 /////////////////////////////////////////////////////////

		// at this point, we know that the ray has succesfully hit both a
		// vertical wall and a horizontal wall, so we need to see which one
		// was closer and then render it

		if (dist_x < dist_y) {

			sline(x, y, (long)x_bound, (long)yi_save, 1);

			// there was a vertical wall closer than the horizontal

			// compute actual scale and multiply by view filter so that spherical
			// distortion is cancelled

			scale = (int)(cos_table[ray] / dist_x);

			// clip wall sliver against view port

			if (scale > (MAX_SCALE - 1)) scale = (MAX_SCALE - 1);

			scale_row = scale_table[scale - 1];

			if (scale > (WINDOW_HEIGHT - 1)) {
				sliver_clip = (scale - (WINDOW_HEIGHT - 1)) >> 1;
				scale = (WINDOW_HEIGHT - 1);
			} else
				sliver_clip = 0;

			sliver_scale = scale - 1;

			// set up parameters for assembly language
			// sliver_texture  ; a pointer to the texture memory
			// sliver_column   ; the current texture column
			// sliver_top      ; the starting Y of the sliver
			// sliver_scale    ; the over all height of the sliver
			// sliver_ray      ; the current video column
			// sliver_clip     ; how much of the texture is being clipped
			// scale_row       ; the pointer to the proper row of pre-computed scale indices

			sliver_texture = object.frames[x_hit_type];
			sliver_column = (yi_save & 63);
			sliver_top = WINDOW_MIDDLE - (scale >> 1);
			sliver_ray = 638 - ray;

			// render the sliver
			object.curr_frame = x_hit_type;
			object.x = sliver_ray;
			object.y = sliver_top;
			Render_Sliver2(&object, sliver_scale, sliver_column);

		} // end if

		else // must of hit a horizontal wall first
		{
			sline(x, y, (long)xi_save, (long)y_bound, 1);

			// there was a vertical wall closer than the horizontal

			// compute actual scale and multiply by view filter so that spherical
			// distortion is cancelled

			scale = (int)(cos_table[ray] / dist_y);

			// do clipping again

			if (scale > (MAX_SCALE - 1)) scale = (MAX_SCALE - 1);

			scale_row = scale_table[scale - 1];

			if (scale > (WINDOW_HEIGHT - 1)) {
				sliver_clip = (scale - (WINDOW_HEIGHT - 1)) >> 1;
				scale = (WINDOW_HEIGHT - 1);
			} else
				sliver_clip = 0;

			sliver_scale = scale - 1;

			// set up parameters for assembly language
			// sliver_texture  ; a pointer to the texture memory
			// sliver_column   ; the current texture column
			// sliver_top      ; the starting Y of the sliver
			// sliver_scale    ; the over all height of the sliver
			// sliver_ray      ; the current video column
			// sliver_clip     ; how much of the texture is being clipped
			// scale_row       ; the pointer to the proper row of pre-computed scale indices

			sliver_texture = object.frames[y_hit_type + 1];
			sliver_column = (xi_save & 63);
			sliver_top = WINDOW_MIDDLE - (scale >> 1);
			sliver_ray = 638 - ray;

			// render the sliver
			object.curr_frame = y_hit_type + 1;
			object.x = sliver_ray;
			object.y = sliver_top;
			Render_Sliver2(&object, sliver_scale, sliver_column);

		} // end else

		// S E C T I O N  7 /////////////////////////////////////////////////////////

		// cast next ray

		// test if view angle need to wrap around

		if (++view_angle >= ANGLE_360) {

			view_angle = 0;

		} // end if

	} // end for ray

} // end Ray_Caster

/////////////////////////////////////////////////////////////////////////////

void Destroy_Door(int x_cell, int y_cell, int command)
{
	// this function is called every frame when a door is being destroyed.
	// Basically it cycles a color on the door and makes it glow red as if it
	// were energizing.  the function does this a specific number of times and
	// then turns itself off and takes the door out of the world

	static int door_x_cell,   // used to hold the position of the door
		door_y_cell;

	// test what is happening i.e. door starting destruction or continuing
	if (command == START_DOOR_DESTROY) {
		// play spell

		// reset glow color
		red_glow.red = 0;
		red_glow.green = 0;
		red_glow.blue = 0;

		Set_Palette_Register(red_glow_index, (RGB_color_ptr)&red_glow);

		// set door sequence in motion and number of clicks
		door_state = DOOR_DYING;
		door_clock = 30;

		// remember where door is so we can make it disapear
		door_x_cell = x_cell;
		door_y_cell = y_cell;
	} else {
		// increase intensity of glow
		red_glow.red += 2;

		Set_Palette_Register(red_glow_index, (RGB_color_ptr)&red_glow);

		// test if we are done with door
		if (--door_clock < 0) {
			// reset state of door
			door_state = DOOR_DEAD;

			// say bye-bye to door
			world[door_y_cell][door_x_cell] = 0;

			// reset palette register
			red_glow.red = 0;

			Set_Palette_Register(red_glow_index, (RGB_color_ptr)&red_glow);
		}
	}
}

// M A I N ///////////////////////////////////////////////////////////////////

void DEMO_Render(double deltatime)
{
#if MAKING_DEMO
	demo_word = 0;
#endif

	// reset deltas
	float dx = 0;
	float dy = 0;

	// draw top view of world
	Draw_2D_Map();

	static int demo_index = 0;
	unsigned char demo_data;

	if (demo_mode) {
		// read raw key from file
		demo_data = demo[demo_index++];

		// test for end of demo, if at end start demo over
		if (demo_data == END_OF_DEMO) {
			// reset data index
			demo_index = 0;

			// move player to starting position again
			player_x = 53 * 64 + 25;
			player_y = 14 * 64 + 25;
			player_view_angle = ANGLE_60;
		}
	}

	// what is user doing

	if (RETRO_KeyState(SDL_SCANCODE_LEFT) || (demo_data & DEMO_LEFT)) {
		if ((player_view_angle -= ANGLE_3) < ANGLE_0) {
			player_view_angle = ANGLE_360;
		}
#if MAKING_DEMO
		demo_word |= DEMO_LEFT;
#endif
	}

	if (RETRO_KeyState(SDL_SCANCODE_RIGHT) || (demo_data & DEMO_RIGHT)) {
		if ((player_view_angle += ANGLE_3) >= ANGLE_360) {
			player_view_angle = ANGLE_0;
		}
#if MAKING_DEMO
		demo_word |= DEMO_RIGHT;
#endif
	}

	if (RETRO_KeyState(SDL_SCANCODE_UP) || (demo_data & DEMO_UP)) {
		dx = (float)(cos(6.28 * player_view_angle / ANGLE_360) * STEP_LENGTH);
		dy = (float)(sin(6.28 * player_view_angle / ANGLE_360) * STEP_LENGTH);
#if MAKING_DEMO
		demo_word |= DEMO_UP;
#endif
	}

	if (RETRO_KeyState(SDL_SCANCODE_DOWN) || (demo_data & DEMO_DOWN)) {
		dx = (float)(-cos(6.28 * player_view_angle / ANGLE_360) * STEP_LENGTH);
		dy = (float)(-sin(6.28 * player_view_angle / ANGLE_360) * STEP_LENGTH);
#if MAKING_DEMO
		demo_word |= DEMO_DOWN;
#endif
	}

	// S E C T I O N   5 /////////////////////////////////////////////////////////

	// move player
	player_x = (int)((float)player_x + dx);
	player_y = (int)((float)player_y + dy);

	// test if user has bumped into a wall i.e. test if there
	// is a cell within the direction of motion, if so back up !

	// compute cell position
	int x_cell = player_x / CELL_X_SIZE;
	int y_cell = player_y / CELL_Y_SIZE;

	// compute position relative to cell
	int x_sub_cell = player_x % CELL_X_SIZE;
	int y_sub_cell = player_y % CELL_Y_SIZE;

	// resolve motion into it's x and y components

	if (dx > 0) {
		// moving right
		if ((world[y_cell][x_cell + 1] != 0) && (x_sub_cell > (CELL_X_SIZE - OVERBOARD))) {
			// back player up amount he steped over the line
			player_x -= (x_sub_cell - (CELL_X_SIZE - OVERBOARD));
		}
	} else {
		// moving left
		if ((world[y_cell][x_cell - 1] != 0) && (x_sub_cell < (OVERBOARD))) {
			// back player up amount he steped over the line
			player_x += (OVERBOARD - x_sub_cell);
		}
	}

	if (dy > 0) {
		// moving up
		if ((world[(y_cell + 1)][x_cell] != 0) && (y_sub_cell > (CELL_Y_SIZE - OVERBOARD))) {
			// back player up amount he steped over the line
			player_y -= (y_sub_cell - (CELL_Y_SIZE - OVERBOARD));
		}
	} else {
		// moving down
		if ((world[(y_cell - 1)][x_cell] != 0) && (y_sub_cell < (OVERBOARD))) {
			// back player up amount he steped over the line
			player_y += (OVERBOARD - y_sub_cell);
		}
	}

#if MAKING_DEMO
	demo_out[demo_out_index++] = demo_word;
	printf("\ndemo out = %d", demo_word);
#endif

	// S E C T I O N   6 /////////////////////////////////////////////////////////

	if (RETRO_KeyState(SDL_SCANCODE_RETURN)) {
		// test if there is a door in front of player

		// project a "feeler" 3 steps in front of player and test for a door
		int door_x = (int)(player_x + cos(6.28 * player_view_angle / ANGLE_360) * 6 * STEP_LENGTH);
		int door_y = (int)(player_y + sin(6.28 * player_view_angle / ANGLE_360) * 6 * STEP_LENGTH);

		// compute cell position
		x_cell = door_x / CELL_X_SIZE;
		y_cell = door_y / CELL_Y_SIZE;

		// test for door
		if (world[y_cell][x_cell] == 7 || world[y_cell][x_cell] == 8) {
			// make door disapear by starting process
			Destroy_Door(x_cell, y_cell, START_DOOR_DESTROY);
		}
	}

	// call all responder and temporal functions that occur each frame
	Destroy_Door(0, 0, PROCESS_DOOR_DESTROY);

	// S E C T I O N   7 /////////////////////////////////////////////////////////

	// clear the double buffer and render the ground and ceiling
	Draw_Ground();

	// render the view
	Ray_Caster(player_x, player_y, player_view_angle);

	// do all rendering that goes on top of 3-D view here
	if (demo_mode) {
		Blit_String(SCREEN_WIDTH / 2, 16, 10, "D e m o   M o d e", 0);
	}

	RECT src_rect = { 0, 0, controls_pcx.header.horz_res, controls_pcx.header.vert_res }, // the source rectangle
	dest_rect = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT }; // the destination rectangle
	Blit_Rect(src_rect, controls_pcx.buffer, controls_pcx.header.horz_res, dest_rect, RETRO.framebuffer, SCREEN_WIDTH, 0);
}

void DEMO_Initialize(void)
{
	// load the intro screen for a second and bore everyone
	PCX_Init((pcx_picture_ptr)&intro_pcx);
	PCX_Load("assets/warintr2.pcx", (pcx_picture_ptr)&intro_pcx, 1);
//	PCX_Show_Buffer((pcx_picture_ptr)&intro_pcx);

	// load the demo information
	Demo_Setup();

	// build all the lookup tables
	Build_Tables();

	Load_World("assets/warmap.dat");

	// load up the textures
	PCX_Init((pcx_picture_ptr)&walls_pcx);
	PCX_Load("assets/wartext.pcx", (pcx_picture_ptr)&walls_pcx, 1);
	Sprite_Init((sprite_ptr)&object, 0, 0, 0, 0, 0, 0);

	// grab a blank
	PCX_Grap_Bitmap((pcx_picture_ptr)&walls_pcx, (sprite_ptr)&object, 0, 0, 0);

	// grab first wall
	PCX_Grap_Bitmap((pcx_picture_ptr)&walls_pcx, (sprite_ptr)&object, 1, 0, 0);
	PCX_Grap_Bitmap((pcx_picture_ptr)&walls_pcx, (sprite_ptr)&object, 2, 1, 0);

	// grab second wall
	PCX_Grap_Bitmap((pcx_picture_ptr)&walls_pcx, (sprite_ptr)&object, 3, 2, 0);
	PCX_Grap_Bitmap((pcx_picture_ptr)&walls_pcx, (sprite_ptr)&object, 4, 3, 0);

	// grab third wall
	PCX_Grap_Bitmap((pcx_picture_ptr)&walls_pcx, (sprite_ptr)&object, 5, 2, 1);
	PCX_Grap_Bitmap((pcx_picture_ptr)&walls_pcx, (sprite_ptr)&object, 6, 3, 1);

	// grab doors
	PCX_Grap_Bitmap((pcx_picture_ptr)&walls_pcx, (sprite_ptr)&object, 7, 0, 2);
	PCX_Grap_Bitmap((pcx_picture_ptr)&walls_pcx, (sprite_ptr)&object, 8, 1, 2);

	// dont need textures anymore
	PCX_Delete((pcx_picture_ptr)&walls_pcx);

	// load up the control panel
	PCX_Init((pcx_picture_ptr)&controls_pcx);
	PCX_Load("assets/warcont.pcx", (pcx_picture_ptr)&controls_pcx, 0);
//	PCX_Show_Buffer((pcx_picture_ptr)&controls_pcx);
//	PCX_Delete((pcx_picture_ptr)&controls_pcx);
	PCX_Delete((pcx_picture_ptr)&intro_pcx);

	// initialize the generic sprite we will use to access the textures with
	object.curr_frame = 0;
	object.x = 0;
	object.y = 0;

	// position the player somewhere interseting
	player_x = 53 * 64 + 25;
	player_y = 14 * 64 + 25;
	player_view_angle = ANGLE_60;

	red_glow.red = 0;
	red_glow.green = 0;
	red_glow.blue = 0;

	Set_Palette_Register(red_glow_index, (RGB_color_ptr)&red_glow);
}

void DEMO_Deinitialize(void)
{
#if MAKING_DEMO
	// save the digitized demo data to a file
	fp = fopen("demo.dat", "wb");
	for (t = 0; t < demo_out_index - 1; t++) {
		putc(demo_out[t], fp);
	}
	putc(END_OF_DEMO, fp);
	putc(END_OF_DEMO, fp);
	fclose(fp);
#endif
}
