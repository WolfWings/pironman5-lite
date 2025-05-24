#include <argp.h>
#include <time.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <linux/i2c-dev.h>

#include "fonts.h"
#include "masks.h"

#include "monitor_argp.i"

struct {
	struct {
		int oled;
		int temp;
		int cpu;
		int disk;
	} handles;
} config = {
	.handles.oled = -1,
	.handles.temp = -1,
	.handles.cpu  = -1,
	.handles.disk = -1,
};

// ============================================================ OLED ROUTINES

#include "monitor_oled.i"

// ============================================================= SCRIPTING VM

#include "monitor_vm.i"

// ================================================================ CPU USAGE

#include "monitor_cpu_usage.i"

// ============================================================== TEMPERATURE

#include "monitor_temperature.i"

// =============================================================== DISK USAGE

#include "monitor_disk_usage.i"

// ========================================================== MAIN EVENT LOOP

void called_every_second( int ignored ) {
	struct timeval time_start, time_end;

	unsigned int cpu_used;

	lua_getglobal( vm, "sensors" );

	lua_pushnumber( vm, ( ( lua_Number ) sensor_update_temperature() ) / 1000.0 );
	lua_setfield( vm, -2, "temperature" );

	sensor_update_cpu_usage();

	cpu_used = ( CPU_WINDOW * 400 ) - ( stat_idle[ 0 ] - stat_idle[ CPU_WINDOW ] );

	lua_pushnumber( vm, ( ( lua_Number ) cpu_used ) / ( 4.0 * CPU_WINDOW ) );
	lua_setfield( vm, -2, "cpu" );

	lua_pushinteger( vm, time( NULL ) );
	lua_setfield( vm, -2, "time" );

	lua_newtable( vm );
	sensor_update_disk_usage();
	lua_setfield( vm, -2, "disk" );

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

	sensor_init_temperature();

	sensor_init_cpu_usage();

	sensor_init_disk_usage();

	vm_init();

	signal( SIGALRM, called_every_second );

	// Update once every second
	setitimer( ITIMER_REAL, &every_second, NULL );

	for (;;) {
		pause();
	}

	__builtin_unreachable();
}
