unsigned int sensor_update_temperature( void ) {
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

void sensor_init_temperature( void ) {
	config.handles.temp = open( arguments.temperature.device, O_RDONLY );
	if ( config.handles.temp < 0 ) {
		perror( "opening device to monitor temperature" );
		exit( -1 );
	}
}
