#include <argp.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

const char *argp_program_version = "v" __PACKAGE_VERSION__;
const char *argp_program_bug_address = "https://github.com/wolfwings/pironman5-lite";

static struct argp_option options[] = {
	{ .name = "device",  .key = 'd', .arg = "DEVICE", .flags = 0, .doc = "SPI device, defaults to /dev/spidev0.0" },
	{ .name = "count",   .key = 'c', .arg = "COUNT",  .flags = 0, .doc = "Number of LEDs, defaults to 4" },
	{ .name = "verbose", .key = 'v', .arg = 0,        .flags = 0, .doc = "Increase output verbosity" },
	{ 0 }
};

struct arguments {
	char *device;
	unsigned int count;
	unsigned int *colors;
	unsigned int color_size;
	unsigned int verbosity;
};

unsigned int hex3to6( unsigned int i ) {
	return ( ( i & 0x00F ) * 0x11 )
	     + ( ( i & 0x0F0 ) * 0x110 )
	     + ( ( i & 0xF00 ) * 0x1100 );
}

static error_t parse_opt( int key, char *arg, struct argp_state *state ) {
	struct arguments *arguments = state->input;
	switch( key ) {
	case 'v':
		arguments->verbosity++;
		break;

	case 'd':
		arguments->device = arg;
		break;

	case 'c':
		errno = 0;
		arguments->count = strtoul( arg, NULL, 10 );

		if ( errno != 0 ) {
			perror( "parsing color count argument as base-10 unsigned integer" );
			argp_usage( state );
		}

		if ( arguments->color_size < arguments->count ) {
			arguments->colors = realloc( arguments->colors, arguments->count * sizeof( unsigned int ) );

			if ( arguments->colors == NULL ) {
				perror( "increasing color argument storage to match new count" );
				argp_usage( state );
			}

			arguments->color_size = arguments->count;
		}

		break;

	case ARGP_KEY_ARG:
		if ( state->arg_num >= arguments->count ) {
			argp_usage( state );
		}

		// Reset to zero to detect errors from strtoul
		errno = 0;

		switch( strnlen( arg, 9 ) ) {

		case 3: // RGB
			arguments->colors[ state->arg_num ] = hex3to6( strtoul( arg, NULL, 16 ) );
			break;

		case 4: // #RGB
			if ( arg[0] != '#' ) {
				argp_usage( state );
			}

			arguments->colors[ state->arg_num ] = hex3to6( strtoul( &arg[1], NULL, 16 ) );
			break;

		case 5: // 0xRGB
			if ( ( arg[0] != '0' )
			  || ( arg[1] != 'x' ) ) {
				argp_usage( state );
			}

			arguments->colors[ state->arg_num ] = hex3to6( strtoul( &arg[2], NULL, 16 ) );
			break;

		case 6: // RRGGBB
			arguments->colors[ state->arg_num ] = strtoul( arg, NULL, 16 );
			break;

		case 7: // #RRGGBB
			if ( arg[0] != '#' ) {
				argp_usage( state );
			}

			arguments->colors[ state->arg_num ] = strtoul( &arg[1], NULL, 16 );
			break;

		case 8: // 0xRRGGBB
			if ( ( arg[0] != '0' )
			  || ( arg[1] != 'x' ) ) {
				argp_usage( state );
			}

			arguments->colors[ state->arg_num ] = strtoul( &arg[2], NULL, 16 );
			break;

		default:
			argp_usage( state );

		}

		if ( errno != 0 ) {
			perror( "parsing hex color string" );
			argp_usage( state );
		}

		break;

	case ARGP_KEY_END:
		if ( state->arg_num < arguments->count ) {
			if ( state->arg_num == 0 ) {
				argp_usage( state );
			}

			for ( int i = state->arg_num; i < arguments->color_size; i++ ) {
				arguments->colors[ i ] = arguments->colors[ i % state->arg_num ];
			}
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
	.args_doc = "HEXCOLOR [HEXCOLOR [...] ]",
	.doc = "SPI-based LED control utility for WS2812 strings",
};

// Chosen to be an exact integer divisor of the default 125Mhz SPI clock
static const uint32_t spi_speed = 3125000;

static uint8_t *ledbuffer;

static uint8_t bits[ 4 ] = { 0x22, 0x26, 0x62, 0x66 };

int main( int argc, char **argv ) {
	struct arguments arguments;

	arguments.verbosity = 0;
	arguments.count = 4;
	arguments.colors = calloc( sizeof( unsigned int ), arguments.count );
	arguments.color_size = arguments.count;
	arguments.device = "/dev/spidev0.0";
	if ( arguments.colors == NULL ) {
		perror( "allocating memory for LED color parsing" );
		exit( EXIT_FAILURE );
	}

	argp_parse( &argp, argc, argv, 0, 0, &arguments );

	if ( arguments.verbosity > 0 ) {
		printf( "Using SPI device: %s", arguments.device );
		printf( " for %u LEDs of values:\n", arguments.count );
		for ( int i = 0; i < arguments.count; i++ ) {
			printf( "\t#%06X\n", arguments.colors[ i ] );
		}
	}

	int spih = open( "/dev/spidev0.0", O_WRONLY );

	if ( spih < 0 ) {
		perror( "opening SPI device" );
		exit( EXIT_FAILURE );
	}

	if ( ioctl( spih, SPI_IOC_WR_MAX_SPEED_HZ, &spi_speed ) < 0 ) {
		perror( "setting SPI frequency" );
		exit( EXIT_FAILURE );
	}

	ledbuffer = calloc( 1, ( arguments.count * 12 ) + 8 );

	if ( ledbuffer == NULL ) {
		perror( "allocating memory for SPI buffer" );
		exit( EXIT_FAILURE );
	}

	for ( int i = 0; i < arguments.count; i++ ) {
		unsigned int c = arguments.colors[ i ];
		// Translate the 0xRRGGBB to wire data order of GGRRBB
		// with bits in 76543210 order.
		ledbuffer[ ( i * 12 ) +  4 ] = bits[ ( c >> 14 ) & 3 ];
		ledbuffer[ ( i * 12 ) +  5 ] = bits[ ( c >> 12 ) & 3 ];
		ledbuffer[ ( i * 12 ) +  6 ] = bits[ ( c >> 10 ) & 3 ];
		ledbuffer[ ( i * 12 ) +  7 ] = bits[ ( c >>  8 ) & 3 ];
		ledbuffer[ ( i * 12 ) +  8 ] = bits[ ( c >> 22 ) & 3 ];
		ledbuffer[ ( i * 12 ) +  9 ] = bits[ ( c >> 20 ) & 3 ];
		ledbuffer[ ( i * 12 ) + 10 ] = bits[ ( c >> 18 ) & 3 ];
		ledbuffer[ ( i * 12 ) + 11 ] = bits[ ( c >> 16 ) & 3 ];
		ledbuffer[ ( i * 12 ) + 12 ] = bits[ ( c >>  6 ) & 3 ];
		ledbuffer[ ( i * 12 ) + 13 ] = bits[ ( c >>  4 ) & 3 ];
		ledbuffer[ ( i * 12 ) + 14 ] = bits[ ( c >>  2 ) & 3 ];
		ledbuffer[ ( i * 12 ) + 15 ] = bits[ ( c >>  0 ) & 3 ];
	}

	if ( arguments.verbosity >= 2 ) {
		printf( "Planned SPI buffer:\n" );
		for ( int i = 0; i < 4; i++ ) {
			printf( "%c", ( i == 0 ) ? '\t' : ':' );
			printf( "0x%02X", ledbuffer[ i ] );
		}
		for ( int i = 4; i < ( arguments.count * 12 ) + 8; i++ ) {
			printf( "%s", ( ( i & 7 ) == 4 ) ? "\n\t" : ":" );
			printf( "0x%02X", ledbuffer[ i ] );
		}
		printf( "\n" );
	}

	for (;;) {
		ssize_t written = write( spih, ledbuffer, ( arguments.count * 12 ) + 8 );

		if ( written == ( ( arguments.count * 12 ) + 8 ) ) {
			break;
		}

		if ( written < 0 ) {
			perror( "writing raw LED buffer to SPI device" );
			exit( EXIT_FAILURE );
		}

		printf( "Incomplete SPI buffer write, retrying\n" );
		usleep( 1000 ); // Force 'latch' w/ 1ms pause
	}

	close( spih );

	return 0;
}
