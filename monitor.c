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

#include <luajit-2.1/lauxlib.h>
#include <luajit-2.1/lualib.h>

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
	(void)( !write( h, gfx_init, 2 ) );
	close( h );
}

void update_oled( void ) {
	(void)( !write( config.handles.oled, raw_gfx + 3, sizeof( raw_gfx ) - 3) );
}

void oled_init( void ) {
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

	if ( write( config.handles.oled, gfx_init, sizeof( gfx_init ) ) != sizeof( gfx_init ) ) {
		perror( "initializing oled" );
		exit( -1 );
	}

	// Update all-black once to avoid 'scroll in' on startup
	update_oled();

	memcpy( gfx, masks + 1024, 1024 );
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

	dst = gfx + ( ( y & ~7 ) * ( 128 / 8 ) ) + x;

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

// =================================================================== LUAJIT

lua_State *vm = NULL;

const char *__default_script =
	"print( 'Hello, World' )\n";

static const luaL_Reg lj_libs[] = {
	{ "",              luaopen_base },
	{ LUA_LOADLIBNAME, luaopen_package },
	{ LUA_TABLIBNAME,  luaopen_table },
	{ LUA_IOLIBNAME,   luaopen_io },
	{ LUA_OSLIBNAME,   luaopen_os },
	{ LUA_STRLIBNAME,  luaopen_string },
	{ LUA_MATHLIBNAME, luaopen_math },
	{ LUA_DBLIBNAME,   luaopen_debug },
	{ LUA_BITLIBNAME,  luaopen_bit },
	{ LUA_JITLIBNAME,  luaopen_jit },
	{ 0 }
};

void vm_init( void ) {
	const luaL_Reg *lib;

	vm = luaL_newstate();

	if ( vm == NULL ) {
		perror( "initializing LuaJIT" );
		exit( EXIT_FAILURE );
	}

	for (lib = lj_libs; lib->func; lib++) {
		lua_pushcfunction( vm, lib->func );
		lua_pushstring( vm, lib->name );
		lua_call( vm, 1, 0 );
	}

	if ( arguments.script == NULL ) {
		arguments.script = __default_script;
	}

	if ( luaL_loadstring( vm, arguments.script ) != LUA_OK ) {
		perror( "pre-compiling Lua script" );
		exit( EXIT_FAILURE );
	}

	lua_setglobal( vm, "update_frame" );
}

// ========================================================== MAIN EVENT LOOP

void called_every_second( int ignored ) {
	struct timeval time_start, time_end;

	unsigned int temp_c, temp_f, temp_y;
	unsigned int p;
	unsigned char mask;
	unsigned int cpu_used;
	unsigned int cpu_y;

	char buffer[ 32 ];

	time_t clock_time;
	struct tm *clock_tm;

	temp_c = update_temperature();
	// 10,000 final scaling factor for F instead of 1,000 for C
	temp_f = ( temp_c * 18 ) + 320000;
	// Always use C for the graph as it aligns with pixel height well
	temp_y = ( temp_c / 1000 ) - 20;
	if ( temp_y < 0 ) {
		temp_y = 0;
	}
	if ( temp_y > 64 ) {
		temp_y = 64;
	}

	update_cpu();

	cpu_used = ( CPU_WINDOW * 400 ) - ( stat_idle[ 0 ] - stat_idle[ CPU_WINDOW ] );

	cpu_y = ( cpu_used * 60 ) / ( CPU_WINDOW * 400 );

	cpu_used /= ( CPU_WINDOW * 4 );

	if ( cpu_y > 60 ) {
		cpu_y = 60;
	}
	if ( cpu_y < 0 ) {
		cpu_y = 0;
	}

	for ( int y = 0; y < 1024; y += 128 ) {
		memcpy( gfx + y, masks + y + 1024, 89 );

		// Not using memcpy because we need to or the mask back in
		for ( int x = 89; x < 126; x++ ) {
			gfx[ x + y ] = gfx[ x + y + 1 ] | masks[ x + y + 1 + 1024 ];
		}

		gfx[ 126 + y ] = 0x00;
		gfx[ 127 + y ] = masks[ 127 + y + 1024 ];
	}

	p = 1024 - 128 + 126;
	while ( cpu_y >= 8 ) {
		gfx[ p ] = 0xFF;
		p -= 128;
		cpu_y -= 8;
	}
	gfx[ p ] |= ( 0xFF << ( 8 - cpu_y ) );

	p = 1024 - 128;
	while ( temp_y >= 8 ) {
		memset( gfx + p, 0xff, 24 );
		p -= 128;
		temp_y -= 8;
	}
	mask = 0xFF << ( 8 - temp_y );

	// Not using memset because we need to 'or' not write
	for ( int i = 0; i < 24; i++ ) {
		gfx[ p + i ] |= mask;
	}

	for ( int i = 0; i < 1024; i++ ) {
		gfx[ i ] = gfx[ i ] & masks[ i ]; // | masks[ i + 1024 ];
	}

	p = snprintf( buffer, sizeof( buffer ), "%3u.%04uF", temp_f / 10000, temp_f % 10000 );
	for ( int i = 0; i < p; i++ ) {
		oled_char( 24 + ( i * font_widths[ 1 - 1 ] ), 0, buffer[ i ], 1 );
	}

	p = snprintf( buffer, sizeof( buffer ), "%2u.%03uC", temp_c / 1000, temp_c % 1000 );
	for ( int i = 0; i < p; i++ ) {
		oled_char( 24 + ( i * font_widths[ 2 - 1 ] ), 8, buffer[ i ], 2 );
	}

	p = snprintf( buffer, sizeof( buffer ), "%3u%%", cpu_used );
	for ( int i = 0; i < p; i++ ) {
		oled_char( 24 + ( i * font_widths[ 3 - 1 ] ), 24, buffer[ i ], 3 );
	}

	clock_time = time( NULL );
	clock_tm = localtime( &clock_time );
	p = snprintf( buffer, sizeof( buffer ), "%02i:%02i:", clock_tm->tm_hour, clock_tm->tm_min );
	for ( int i = 0; i < p; i++ ) {
		oled_char( 27 + ( i * font_widths[ 2 - 1 ] ), 48, buffer[ i ], 2 );
	}
	p = snprintf( buffer, sizeof( buffer ), "%02i", clock_tm->tm_sec );
	for ( int i = 0; i < p; i++ ) {
		oled_char( 75 + ( i * font_widths[ 1 - 1 ] ), 56, buffer[ i ], 1 );
	}

	lua_getglobal( vm, "update_frame" );

	if ( arguments.verbosity >= 2 ) {
		gettimeofday( &time_start, NULL );
	}

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
	}

	update_oled();
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
