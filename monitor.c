#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <linux/i2c-dev.h>

#include "ssd1306.h"
#include "masks.h"
#include "font.h"

struct {
	struct {
		int oled;
		int temp;
		int cpu;
		int signals;
	} handles;
	sigset_t signal_mask;
} config = {
	.handles.oled    = -1,
	.handles.temp    = -1,
	.handles.cpu     = -1,
	.handles.signals = -1,
};

void signalfd_add( int signal ) {
	sigaddset( &config.signal_mask, SIGALRM );
	sigprocmask( SIG_SETMASK, &config.signal_mask, NULL );
	signalfd( config.handles.signals, &config.signal_mask, 0 );
}

// ============================================================ OLED ROUTINES

void handler_terminate( int ignored ) {
	exit( 0 );
}

void display_off_atexit( void ) {
	if ( config.handles.oled >= 0 ) {
		(void)( !write( config.handles.oled, gfx_init, 2 ) );
	}
}

void update_oled( void ) {
	(void)( !write( config.handles.oled, raw_gfx + 3, sizeof( raw_gfx ) - 3) );
}

void oled_init( void ) {
	config.handles.oled = open( "/dev/i2c-1", O_RDWR );
	if ( config.handles.oled < 0 ) {
		perror( "opening I2C device for read-write access" );
		exit( -1 );
	}

	if ( ioctl( config.handles.oled, I2C_SLAVE, (uint32_t)0x3c ) < 0 ) {
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
void oled_char( unsigned int x, unsigned int y, unsigned int c ) {
	int src, dst;

	if ( ( y > 64 - 32 )
	  || ( x > 128 - 16 ) ) {
		return;
	}

	dst = ( ( y / 8 ) * 128 ) + x;

	if ( c < 13 ) {
		src = ( 16 * 32 / 8 ) * c;

		for ( int i = 0; i < 4; i++ ) {
			memcpy( gfx + dst + ( i * 128 ), font + src + ( i * 16 ), 16 );
		}
	} else {
		for ( int i = 0; i < 4; i++ ) {
			memset( gfx + dst + ( i * 128 ), 0, 16 );
		}
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
	config.handles.temp = open( "/sys/class/thermal/thermal_zone0/temp", O_RDONLY );
	if ( config.handles.temp < 0 ) {
		perror( "Opening /sys/class/thermal/thermal_zone0/temp to monitor temperature" );
		exit( -1 );
	}
}

// ========================================================== MAIN EVENT LOOP

void called_every_second( void ) {
	unsigned int temp_c, temp_f, temp_y;
	unsigned int p;
	unsigned char mask;
	unsigned int cpu_used;
	unsigned int cpu_y;

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

	temp_f /= 10000;
	if ( temp_f > 99 ) {
		oled_char( 24, 0, 1 );
	}
	if ( temp_f > 9 ) {
		oled_char( 24 + 16, 0, ( temp_f / 10 ) % 10 );
	}
	oled_char( 24 + 32, 0, temp_f % 10 );
	oled_char( 24 + 48, 0, 11 );

//	if ( cpu_used > 99 ) {
//		oled_char( 24, 0, 1 );
//	}
//	if ( cpu_used > 9 ) {
//		oled_char( 24 + 16, 0, ( cpu_used / 10 ) % 10 );
//	}
//	oled_char( 24 + 32,  0, cpu_used % 10 );
//	oled_char( 24 + 48,  0, 12 );

	oled_char( 24 + 16, 32, ( temp_c / 1000 ) / 10 );
	oled_char( 24 + 32, 32, ( temp_c / 1000 ) % 10 );
	oled_char( 24 + 48, 32, 10 );

	update_oled();
}

// =========================================================== MAIN() STARTUP

int main( void ) {
	ssize_t bytes;
	struct signalfd_siginfo sig;

	sigemptyset( &config.signal_mask );
	config.handles.signals = signalfd( -1, &config.signal_mask, 0 );
	if ( config.handles.signals < 0 ) {
		perror( "allocating signalfd for handling signals in event loop" );
	}

	struct itimerval every_second = {
		.it_interval.tv_sec = 1,
		.it_interval.tv_usec = 0,
		.it_value.tv_sec = 1,
		.it_value.tv_usec = 0,
	};

	oled_init();

	temperature_init();

	cpuidle_init();

	// Mark signals to capture for the event loop
	signalfd_add( SIGINT );  // Ctrl+C
	signalfd_add( SIGTERM ); // Default 'kill'
	signalfd_add( SIGALRM ); // The main one we use

	// Update once every second
	setitimer( ITIMER_REAL, &every_second, NULL );

	for (;;) {
		bytes = read( config.handles.signals, &sig, sizeof( sig ) );

		if ( bytes != sizeof( sig ) ) {
			perror( "reading signalfd buffer" );
			exit( EXIT_FAILURE );
		}

		if ( sig.ssi_signo == SIGALRM ) {
			called_every_second();
			continue;
		}

		if ( sig.ssi_signo == SIGINT ) {
			break;
		}

		if ( sig.ssi_signo == SIGTERM ) {
			break;
		}

		printf( "Unknown signal received: %s\n", strsignal( sig.ssi_signo ) );
	}

	return 0;
}
