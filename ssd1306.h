// I2C for the SSD1306 uses 'intent' followed by 'command' bytes for
// most communications. So most command buffers are alternating 0x80
// and the actual command or value sent.
//
// Once a 'data upload' is configured a single 0x40 is sent followed
// by sufficient raw bytes directly however to cut bandwidth in half
// so buffer updates happen much faster.
//
// It's possible to send smaller 'damage rectangles' as there is RAM
// buffering the entire display on the controller.
static unsigned char gfx_init[] = {
	0x80, 0xae,             // Power off display
	0x80, 0x20, 0x80, 0x00, // Set memory format: Vertical
	0x80, 0xa8, 0x80, 0x3f, // MUX aka "height" of 64 lines
	0x80, 0xd3, 0x80, 0x00, // Reset 'display offset' to 0
	0x80, 0x40,             // Reset 'start line' to 0
	0x80, 0xa1,             // Segment remap: 1
	0x80, 0xc8,             // Invert scan direction
	0x80, 0xda, 0x80, 0x12, // Pin configuration, 0x12 is valid for pironman 5 OLEDs
	0x80, 0x81, 0x80, 0xff, // Contrast to maximum
	0x80, 0xa4,             // Disable the entire screen
	0x80, 0xa6,             // Reset screen to normal
	0x80, 0xd5, 0x80, 0x80, // Clock frequency
	0x80, 0xd9, 0x80, 0xf1, // Precharge period
	0x80, 0xdb, 0x80, 0x30, // Deselect VComH
	0x80, 0x8d, 0x80, 0x14, // Enable charge pump
	0x80, 0xaf,             // Power on display
	0x80, 0x2e,             // Turn off scrolling
};

// Raw 'framebuffer' including bytes needed to update
static unsigned char raw_gfx[ 16 + ( ( 128 * 64 ) / 8 ) ] = {
	0, 0, 0, // Padding to align bit-buffer to 16 bytes
	0x80, 0x21, 0x80, 0x00, 0x80, 0x7f, // Horizontal pixels
	0x80, 0x22, 0x80, 0x00, 0x80, 0x07, // Vertical stripes
	0x40,
	0
};

#define gfx ( raw_gfx + 13 + 3 )
