#include <err.h>
#include <argp.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/timerfd.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <linux/i2c-dev.h>

#include "fonts.h"
#include "masks.h"

#include "monitor_argp.i"

// Integer divisors of 1,000,000,000 are ideal
#define UPDATE_FPS 64

#if ( ( 1000000000 % UPDATE_FPS ) > 0 )
	#warning FPS not set to value that divides exactly into nanoseconds
#endif

struct {
	struct {
		int oled;
		int temp;
		int cpu;
		int disk;
		int timerfd;
	} handles;
	uint64_t overrun;
	long int processors;
} config = {
	.handles.oled    = -1,
	.handles.temp    = -1,
	.handles.cpu     = -1,
	.handles.disk    = -1,
	.handles.timerfd = -1,
	.overrun         = 0,
	.processors      = 1,
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

static void called_every_second( void ) {
	struct timeval time_start, time_end;

	float cpu_used;

	lua_getglobal( vm, "sensors" );

	lua_pushnumber( vm, ( ( lua_Number ) sensor_update_temperature() ) / 1000.0 );
	lua_setfield( vm, -2, "temperature" );

	sensor_update_cpu_usage();

	cpu_used = ( ( config.processors * CPU_WINDOW * 100 ) + stat_idle[ CPU_WINDOW ] - stat_idle[ 0 ] );
	cpu_used /= (float)( config.processors * CPU_WINDOW );
	if ( cpu_used < 0.0) {
		cpu_used = 0.0;
	} else if ( cpu_used > 100.0 ) {
		cpu_used = 100.0;
	}

	lua_pushnumber( vm, cpu_used );
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

	// Displays a sixel-based preview of the OLED display if requested
	//
	// We render it as a 132x72 image, with four blank rows and a two-
	// sixel border around the OLED image so the boundaries are clear.
	if ( arguments.terminal_preview > 0 ) {
		char sixel[ 134 ];

		// 9 = most compatible aspect ratio 1:1 sixel mode
		// We're running a single color so specify 'pure bright white'
		printf( "\033P9;1;0q#1;1;0;100;0" );

		// Top border drawn ┌─────────┐
		sixel[ 0 ] = '?' + 16 + 32;
		memset( sixel + 1, '?' + 16, 130 );
		sixel[ 131 ] = '?' + 16 + 32;
		sixel[ 132 ] = '-'; // sixel equivalent of \n
		sixel[ 133 ] = 0;
		printf( "%s", sixel );

		// Buffer displayed │ content │
		sixel[ 0 ] = '?' + 63;
		sixel[ 1 ] = '?';
		sixel[ 130 ] = '?';
		sixel[ 131 ] = '?' + 63;
		for ( int y = 0; y < 64; y += 6 ) {
			memset( sixel + 2, '?', 128 );
			for ( int yo = 0; yo < 6; yo++ ) {
				// Because we output 6 pixels per stripe
				// we'll have two leftover rows so we'll
				// add the 'bottom border' directly.
				if ( ( y + yo ) >= 64 ) {
					for ( int x = 1; x <= 130; x++ ) {
						sixel[ x ] += 32;
					}
					break;
				}
				int m = 1 << ( ( y + yo ) % 8 );
				int b = 1 << yo;
				int p = ( ( y + yo ) / 8 ) * 128;
				for ( int x = 0; x < 128; x++ ) {
					if ( oled_buffer[ p + x ] & m ) {
						sixel[ x + 2 ] += b;
					}
				}
			}
			printf( "%s", sixel );
		}

		// Exit sixel mode
		printf( "\033\\\n" );
	}
}

// =========================================================== MAIN() STARTUP

void exit_cleanly( int ignored ) {
	exit( 0 );
}

int main( int argc, char **argv ) {
	uint64_t frames;
	struct itimerspec timer_interval = {
		.it_interval.tv_sec = 0,
		.it_interval.tv_nsec = 1000000000 / UPDATE_FPS,
		.it_value.tv_sec = 0,
		.it_value.tv_nsec = 1000000000 / UPDATE_FPS,
	};

	signal( SIGTERM, exit_cleanly );
	signal( SIGINT, exit_cleanly );

	argp_parse( &argp, argc, argv, 0, 0, NULL );

	oled_init();

	sensor_init_temperature();

	sensor_init_cpu_usage();

	sensor_init_disk_usage();

	vm_init();

	config.handles.timerfd = timerfd_create( CLOCK_BOOTTIME, 0 );
	if ( config.handles.timerfd == -1 ) {
		err( EXIT_FAILURE, "timerfd_create" );
	}

	if ( timerfd_settime( config.handles.timerfd, 0, &timer_interval, NULL ) != 0 ) {
		err( EXIT_FAILURE, "timerfd_settime" );
	}

	frames = UPDATE_FPS;

	for (;;) {
		errno = 0;
		if ( read( config.handles.timerfd, &config.overrun, sizeof( config.overrun ) ) < sizeof( config.overrun ) ) {
			if ( errno == EINTR ) {
				continue;
			}
			err( EXIT_FAILURE, "read( timerfd )" );
		}

		if ( ( config.overrun > 1 )
		  && ( arguments.verbosity >= 1 ) ) {
			printf( "Frame drop of %li detected\n", config.overrun - 1 );
		}

		frames += config.overrun;
		while ( frames > UPDATE_FPS ) {
			frames -= UPDATE_FPS;
			called_every_second();
		}
	}

	__builtin_unreachable();
}
