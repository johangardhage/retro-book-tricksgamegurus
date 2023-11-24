//
// WARLOCK.CPP
//

#define RETRO_WIDTH 320
#define RETRO_HEIGHT 200

#include "lib/retro.h"
#include "lib/retromain.h"
#include "lib/retrogfx.h"
#include "lib/retrofont.h"
#include "graphics.h"

// D E F I N E S /////////////////////////////////////////////////////////////

// indices into arrow key state table
#define INDEX_UP        0
#define INDEX_DOWN      1
#define INDEX_RIGHT     2
#define INDEX_LEFT      3

// these are for the door system
#define DOOR_DEAD            0     // the door is gone
#define DOOR_DYING           1     // the door is phasing
#define PROCESS_DOOR_DESTROY 0     // tells the door engine to continue processing
#define START_DOOR_DESTROY   1     // telsl the door engine to begin

#define OVERBOARD              25  // the closest a player can get to a wall
#define INTERSECTION_FOUND      1  // used by ray caster to flag an intersection
#define MAX_SCALE             SCREEN_HEIGHT * 2  // maximum size and wall "sliver" can be
#define WINDOW_HEIGHT         SCREEN_HEIGHT * 2 // height of the game view window
#define WINDOW_MIDDLE         (SCREEN_HEIGHT / 2)  // the center or horizon of the view window
#define VERTICAL_SCALE      15000 // used to scale the "slivers" to get proper perspective and aspect ratio

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
#define CELL_X_SIZE   64     // size of a cell in the game world
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

float inv_cos_table[ANGLE_360 + 1];          // used to compute distances by calculating
float inv_sin_table[ANGLE_360 + 1];          // the hypontenuse

sprite object;                     // general sprite object used by everyone

pcx_picture walls_pcx;             // holds the wall textures

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

// used for color FX
RGB_color red_glow;                       // red glowing objects
int red_glow_index = 254;                 // index of color register to glow

// variables to track status of a door
int door_state = DOOR_DEAD;               // state of door
int door_clock = 0;                       // global door clock, counts number of frames to do door animation

// F U N C T I O N S /////////////////////////////////////////////////////////

void Build_Tables(void)
{
	// this function builds all the look up tables for the system

	// create tables, sit back for a sec!
	for (int ang = ANGLE_0; ang <= ANGLE_360; ang++) {
		float rad_angle = (float)((3.272e-4) + ang * 2 * M_PI / ANGLE_360);
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
		y_delta;      // position

	float xi,     // used to track the x and y intersections
		yi,
		xi_save,      // used to save exact x and y intersection points
		yi_save,
		dist_x,  // the distance of the x and y ray intersections from
		dist_y,  // the viewpoint
		scale;

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

				// compute current map position to inspect

				cell_x = ((x_bound + next_x_cell) / CELL_X_SIZE);
				cell_y = (long)(yi / CELL_Y_SIZE);

				// Make sure values are within bounds

				bool oob = false;
				if (cell_x < 0 || cell_x > WORLD_COLUMNS || cell_y < 0 || cell_y > WORLD_ROWS) {
					oob = true;
				}

				// test if there is a block where the current x ray is intersecting

				if (oob || (x_hit_type = world[cell_y][cell_x]) != 0) {
					// compute distance

					dist_x = (yi - y) * inv_sin_table[view_angle];
					yi_save = yi;

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

				cell_x = (long)(xi / CELL_X_SIZE);
				cell_y = ((y_bound + next_y_cell) / CELL_Y_SIZE);

				// Make sure values are within bounds

				bool oob = false;
				if (cell_x < 0 || cell_x > WORLD_COLUMNS || cell_y < 0 || cell_y > WORLD_ROWS) {
					oob = true;
				}

				// test if there is a block where the current y ray is intersecting

				if (oob || (y_hit_type = world[cell_y][cell_x]) != 0) {

					// compute distance

					dist_y = (xi - x) * inv_cos_table[view_angle];
					xi_save = xi;

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

			// there was a vertical wall closer than the horizontal

			// compute actual scale and multiply by view filter so that spherical
			// distortion is cancelled

			float cos_ray = 1 / cos((ray - ANGLE_30) * 2 * M_PI / ANGLE_360);
			scale = cos_ray * VERTICAL_SCALE / (1e-10 + dist_x);

			// clip wall sliver against view port

			if (scale > (MAX_SCALE - 1)) {
				scale = (MAX_SCALE - 1);
			}

			if (scale > (WINDOW_HEIGHT - 1)) {
				scale = (WINDOW_HEIGHT - 1);
			}

			sliver_scale = scale - 1;

			// set up parameters for assembly language
			// sliver_texture  ; a pointer to the texture memory
			// sliver_column   ; the current texture column
			// sliver_top      ; the starting Y of the sliver
			// sliver_scale    ; the over all height of the sliver
			// sliver_ray      ; the current video column
			// sliver_clip     ; how much of the texture is being clipped

			sliver_texture = object.frames[x_hit_type];
			sliver_column = ((int)yi_save & 63);
			sliver_top = WINDOW_MIDDLE - ((int)scale >> 1);
			sliver_ray = ray;

			// render the sliver
			for (int y = 0; y < sliver_scale; y++) {
				int work_offset = (int)((CELL_Y_SIZE / (float)sliver_scale * (y + 0.5))) * CELL_X_SIZE;
				if (y + sliver_top >= 0 && y + sliver_top < SCREEN_HEIGHT && sliver_ray >= 0 && sliver_ray < SCREEN_WIDTH) {
					RETRO.framebuffer[(y + sliver_top) * SCREEN_WIDTH + sliver_ray] = sliver_texture[work_offset + sliver_column];
				}
			}

		} // end if

		else // must of hit a horizontal wall first
		{
			// there was a vertical wall closer than the horizontal

			// compute actual scale and multiply by view filter so that spherical
			// distortion is cancelled

			float cos_ray = 1 / cos((ray - ANGLE_30) * 2 * M_PI / ANGLE_360);
			scale = cos_ray * VERTICAL_SCALE / (1e-10 + dist_y);

			// do clipping again

			if (scale > (MAX_SCALE - 1)) {
				scale = (MAX_SCALE - 1);
			}

			if (scale > (WINDOW_HEIGHT - 1)) {
				scale = (WINDOW_HEIGHT - 1);
			}

			sliver_scale = scale - 1;

			// set up parameters for assembly language
			// sliver_texture  ; a pointer to the texture memory
			// sliver_column   ; the current texture column
			// sliver_top      ; the starting Y of the sliver
			// sliver_scale    ; the over all height of the sliver
			// sliver_ray      ; the current video column
			// sliver_clip     ; how much of the texture is being clipped

			sliver_texture = object.frames[y_hit_type + 1];
			sliver_column = ((int)xi_save & 63);
			sliver_top = WINDOW_MIDDLE - ((int)scale >> 1);
			sliver_ray = ray;

			// render the sliver
			for (int y = 0; y < sliver_scale; y++) {
				int work_offset = (int)((CELL_Y_SIZE / (float)sliver_scale * (y + 0.5))) * CELL_X_SIZE;
				if (y + sliver_top >= 0 && y + sliver_top < SCREEN_HEIGHT && sliver_ray >= 0 && sliver_ray < SCREEN_WIDTH) {
					RETRO.framebuffer[(y + sliver_top) * SCREEN_WIDTH + sliver_ray] = sliver_texture[work_offset + sliver_column];
				}
			}

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
	// reset deltas
	float dx = 0;
	float dy = 0;

	// what is user doing

	if (RETRO_KeyState(SDL_SCANCODE_RIGHT)) {
		if ((player_view_angle -= ANGLE_3) < ANGLE_0) {
			player_view_angle = ANGLE_360;
		}
	}

	if (RETRO_KeyState(SDL_SCANCODE_LEFT)) {
		if ((player_view_angle += ANGLE_3) >= ANGLE_360) {
			player_view_angle = ANGLE_0;
		}
	}

	if (RETRO_KeyState(SDL_SCANCODE_UP)) {
		dx = (float)(cos(2 * M_PI * player_view_angle / ANGLE_360) * STEP_LENGTH);
		dy = (float)(sin(2 * M_PI * player_view_angle / ANGLE_360) * STEP_LENGTH);
	}

	if (RETRO_KeyState(SDL_SCANCODE_DOWN)) {
		dx = (float)(-cos(2 * M_PI * player_view_angle / ANGLE_360) * STEP_LENGTH);
		dy = (float)(-sin(2 * M_PI * player_view_angle / ANGLE_360) * STEP_LENGTH);
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

	// S E C T I O N   6 /////////////////////////////////////////////////////////

	if (RETRO_KeyState(SDL_SCANCODE_RETURN)) {
		// test if there is a door in front of player

		// project a "feeler" 3 steps in front of player and test for a door
		int door_x = (int)(player_x + cos(2 * M_PI * player_view_angle / ANGLE_360) * 6 * STEP_LENGTH);
		int door_y = (int)(player_y + sin(2 * M_PI * player_view_angle / ANGLE_360) * 6 * STEP_LENGTH);

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
	RETRO_DrawFilledRectangle(0, 0, RETRO_WIDTH, 80, 0);
	RETRO_DrawFilledRectangle(0, RETRO_HEIGHT / 2, RETRO_WIDTH, RETRO_HEIGHT, 8);

	// render the view
	Ray_Caster(player_x, player_y, player_view_angle);
}

void DEMO_Initialize(void)
{
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
