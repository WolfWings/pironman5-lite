// grep -E '^/dev' /proc/mounts | cut -d ' ' -f 2
//
// This provides a mostly clean list of all mounts for physical block devices
// that we can iterate over and build a Lua table of disk usage.
void sensor_update_disk_usage( void ) {
	struct statvfs vfs;
	char mounts[ 16384 ];
	ssize_t bytes;
	int p;

	lseek( config.handles.disk, 0, SEEK_SET );

	// This simplifies searching to prepend a newline before the first line
	mounts[ 0 ] = '\n';
	bytes = read( config.handles.disk, mounts + 1, sizeof( mounts ) - 1 );

	if ( bytes < 0 ) {
		warn( "reading /proc/mounts" );
		return;
	}

	if ( bytes > 16383 ) {
		bytes = 16383;
	}
	mounts[ bytes ] = '\0';

	p = 0;
	for (;;) {
		char *found;
		found = memchr( mounts + p, '\n', sizeof( mounts ) - p );
		if ( found == NULL ) {
			break;
		}
		p = found - mounts + 1;
		if ( ( found[ 1 ] != '/' )
		  || ( found[ 2 ] != 'd' )
		  || ( found[ 3 ] != 'e' )
		  || ( found[ 4 ] != 'v' )
		  || ( found[ 5 ] != '/' ) ) {
			continue;
		}
		found = memchr( mounts + p, ' ', sizeof( mounts ) - p );
		if ( found == NULL ) {
			break;
		}
		p = found - mounts + 1;
		found = memchr( mounts + p, ' ', sizeof( mounts ) - p );
		if ( found == NULL ) {
			break;
		}
		*found = '\0';
		if ( statvfs( mounts + p, &vfs ) != 0 ) {
			continue;
		}

		lua_newtable( vm );
		lua_pushnumber( vm, (lua_Number)vfs.f_frsize );
		lua_setfield( vm, -2, "blocksize" );
		lua_pushnumber( vm, (lua_Number)vfs.f_blocks );
		lua_setfield( vm, -2, "total" );
		lua_pushnumber( vm, (lua_Number)vfs.f_bfree );
		lua_setfield( vm, -2, "free" );
		lua_pushnumber( vm, (lua_Number)vfs.f_bavail );
		lua_setfield( vm, -2, "avail" );
		lua_setfield( vm, -2, mounts + p );

		if ( arguments.verbosity >= 3 ) {
			printf( "\tMount point %s: %lu/%lu\n", mounts + p, vfs.f_blocks - vfs.f_bfree, vfs.f_blocks );
		}

		p = found - mounts + 1;
	}
}

void sensor_init_disk_usage( void ) {
	config.handles.disk = open( "/proc/mounts", O_RDONLY );
	if ( config.handles.disk < 0 ) {
		err( EXIT_FAILURE, "opening /proc/mounts" );
	}
}
