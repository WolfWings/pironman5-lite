#include <argp.h>
#include <time.h>
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
#include "fonts.h"

// No name used as it's purely for convenience defining mostly sequential values
enum {
	ARGP_OPTION_VERBOSE = 'v',

	ARGP_OPTION_LONG_ONLY = 0x10FFFF, // Highest possible unicode code point
	ARGP_OPTION_OLED_DEVICE,
	ARGP_OPTION_OLED_ADDRESS,
	ARGP_OPTION_TEMPERATURE_DEVICE,
	ARGP_OPTION_AUX_FAN_GPIO,
	ARGP_OPTION_AUX_FAN_THRESHOLD_ON,
	ARGP_OPTION_AUX_FAN_THRESHOLD_OFF,
	ARGP_OPTION_PINCTRL,
};

const char *argp_program_version = "v" __PACKAGE_VERSION__;
const char *argp_program_bug_address = "https://github.com/wolfwings/pironman5-lite";

static struct argp_option options[] = {
	{ .name = "verbose"
	, .key = ARGP_OPTION_VERBOSE, .arg = 0
	, .flags = 0, .doc = "Increase output verbosity" },

	{ .name = "oled-device"
	, .key = ARGP_OPTION_OLED_DEVICE, .arg = "DEVICE"
	, .flags = 0, .doc = "OLED I2C device; defaults to /dev/i2c-1" },

	{ .name = "oled-address"
	, .key = ARGP_OPTION_OLED_ADDRESS, .arg = "ADDRESS"
	, .flags = 0, .doc = "OLED I2C address; defaults to 0x3C" },

	{ .name = "temperature-device"
	, .key = ARGP_OPTION_TEMPERATURE_DEVICE, .arg = "DEVICE"
	, .flags = 0, .doc = "Temperature monitoring device; defaults to /sys/class/thermal/thermal_zone0/temp" },

	{ .name = "aux-fan-gpio"
	, .key = ARGP_OPTION_AUX_FAN_GPIO, .arg = "PIN"
	, .flags = 0, .doc = "GPIO pin to enable additional cooling fans at the temperature threshold; defaults to 6, set to -1 to disable" },

	{ .name = "aux-fan-threshold-on"
	, .key = ARGP_OPTION_AUX_FAN_THRESHOLD_ON, .arg = "TEMPERATURE"
	, .flags = 0, .doc = "Temperature to enable auxiliary cooling fan(s); defaults to 70C, may specify F suffix" },

	{ .name = "aux-fan-threshold-off"
	, .key = ARGP_OPTION_AUX_FAN_THRESHOLD_OFF, .arg = "TEMPERATURE"
	, .flags = 0, .doc = "Temperature to disable auxiliary cooling fan(s); defaults to 60C, may specify F suffix" },

	{ .name = "pinctrl"
	, .key = ARGP_OPTION_PINCTRL, .arg = "FILENAME"
	, .flags = 0, .doc = "Filename to execute 'pinctrl' equivalent program to modify GPIO pins; defaults to /usr/bin/pinctrl" },

	{ 0 }
};

struct {
	struct {
		char *device;
		uint32_t address;
	} oled;
	struct {
		char *device;
	} temperature;
	struct {
		int pin;
		unsigned int t_on;
		unsigned int t_off;
	} fan;
	char *pinctrl;
	unsigned int verbosity;
} arguments = {
	.oled.device        = "/dev/i2c-1",
	.oled.address       = 0x3C,
	.temperature.device = "/sys/class/thermal/thermal_zone0/temp",
	.fan.pin            = 6,

	// Temp thresholds are based on raw kernel readings which are C*1000
	.fan.t_on           = 70000,
	.fan.t_off          = 60000,
	.verbosity          = 0,
	.pinctrl            = "/usr/bin/pinctrl",
};


static error_t parse_opt( int key, char *arg, struct argp_state *state ) {
	char *endptr;

	switch( key ) {
	case ARGP_OPTION_VERBOSE:
		arguments.verbosity++;
		break;

	case ARGP_OPTION_TEMPERATURE_DEVICE:
		arguments.temperature.device = arg;
		break;

	case ARGP_OPTION_OLED_DEVICE:
		arguments.oled.device = arg;
		break;

	case ARGP_OPTION_OLED_ADDRESS:
		errno = 0;
		arguments.oled.address = strtoul( arg, NULL, 0 );

		if ( errno != 0 ) {
			perror( "parsing OLED I2C address" );
			argp_usage( state );
		}

		break;

	case ARGP_OPTION_AUX_FAN_GPIO:
		errno = 0;
		arguments.fan.pin = strtol( arg, NULL, 0 );

		if ( errno != 0 ) {
			perror( "parsing auxiliary cooling fan GPIO pin number" );
			argp_usage( state );
		}

		break;

	case ARGP_OPTION_AUX_FAN_THRESHOLD_ON:
		errno = 0;
		arguments.fan.t_on = strtoul( arg, &endptr, 10 ) * 1000;

		if ( errno != 0 ) {
			perror( "parsing auxiliary cooling fan \"on\" temperature" );
			argp_usage( state );
		}

		if ( ( *endptr == 'F' )
		  || ( *endptr == 'f' ) ) {
			arguments.fan.t_on = ( ( arguments.fan.t_on - 32000 ) * 10 ) / 18;
		}

		break;

	case ARGP_OPTION_AUX_FAN_THRESHOLD_OFF:
		errno = 0;
		arguments.fan.t_off = strtoul( arg, &endptr, 10 ) * 1000;

		if ( errno != 0 ) {
			perror( "parsing auxiliary cooling fan \"off\" temperature" );
			argp_usage( state );
		}

		if ( ( *endptr == 'F' )
		  || ( *endptr == 'f' ) ) {
			arguments.fan.t_off = ( ( arguments.fan.t_off - 32000 ) * 10 ) / 18;
		}

		break;

	case ARGP_OPTION_PINCTRL:
		arguments.pinctrl = arg;
		break;

	case ARGP_KEY_ARG:
		argp_usage( state );
		break;

	case ARGP_KEY_END:
		if ( state->arg_num > 0 ) {
			argp_usage( state );
		}

		if ( arguments.fan.t_off >= arguments.fan.t_on ) {
			printf( "Auxiliary fan \"off\" temperature is not less than the \"on\" temperature.\n" );
			argp_usage( state );
		}

		break;

	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

static struct argp argp = {
	.options = options,
	.parser = parse_opt,
	.args_doc = "",
	.doc = "monitoring utility for display on tiny I2C OLED screens",
};

struct {
	struct {
		int oled;
		int temp;
		int cpu;
	} handles;
	struct {
		char *fan_pin;
	} strings;
	struct {
		int fan;
	} status;
	sigset_t signal_mask;
} config = {
	.status.fan       = -1,
	.strings.fan_pin  = NULL,
	.handles.oled     = -1,
	.handles.temp     = -1,
	.handles.cpu      = -1,
};

// ============================================================ OLED ROUTINES

void handler_terminate( int ignored ) {
	exit( 0 );
}

void display_off_atexit( void ) {
	printf( "AtExit Called\n" );
	if ( config.handles.oled >= 0 ) {
		(void)( !write( config.handles.oled, gfx_init, 2 ) );
	} else {
		int h = open( arguments.oled.device, O_WRONLY );
		ioctl( h, I2C_SLAVE, arguments.oled.address );
		(void)( !write( h, gfx_init, 2 ) );
		close( h );
	}
}

void update_oled( void ) {
	(void)( !write( config.handles.oled, raw_gfx + 3, sizeof( raw_gfx ) - 3) );
}

void oled_init( void ) {
	config.handles.oled = open( arguments.oled.device, O_RDWR );
	if ( config.handles.oled < 0 ) {
		perror( "opening I2C device for read-write access" );
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
	int src;
	int dst;
	int width;

	if ( ( size - 1 ) > 3 ) {
		return;
	}

	width = font_widths[ size - 1 ];

	if ( ( y > 64 - ( size * 8 ) )
	  || ( x > 128 - width ) ) {
		return;
	}

	dst = ( ( y / 8 ) * 128 ) + x;

	if ( ( c < 33 ) || ( c > 126 ) ) {
		c = 32;
	}

	src = ( c - 32 ) * width * size;
	for ( int l = 0; l < size; l++ ) {
		memcpy( gfx + dst + ( l * 128 ), fonts[ size - 1 ] + src + ( l * width ), width );
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
	int bytes;

	config.handles.temp = open( arguments.temperature.device, O_RDONLY );
	if ( config.handles.temp < 0 ) {
		perror( "opening device to monitor temperature" );
		exit( -1 );
	}

	bytes = snprintf( NULL, 0, "%u", arguments.fan.pin );
	config.strings.fan_pin = malloc( bytes + 1 );
	if ( config.strings.fan_pin == NULL ) {
		perror( "allocating string buffer for fan GPIO pin, disabling aux fan control" );
		arguments.fan.pin = -1;
	}
	sprintf( config.strings.fan_pin, "%u", arguments.fan.pin );
}

// ========================================================== MAIN EVENT LOOP

void called_every_second( int ignored ) {
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

	if ( arguments.fan.pin > -1 ) {
		if ( temp_c > arguments.fan.t_on ) {
			if ( config.status.fan != 1 ) {
				config.status.fan = 1;
				if ( fork() == 0 ) {
					char *cmdargv[] = { arguments.pinctrl, "set", config.strings.fan_pin, "op", "dh", NULL };
					execvp( arguments.pinctrl, cmdargv );
					exit( 0 );
				}
			}
		} else if ( temp_c < arguments.fan.t_off ) {
			if ( config.status.fan != 0 ) {
				config.status.fan = 0;
				if ( fork() == 0 ) {
					char *cmdargv[] = { arguments.pinctrl, "set", config.strings.fan_pin, "op", "dl", NULL };
					execvp( arguments.pinctrl, cmdargv );
					exit( 0 );
				}
			}
		}
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

	update_oled();
}

// =========================================================== MAIN() STARTUP

void exit_cleanly( int ignored ) {
	exit( 0 );
}

int main( int argc, char **argv ) {
	// Disables 'zombie' processes
	struct sigaction no_zombies = {
		.sa_handler = SIG_DFL,
		.sa_flags = SA_NOCLDWAIT,
	};

	struct itimerval every_second = {
		.it_interval.tv_sec = 1,
		.it_interval.tv_usec = 0,
		.it_value.tv_sec = 1,
		.it_value.tv_usec = 0,
	};

	signal( SIGTERM, exit_cleanly );
	signal( SIGINT, exit_cleanly );

	sigaction( SIGCHLD, &no_zombies, NULL );

	argp_parse( &argp, argc, argv, 0, 0, NULL );

	oled_init();

	temperature_init();

	cpuidle_init();

	signal( SIGALRM, called_every_second );

	// Update once every second
	setitimer( ITIMER_REAL, &every_second, NULL );

	for (;;) {
		pause();
	}

	__builtin_unreachable();
}
