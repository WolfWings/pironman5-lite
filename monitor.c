#include <argp.h>
#include <time.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#include "fonts.h"
#include "ssd1306.h"
#include "masks.h"

#include "monitor_argp.h"

struct {
	struct {
		int oled;
		int temp;
		int cpu;
	} handles;
} config = {
	.handles.oled     = -1,
	.handles.temp     = -1,
	.handles.cpu      = -1,
};

// ============================================================ OLED ROUTINES

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

// ================================================================ CPU USAGE

// CPU usage (negative idle) is averaged over this many seconds
//
// We still update every second, but this allows for a smoother
// graph displayed on the tiny screen that's more readable.
#define CPU_WINDOW 5

uint64_t stat_idle[ CPU_WINDOW + 1 ];

void update_cpu( void ) {
	char buffer[ 64 ];
	unsigned int index;
	unsigned int spaces;
	uint64_t idle;

	// We don't need to check if the handle is valid as we'll leave on error reading
	lseek( config.handles.cpu, 0, SEEK_SET );

	// Minimal possible string is "cpu  0 0 0 0 " so abort on shorts + errors
	if ( read( config.handles.cpu, buffer, sizeof( buffer ) ) < 13 ) {
		return;
	}

	// Skip over the first five spaces ignoring all other characters
	index = 0;
	spaces = 0;
	do {
		if ( buffer[ index ] == ' ' ) {
			spaces++;
			if ( spaces >= 5 ) {
				break;
			}
		}
		index++;
	} while ( index < 64 );

	// Build the CPU usage value
	idle = 0;

	while ( index < 64 ) {
		index++;
		if ( buffer[ index ] == ' ' ) {
			break;
		}
		idle *= 10;
		idle += buffer[ index ] - '0';
	};

	// Now we can shift the usage window over and add the new value
	for ( int i = CPU_WINDOW; i > 0; i-- ) {
		stat_idle[ i ] = stat_idle[ i - 1 ];
	}
	stat_idle[ 0 ] = idle;
}

void cpuidle_init( void ) {
	config.handles.cpu = open( "/proc/stat", O_RDONLY );
	if ( config.handles.temp < 0 ) {
		perror( "Opening /proc/stat to monitor CPU usage" );
		exit( -1 );
	}

	stat_idle[ 0 ] = 0;
	update_cpu();
	for ( int i = 1; i <= CPU_WINDOW; i++ ) {
		stat_idle[ i ] = stat_idle[ i - 1 ] - 400;
	}
}

// ============================================================== TEMPERATURE

unsigned int update_temperature( void ) {
	char buffer[ 64 ];
	ssize_t bytes;
	unsigned int tally;

	lseek( config.handles.temp, 0, SEEK_SET );
	bytes = read( config.handles.temp, buffer, sizeof( buffer ) );
	if ( bytes < 0 ) {
		return -1;
	}

	tally = 0;
	for ( int i = 0; i < bytes - 1; i++ ) {
		tally = ( tally * 10 ) + ( buffer[ i ] - '0' );
	}

	return tally;
}

void temperature_init( void ) {
	config.handles.temp = open( arguments.temperature.device, O_RDONLY );
	if ( config.handles.temp < 0 ) {
		perror( "opening device to monitor temperature" );
		exit( -1 );
	}
}

// ============================================================= SCRIPTING VM

#include "monitor_vm.h"

// ========================================================== MAIN EVENT LOOP

void called_every_second( int ignored ) {
	struct timeval time_start, time_end;

	unsigned int cpu_used;

	lua_getglobal( vm, "sensors" );

	lua_pushnumber( vm, ( ( lua_Number ) update_temperature() ) / 1000.0 );
	lua_setfield( vm, -2, "temperature" );

	update_cpu();

	cpu_used = ( CPU_WINDOW * 400 ) - ( stat_idle[ 0 ] - stat_idle[ CPU_WINDOW ] );

	lua_pushnumber( vm, ( ( lua_Number ) cpu_used ) / ( 4.0 * CPU_WINDOW ) );
	lua_setfield( vm, -2, "cpu" );

	lua_pushinteger( vm, time( NULL ) );
	lua_setfield( vm, -2, "time" );

	lua_setglobal( vm, "sensors" );

	if ( arguments.verbosity >= 2 ) {
		gettimeofday( &time_start, NULL );
	}

	// Per-frame function is kept on top of the stack
	lua_pushvalue( vm, 1 );

	if ( lua_pcall( vm, 0, 0, 0 ) != LUA_OK ) {
		printf( "error in lua script\n" );
		exit( EXIT_FAILURE );
	}

	if ( arguments.verbosity >= 2 ) {
		gettimeofday( &time_end, NULL );
		time_end.tv_sec -= time_start.tv_sec;
		time_end.tv_usec -= time_start.tv_usec;
		while ( time_end.tv_usec < 0 ) {
			time_end.tv_usec += 1000000;
			time_end.tv_sec -= 1;
		}
		printf( "%li.%06li seconds taken\n", time_end.tv_sec, time_end.tv_usec );
		printf( "%i elements on VM stack\n", lua_gettop( vm ) );
	}

	for ( int i = 0; i < 1024; i++ ) {
		oled_buffer[ i ] = ( oled_buffer[ i ] | oled_mask_or[ i ] ) & oled_mask_and[ i ];
	}

	update_oled();

	// Directly generate UTF-8 braille encoding of the OLED display
	if ( arguments.verbosity >= 3 ) {
		char bufferh[ ( 64 * 3 ) + 1 ] = { [ 64 * 3 ] = '\0' };
		char bufferl[ ( 64 * 3 ) + 1 ] = { [ 64 * 3 ] = '\0' };
		for ( int y = 0; y < 64; y += 8 ) {
			int p = ( y / 8 ) * 128;
			for ( int x = 0; x < 128; x += 2 ) {
				int cx = ( x / 2 ) * 3;
				unsigned char cu, cl;
				unsigned char c0, c1;
				c0 = oled_buffer[ p + x ];
				c1 = oled_buffer[ p + x + 1];
				cu = ( ( c0 & 0x07 ) >> 0 ) | ( ( c1 & 0x07 ) << 3 );
				cl = ( ( c0 & 0x70 ) >> 4 ) | ( ( c1 & 0x70 ) >> 1 );
				if ( c0 & 0x08 ) cu |= 0x40;
				if ( c0 & 0x80 ) cl |= 0x40;
				if ( c1 & 0x08 ) cu |= 0x80;
				if ( c1 & 0x80 ) cl |= 0x80;
				bufferh[ cx     ] = 0xE2;
				bufferh[ cx + 1 ] = 0xA0 | ( cu >> 6 );
				bufferh[ cx + 2 ] = 0x80 | ( cu & 0x3F );
				bufferl[ cx     ] = 0xE2;
				bufferl[ cx + 1 ] = 0xA0 | ( cl >> 6 );
				bufferl[ cx + 2 ] = 0x80 | ( cl & 0x3F );
			}
			printf( "%s\n%s\n", bufferh, bufferl );
		}
	}
}

// =========================================================== MAIN() STARTUP

void exit_cleanly( int ignored ) {
	exit( 0 );
}

int main( int argc, char **argv ) {
	struct itimerval every_second = {
		.it_interval.tv_sec = 1,
		.it_interval.tv_usec = 0,
		.it_value.tv_sec = 1,
		.it_value.tv_usec = 0,
	};

	signal( SIGTERM, exit_cleanly );
	signal( SIGINT, exit_cleanly );

	argp_parse( &argp, argc, argv, 0, 0, NULL );

	oled_init();

	temperature_init();

	cpuidle_init();

	vm_init();

	signal( SIGALRM, called_every_second );

	// Update once every second
	setitimer( ITIMER_REAL, &every_second, NULL );

	for (;;) {
		pause();
	}

	__builtin_unreachable();
}
