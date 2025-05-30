// CPU usage (negative idle) is averaged over this many seconds
//
// We still update every second, but this allows for a smoother
// graph displayed on the tiny screen that's more readable.
#define CPU_WINDOW 5

uint64_t stat_idle[ CPU_WINDOW + 1 ];

void sensor_update_cpu_usage( void ) {
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

void sensor_init_cpu_usage( void ) {
	config.handles.cpu = open( "/proc/stat", O_RDONLY );
	if ( config.handles.temp < 0 ) {
		err( EXIT_FAILURE, "Opening /proc/stat to monitor CPU usage" );
	}

	stat_idle[ 0 ] = 0;
	sensor_update_cpu_usage();
	for ( int i = 1; i <= CPU_WINDOW; i++ ) {
		stat_idle[ i ] = stat_idle[ i - 1 ] - 400;
	}
}
