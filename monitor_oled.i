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
static unsigned char oled_ssd1306_init[] = {
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
static unsigned char oled_buffer_raw[ 16 + ( ( 128 * 64 ) / 8 ) ] = {
	0, 0, 0, // Padding to align bit-buffer to 16 bytes
	0x80, 0x21, 0x80, 0x00, 0x80, 0x7f, // Horizontal pixels
	0x80, 0x22, 0x80, 0x00, 0x80, 0x07, // Vertical stripes
	0x40,
	0
};

// Convenience define pointing at the ACTUAL framebuffer data
//
// The framebuffer is 1-bit-per-pixel, in horizontal bands of 8 vertical pixels per byte:
//
//   Byte0Bit0   Byte1Bit0   Byte2Bit0 ...  Byte125Bit0  Byte126Bit0  Byte127Bit0
//   Byte0Bit1   Byte1Bit1   Byte2Bit1 ...  Byte125Bit1  Byte126Bit1  Byte127Bit1
//   Byte0Bit2   Byte1Bit2   Byte2Bit2 ...  Byte125Bit2  Byte126Bit2  Byte127Bit2
//                                     ...
// Byte896Bit5 Byte897Bit5 Byte898Bit5 ... Byte1021Bit5 Byte1022Bit5 Byte1023Bit5
// Byte896Bit6 Byte897Bit6 Byte898Bit6 ... Byte1021Bit6 Byte1022Bit6 Byte1023Bit6
// Byte896Bit7 Byte897Bit7 Byte898Bit7 ... Byte1021Bit7 Byte1022Bit7 Byte1023Bit7

#define oled_buffer ( oled_buffer_raw + 13 + 3 )

void display_off_atexit( void ) {
	int h = open( arguments.oled.device, O_WRONLY );
	ioctl( h, I2C_SLAVE, arguments.oled.address );
	(void)( !write( h, oled_ssd1306_init, 2 ) );
	close( h );
}

void update_oled( void ) {
	(void)( !write( config.handles.oled, oled_buffer_raw + 3, sizeof( oled_buffer_raw ) - 3) );
}

void oled_init( void ) {
	int h;

	config.handles.oled = open( arguments.oled.device, O_WRONLY );
	if ( config.handles.oled < 0 ) {
		perror( "opening I2C device for write access" );
		exit( -1 );
	}

	if ( ioctl( config.handles.oled, I2C_SLAVE, arguments.oled.address ) < 0 ) {
		perror( "ioctl to assign destination I2C address" );
		exit( -1 );
	}

	// So we exit 'cleanly' and don't leave the oled on
	atexit( display_off_atexit );

	if ( write( config.handles.oled, oled_ssd1306_init, sizeof( oled_ssd1306_init ) ) != sizeof( oled_ssd1306_init ) ) {
		perror( "initializing oled" );
		exit( -1 );
	}

	// Update all-black once to avoid 'scroll in' on startup
	update_oled();

	if ( arguments.oled.mask == NULL ) {
		return;
	}

	h = open( arguments.oled.mask, O_RDONLY );
	if ( h == -1 ) {
		perror( "opening custom OLED mask file, using default" );
		return;
	}

	memset( oled_mask_and, ~0, sizeof( oled_mask_and ) );
	memset( oled_mask_or, 0, sizeof( oled_mask_or ) );

	for ( int y = 0; y < 64; y++ ) {
		int p = ( ( y / 8 ) * 128 );
		int b = 1 << ( y % 8 );
		for ( int x = 0; ; x++ ) {
			unsigned char c;
			ssize_t bytes = read( h, &c, 1 );
			if ( bytes == 0 ) {
				close( h );
				return;
			}

			if ( bytes == -1 ) {
				perror( "reading custom OLED mask file" );
				close( h );
				return;
			}

			if ( c == '\n' ) {
				break;
			}

			if ( x < 128 ) {
				if ( c == 'X' ) {
					oled_mask_and[ p + x ] &= ~b;
				} else if ( c == '+' ) {
					oled_mask_or[ p + x ] |= b;
				}
			}
		}
	}

	close( h );
}

// The lower 3 bits of 'y' are ignored
void oled_char( unsigned int x, unsigned int y, unsigned int c, unsigned int size ) {
	const unsigned char *src;
	unsigned char *dst;
	int width;

	if ( ( size - 1 ) > 3 ) {
		return;
	}

	width = font_widths[ size - 1 ];

	if ( ( y > 64 - ( size * 8 ) )
	  || ( x > 128 - width ) ) {
		return;
	}

	dst = oled_buffer + ( ( y & ~7 ) * ( 128 / 8 ) ) + x;

	// Map character codes to font range
	c -= 32;

	// Limit character codes to covered glyphs
	if ( c > ( 126 - 32 ) ) {
		c = 0;
	}

	src = fonts[ size - 1 ] + ( c * width * size );

	while ( size > 0 ) {
		memcpy( dst, src, width );
		dst += 128;
		src += width;
		size--;
	}
}
