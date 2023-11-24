// GRAPHICS.H - header file for basic graphics and sprites

#ifndef GRAPHICS_H
#define GRAPHICS_H

// D E F I N E S  ////////////////////////////////////////////////////////////

#define SCREEN_WIDTH      RETRO_WIDTH
#define SCREEN_HEIGHT     RETRO_HEIGHT

#define CHAR_WIDTH        8
#define CHAR_HEIGHT       8

#define SPRITE_WIDTH      64
#define SPRITE_HEIGHT     64

#define MAX_SPRITE_FRAMES 24
#define SPRITE_DEAD       0
#define SPRITE_ALIVE      1
#define SPRITE_DYING      2

// S T R U C T U R E S ///////////////////////////////////////////////////////

// this structure holds a RGB triple in three bytes
typedef struct RGB_color_typ
{
	unsigned char red;    // red   component of color 0-63
	unsigned char green;  // green component of color 0-63
	unsigned char blue;   // blue  component of color 0-63
} RGB_color, *RGB_color_ptr;

typedef struct pcx_header_typ
{
	char manufacturer;
	char version;
	char encoding;
	char bits_per_pixel;
	short int x, y;
	short int width, height;
	short int horz_res, vert_res;
	char ega_palette[48];
	char reserved;
	char num_color_planes;
	short int bytes_per_line;
	short int palette_type;
	char padding[58];
} pcx_header, *pcx_header_ptr;

typedef struct pcx_picture_typ
{
	pcx_header header;
	RGB_color palette[256];
	unsigned char *buffer;
} pcx_picture, *pcx_picture_ptr;

typedef struct sprite_typ
{
	int x, y;            // position of sprite
	int x_old, y_old;    // old position of sprite
	int width, height;   // dimensions of sprite in pixels
	int anim_clock;     // the animation clock
	int anim_speed;     // the animation speed
	int motion_speed;   // the motion speed
	int motion_clock;   // the motion clock

	unsigned char *frames[MAX_SPRITE_FRAMES]; // array of pointers to the images
	int curr_frame;                      // current frame being displayed
	int num_frames;                      // total number of frames
	int state;                           // state of sprite, alive, dead...
	unsigned char *background;                // whats under the sprite
} sprite, *sprite_ptr;

// RECT structure (windef.h)
typedef struct RECT_TYP {
	int left;
	int top;
	int right;
	int bottom;
} RECT, *PRECT, *NPRECT, *LPRECT;

// F U N C T I O N S /////////////////////////////////////////////////////////

void Blit_Char(int xc, int yc, char c, int color, int trans_flag)
{
	// this function uses the rom 8x8 character set to blit a character on the
	// video screen, notice the trick used to extract bits out of each character
	// byte that comprises a line

	// compute starting offset in rom character lookup table
	unsigned char *work_char = RETRO_FontData[c - 32];

	// compute offset of character in video buffer
	int offset = (yc * SCREEN_WIDTH) + xc;

	for (int y = 0; y < CHAR_HEIGHT; y++) {
		// reset bit mask
		unsigned char bit_mask = 0x01;

		for (int x = 0; x < CHAR_WIDTH; x++) {
			// test for transparent pixel i.e. 0, if not transparent then draw
			if ((*work_char & bit_mask)) {
				RETRO.framebuffer[offset + x] = color;
			} else if (!trans_flag) { // takes care of transparency
				RETRO.framebuffer[offset + x] = 0;
			}

			// shift bit mask
			bit_mask = (bit_mask << 1);
		}

		// move to next line in video buffer and in rom character data area
		offset += SCREEN_WIDTH;
		work_char++;
	}
}

//////////////////////////////////////////////////////////////////////////////

void Blit_String(int x, int y, int color, const char *string, int trans_flag)
{
	// this function blits an entire string on the screen with fixed spacing
	// between each character.  it calls blit_char.

	int index;

	for (index = 0; string[index] != 0; index++) {
		Blit_Char(x + (index << 3), y, string[index], color, trans_flag);
	}
}

//////////////////////////////////////////////////////////////////////////////

void Set_Palette_Register(int index, RGB_color_ptr color)
{
	// this function sets a single color look up table value indexed by index
	// with the value in the color structure

	RETRO_SetColor(index, color->red, color->green, color->blue);
}

///////////////////////////////////////////////////////////////////////////////

void Get_Palette_Register(int index, RGB_color_ptr color)
{
	// this function gets the data out of a color lookup regsiter and places it
	// into color

	RETRO_Palette palette = RETRO_GetColor(index);

	color->red = palette.r;
	color->green = palette.g;
	color->blue = palette.b;
}

//////////////////////////////////////////////////////////////////////////////

void PCX_Init(pcx_picture_ptr image)
{
	// this function allocates the buffer region needed to load a pcx file

	if (!(image->buffer = (unsigned char *)malloc(SCREEN_WIDTH * SCREEN_HEIGHT + 1))) {
		printf("\ncouldn't allocate screen buffer");
	}
}

//////////////////////////////////////////////////////////////////////////////

void Plot_Pixel_Fast(int x, int y, unsigned char color)
{
	// plots the pixel in the desired color a little quicker using binary shifting
	// to accomplish the multiplications

	// use the fact that 320*y = 256*y + 64*y = y<<8 + y<<6
	RETRO.framebuffer[(y * SCREEN_WIDTH) + x] = color;
}

//////////////////////////////////////////////////////////////////////////////

unsigned char Get_Pixel(int x, int y)
{
	// gets the pixel from the screen

	return RETRO.framebuffer[(y * SCREEN_WIDTH) + x];
}

//////////////////////////////////////////////////////////////////////////////

void PCX_Delete(pcx_picture_ptr image)
{
	// this function de-allocates the buffer region used for the pcx file load

	free(image->buffer);
}

//////////////////////////////////////////////////////////////////////////////

void PCX_Load(const char *filename, pcx_picture_ptr image, int enable_palette)
{
	// this function loads a pcx file into a picture structure, the actual image
	// data for the pcx file is decompressed and expanded into a secondary buffer
	// within the picture structure, the separate images can be grabbed from this
	// buffer later.  also the header and palette are loaded

	FILE *fp;
	int num_bytes, index;
	long count;
	unsigned char data;
	char *temp_buffer;

	// open the file
	fp = fopen(filename, "rb");

	// load the header
	temp_buffer = (char *)image;

	for (index = 0; index < 128; index++) {
		temp_buffer[index] = getc(fp);
	}

	// load the data and decompress into buffer
	count = 0;

	while (count <= 320 * 200) {
		// get the first piece of data
		data = getc(fp);

		// is this a rle?
		if (data >= 192 && data <= 255) {
			// how many bytes in run?
			num_bytes = data - 192;

			// get the actual data for the run
			data = getc(fp);

			// replicate data in buffer num_bytes times
			while (num_bytes-- > 0) {
				image->buffer[count++] = data;
			}
		} else {
			// actual data, just copy it into buffer at next location
			image->buffer[count++] = data;
		}
	} // end while

	// move to end of file then back up 768 bytes i.e. to begining of palette
	fseek(fp, -768L, SEEK_END);

	// load the pallete into the palette
	for (index = 0; index < 256; index++) {
		// get the red component
		image->palette[index].red = (getc(fp));

		// get the green component
		image->palette[index].green = (getc(fp));

		// get the blue component
		image->palette[index].blue = (getc(fp));
	}

	fclose(fp);

	// change the palette to newly loaded palette if commanded to do so
	if (enable_palette) {
		for (index = 0; index < 256; index++) {
			Set_Palette_Register(index, (RGB_color_ptr)&image->palette[index]);
		}
	}
}

//////////////////////////////////////////////////////////////////////////////

void PCX_Show_Buffer(pcx_picture_ptr image)
{
	// just copy he pcx buffer into the video buffer

	memcpy(RETRO.framebuffer, image->buffer, SCREEN_WIDTH * SCREEN_HEIGHT);
}

//////////////////////////////////////////////////////////////////////////////

void Sprite_Init(sprite_ptr sprite, int x, int y, int ac, int as, int mc, int ms)
{
	// this function initializes a sprite with the sent data

	sprite->x = x;
	sprite->y = y;
	sprite->x_old = x;
	sprite->y_old = y;
	sprite->width = SPRITE_WIDTH;
	sprite->height = SPRITE_HEIGHT;
	sprite->anim_clock = ac;
	sprite->anim_speed = as;
	sprite->motion_clock = mc;
	sprite->motion_speed = ms;
	sprite->curr_frame = 0;
	sprite->state = SPRITE_DEAD;
	sprite->num_frames = 0;
	sprite->background = (unsigned char *)malloc(SPRITE_WIDTH * SPRITE_HEIGHT + 1);

	// set all bitmap pointers to null
	for (int index = 0; index < MAX_SPRITE_FRAMES; index++) {
		sprite->frames[index] = NULL;
	}
}

//////////////////////////////////////////////////////////////////////////////

void Sprite_Delete(sprite_ptr sprite)
{
	// this function deletes all the memory associated with a sprire

	free(sprite->background);

	// now de-allocate all the animation frames
	for (int index = 0; index < MAX_SPRITE_FRAMES; index++) {
		free(sprite->frames[index]);
	}
}

//////////////////////////////////////////////////////////////////////////////

void PCX_Grap_Bitmap(pcx_picture_ptr image,
	sprite_ptr sprite,
	int sprite_frame,
	int grab_x, int grab_y)
{
	// this function will grap a bitmap from the pcx frame buffer. it uses the
	// convention that the 320x200 pixel matrix is sub divided into a smaller
	// matrix of nxn adjacent squares

	int x_off, y_off, x, y;
	unsigned char *sprite_data;

	// first allocate the memory for the sprite in the sprite structure
	sprite->frames[sprite_frame] = (unsigned char *)malloc(SPRITE_WIDTH * SPRITE_HEIGHT + 1);

	// create an alias to the sprite frame for ease of access
	sprite_data = sprite->frames[sprite_frame];

	// now load the sprite data into the sprite frame array from the pcx picture
	x_off = (SPRITE_WIDTH + 1) * grab_x + 1;
	y_off = (SPRITE_HEIGHT + 1) * grab_y + 1;

	// compute starting y address
	y_off = y_off * 320;

	for (y = 0; y < SPRITE_HEIGHT; y++) {
		for (x = 0; x < SPRITE_WIDTH; x++) {
			// get the next byte of current row and place into next position in
			// sprite frame data buffer
			sprite_data[y * SPRITE_WIDTH + x] = image->buffer[y_off + x_off + x];
		}

		// move to next line of picture buffer
		y_off += 320;
	}

	// increment number of frames
	sprite->num_frames++;
}

//////////////////////////////////////////////////////////////////////////////

void Behind_Sprite(sprite_ptr sprite)
{
	// this function scans the background behind a sprite so that when the sprite
	// is draw, the background isnn'y obliterated

	unsigned char *work_back;
	int work_offset = 0, offset, y;

	// alias a pointer to sprite background for ease of access
	work_back = sprite->background;

	// compute offset of background in video buffer
	offset = (sprite->y << 8) + (sprite->y << 6) + sprite->x;

	for (y = 0; y < SPRITE_HEIGHT; y++) {
		// copy the next row out off screen buffer into sprite background buffer
		memcpy(&work_back[work_offset], &RETRO.framebuffer[offset], SPRITE_WIDTH);

		// move to next line in video buffer and in sprite background buffer
		offset += SCREEN_WIDTH;
		work_offset += SPRITE_WIDTH;
	}
}

//////////////////////////////////////////////////////////////////////////////

void Erase_Sprite(sprite_ptr sprite)
{
	// this function replaces the background that was saved from where a sprite
	// was going to be placed

	unsigned char *work_back;
	int work_offset = 0, offset, y;

	// alias a pointer to sprite background for ease of access
	work_back = sprite->background;

	// compute offset of background in video buffer
	offset = (sprite->y << 8) + (sprite->y << 6) + sprite->x;

	for (y = 0; y < SPRITE_HEIGHT; y++) {
		// copy the next row out off screen buffer into sprite background buffer
		memcpy(&RETRO.framebuffer[offset], &work_back[work_offset], SPRITE_WIDTH);

		// move to next line in video buffer and in sprite background buffer
		offset += SCREEN_WIDTH;
		work_offset += SPRITE_WIDTH;
	}
}

//////////////////////////////////////////////////////////////////////////////

void Draw_Sprite(sprite_ptr sprite)
{
	// this function draws a sprite on the screen row by row very quickly
	// note the use of shifting to implement multplication

	unsigned char *work_sprite;
	int work_offset = 0, offset, x, y;
	unsigned char data;

	// alias a pointer to sprite for ease of access
	work_sprite = sprite->frames[sprite->curr_frame];

	// compute offset of sprite in video buffer
	offset = (sprite->y << 8) + (sprite->y << 6) + sprite->x;

	for (y = 0; y < SPRITE_HEIGHT; y++) {
		// copy the next row into the screen buffer using memcpy for speed
		for (x = 0; x < SPRITE_WIDTH; x++) {
			// test for transparent pixel i.e. 0, if not transparent then draw
			if ((data = work_sprite[work_offset + x])) {
				RETRO.framebuffer[offset + x] = data;
			}
		}

		// move to next line in video buffer and in sprite bitmap buffer
		offset += SCREEN_WIDTH;
		work_offset += SPRITE_WIDTH;
	}
}

//////////////////////////////////////////////////////////////////////////////

void Behind_Sprite_VB(sprite_ptr sprite)
{
	// this function scans the background behind a sprite so that when the sprite
	// is draw, the background isnn'y obliterated

	unsigned char *work_back;
	int work_offset = 0, offset, y;

	// alias a pointer to sprite background for ease of access
	work_back = sprite->background;

	// compute offset of background in video buffer
	offset = (sprite->y << 8) + (sprite->y << 6) + sprite->x;

	for (y = 0; y < SPRITE_HEIGHT; y++) {
		// copy the next row out off screen buffer into sprite background buffer
		memcpy(&work_back[work_offset], &RETRO.framebuffer[offset], SPRITE_WIDTH);

		// move to next line in video buffer and in sprite background buffer
		offset += SCREEN_WIDTH;
		work_offset += SPRITE_WIDTH;
	}
}

//////////////////////////////////////////////////////////////////////////////

void Erase_Sprite_VB(sprite_ptr sprite)
{
	// this function replaces the background that was saved from where a sprite
	// was going to be placed

	unsigned char *work_back;
	int work_offset = 0, offset, y;

	// alias a pointer to sprite background for ease of access
	work_back = sprite->background;

	// compute offset of background in video buffer
	offset = (sprite->y << 8) + (sprite->y << 6) + sprite->x;

	for (y = 0; y < SPRITE_HEIGHT; y++) {
		// copy the next row out off screen buffer into sprite background buffer
		memcpy(&RETRO.framebuffer[offset], &work_back[work_offset], SPRITE_WIDTH);

		// move to next line in video buffer and in sprite background buffer
		offset += SCREEN_WIDTH;
		work_offset += SPRITE_WIDTH;
	}
}

//////////////////////////////////////////////////////////////////////////////

void Draw_Sprite_VB(sprite_ptr sprite)
{
	// this function draws a sprite on the screen row by row very quickly
	// note the use of shifting to implement multplication

	unsigned char *work_sprite;
	int work_offset = 0, offset, x, y;
	unsigned char data;

	// alias a pointer to sprite for ease of access
	work_sprite = sprite->frames[sprite->curr_frame];

	// compute offset of sprite in video buffer
	offset = (sprite->y << 8) + (sprite->y << 6) + sprite->x;

	for (y = 0; y < SPRITE_HEIGHT; y++) {
		// copy the next row into the screen buffer using memcpy for speed
		for (x = 0; x < SPRITE_WIDTH; x++) {
			// test for transparent pixel i.e. 0, if not transparent then draw
			if ((data = work_sprite[work_offset + x])) {
				RETRO.framebuffer[offset + x] = data;
			}
		}

		// move to next line in video buffer and in sprite bitmap buffer
		offset += SCREEN_WIDTH;
		work_offset += SPRITE_WIDTH;
	}
}

//////////////////////////////////////////////////////////////////////////////

void Blit_Rect(RECT src_rect, unsigned char *src_buf, int src_pitch, RECT dest_rect, unsigned char *dest_buf, int dest_pitch, int alpha = -1)
{
	unsigned char *src = src_buf;
	unsigned char *dest = dest_buf;

	float dest_xdiff = (dest_rect.right - dest_rect.left) != 0 ? dest_rect.right - dest_rect.left : 1;
	float dest_ydiff = (dest_rect.bottom - dest_rect.top) != 0 ? dest_rect.bottom - dest_rect.top : 1;
	float src_xdelta = (src_rect.right - src_rect.left) / dest_xdiff;
	float src_ydelta = (src_rect.bottom - src_rect.top) / dest_ydiff;

	for (int y = 0; y < dest_ydiff; y++) {
		for (int x = 0; x < dest_xdiff; x++) {
			int src_x = src_rect.left + src_xdelta * x;
			int src_y = src_rect.top + src_ydelta * y;
			int dest_x = dest_rect.left + x;
			int dest_y = dest_rect.top + y;
			if (dest_x >= 0 && dest_x < SCREEN_WIDTH && dest_y >= 0 && dest_y < SCREEN_HEIGHT) {
				if (alpha == -1 || src[src_y * src_pitch + src_x] != alpha) {
					dest[dest_y * dest_pitch + dest_x] = src[src_y * src_pitch + src_x];
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////////

#endif
