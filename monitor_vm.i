#include <luajit-2.1/lauxlib.h>
#include <luajit-2.1/lualib.h>

lua_State *vm = NULL;

#include "default_script.h"

// oled.print( X, Y, TEXT, SIZE )
//
// Render TEXT starting at X,Y as the upper left corner at SIZE*8 pixels tall
int vm_lua_oled_print( lua_State *state ) {
	size_t len;
	int x            = luaL_checkinteger( state, -4 );
	int y            = luaL_checkinteger( state, -3 );
	const char *text = luaL_checklstring( state, -2, &len );
	int size         = luaL_checkinteger( state, -1 );

	// Only 8, 16, 24, and 32-pixel tall fonts exist
	if ( ( size < 1 ) || ( size > 4 ) ) {
		return 0;
	}

	int width = font_widths[ size - 1 ];

	for ( int i = 0; i < len; i++ ) {
		oled_char( x, y, text[ i ], size );
		x += width;
	}

	return 0;
}

// oled.fillrect( X0, Y0, X1, Y1 )
//
// Fill a rectangle in the framebuffer
int vm_lua_oled_fillrect( lua_State *state ) {
	int x0 = luaL_checkinteger( state, -4 );
	int y0 = luaL_checkinteger( state, -3 );
	int x1 = luaL_checkinteger( state, -2 );
	int y1 = luaL_checkinteger( state, -1 );

	int p;
	int mask;

	// Swap parameters to keep y0 < y1
	if ( y0 > y1 ) {
		int t = y0;
		y0 = y1;
		y1 = t;
	}

	// If the minimum Y is too high, bail
	if ( y0 > 63 ) {
		return 0;
	}

	// If the maximum Y is too low, bail
	if ( y1 < 0 ) {
		return 0;
	}

	// Crop the minimum Y value if needed
	if ( y0 < 0 ) {
		y0 = 0;
	}

	// Crop the maximum Y value if needed
	if ( y1 > 63 ) {
		y1 = 63;
	}

	// And likewise for the X coordinates...
	if ( x0 > x1 ) {
		int t = x0;
		x0 = x1;
		x1 = t;
	}

	if ( x0 > 127 ) {
		return 0;
	}

	if ( x1 < 0 ) {
		return 0;
	}

	if ( x0 < 0 ) {
		x0 = 0;
	}

	if ( x1 > 127 ) {
		x1 = 127;
	}

	// First handle any 'partial stripe' at the minimum Y coordinate
	p = ( y0 / 8 ) * 128;

	// This builds the 'upper' bits to fill with
	//
	// This may only build a partial 'middle' stripe if Y0 and Y1 are
	// close together, and in the process will push 'y0' to become
	// greater than y1, preventing further stripe rendering.
	mask = 0;
	while ( ( y0 & 7 ) && ( y0 <= y1 ) ) {
		mask |= 1 << ( y0 & 7 );
		y0++;
	}
	if ( mask ) {
		for ( int x = x0; x <= x1; x++ ) {
			oled_buffer[ p + x ] |= mask;
		}
	}

	// Now handle the 'lower' partial stripe with the maximum y.
	p = ( y1 / 8 ) * 128;
	mask = 0;
	while ( ( ( y1 & 7 ) != 7 ) && ( y0 <= y1 ) ) {
		mask |= 1 << ( y1 & 7 );
		y1--;
	}
	if ( mask ) {
		for ( int x = x0; x <= x1; x++ ) {
			oled_buffer[ p + x ] |= mask;
		}
	}

	// And now the easy part, any 'full 8 pixel aligned' stripes
	p = ( y0 / 8 ) * 128;
	while ( y0 < y1 ) {
		memset( oled_buffer + p + x0, 0xFF, ( x1 - x0 ) + 1 );
		p += 128;
		y0 += 8;
	}

	return 0;
}

// oled.copyrect( X0, Y0, X1, Y1, XOffset, YOffset )
//
// Copies the rectangle from (X0,Y0)-(X1,Y1) offsetting it by (XOffset,YOffset) pixels
//
// Note that the Y coordinates ignore the lower 3 bits so they are 'stripe aligned'
int vm_lua_oled_copyrect( lua_State *state ) {
	int x0 = luaL_checkinteger( state, -6 );
	int y0 = luaL_checkinteger( state, -5 ) & ~7;
	int x1 = luaL_checkinteger( state, -4 );
	int y1 = luaL_checkinteger( state, -3 ) & ~7;
	int xo = luaL_checkinteger( state, -2 );
	int yo = luaL_checkinteger( state, -1 ) & ~7;

	int s;
	int d;

	if ( y0 > y1 ) {
		int t = y0;
		y0 = y1;
		y1 = t;
	}

	if ( y0 + yo > 63 ) {
		return 0;
	}

	if ( y1 + yo < 0 ) {
		return 0;
	}

	if ( y0 + yo < 0 ) {
		y0 = 0 - yo;
	}

	if ( y1 + yo > 63 ) {
		y1 = 63 - yo;
	}

	if ( x0 > x1 ) {
		int t = x0;
		x0 = x1;
		x1 = t;
	}

	if ( x0 + xo > 127 ) {
		return 0;
	}

	if ( x1 + xo < 0 ) {
		return 0;
	}

	if ( x0 + xo < 0 ) {
		x0 = 0 - xo;
	}

	if ( x1 + xo > 127 ) {
		x1 = 127 - xo;
	}

	x1 = ( x1 - x0 ) + 1;

	s = ( ( y0 / 8 ) * 128 ) + x0;
	d = ( ( ( y0 + yo ) / 8 ) * 128 ) + x0 + xo;
	while ( y0 <= y1 ) {
		memmove( oled_buffer + d, oled_buffer + s, x1 );
		s += 128;
		d += 128;
		y0 += 8;
	}

	return 0;
}

// oled.eraserect( X0, Y0, X1, Y1 )
//
// Near-identical to oled.fillrect however it erases rectangles instead
int vm_lua_oled_eraserect( lua_State *state ) {
	int x0 = luaL_checkinteger( state, -4 );
	int y0 = luaL_checkinteger( state, -3 );
	int x1 = luaL_checkinteger( state, -2 );
	int y1 = luaL_checkinteger( state, -1 );

	int p;
	int mask;

	if ( y0 > y1 ) {
		int t = y0;
		y0 = y1;
		y1 = t;
	}

	if ( y0 > 63 ) {
		return 0;
	}

	if ( y1 < 0 ) {
		return 0;
	}

	if ( y0 < 0 ) {
		y0 = 0;
	}

	if ( y1 > 63 ) {
		y1 = 63;
	}

	if ( x0 > x1 ) {
		int t = x0;
		x0 = x1;
		x1 = t;
	}

	if ( x0 > 127 ) {
		return 0;
	}

	if ( x1 < 0 ) {
		return 0;
	}

	if ( x0 < 0 ) {
		x0 = 0;
	}

	if ( x1 > 127 ) {
		x1 = 127;
	}

	p = ( y0 / 8 ) * 128;
	mask = 0;
	while ( ( y0 & 7 ) && ( y0 <= y1 ) ) {
		mask |= 1 << ( y0 & 7 );
		y0++;
	}
	if ( mask ) {
		mask = ~mask;
		for ( int x = x0; x <= x1; x++ ) {
			oled_buffer[ p + x ] &= mask;
		}
	}

	p = ( y1 / 8 ) * 128;
	mask = 0;
	while ( ( ( y1 & 7 ) != 7 ) && ( y0 <= y1 ) ) {
		mask |= 1 << ( y1 & 7 );
		y1--;
	}
	if ( mask ) {
		mask = ~mask;
		for ( int x = x0; x <= x1; x++ ) {
			oled_buffer[ p + x ] &= mask;
		}
	}

	p = ( y0 / 8 ) * 128;
	while ( y0 < y1 ) {
		memset( oled_buffer + p + x0, 0, ( x1 - x0 ) + 1 );
		p += 128;
		y0 += 8;
	}

	return 0;
}

const struct luaL_Reg oled_lib[] = {
	{ "print", vm_lua_oled_print },
	{ "fillrect", vm_lua_oled_fillrect },
	{ "copyrect", vm_lua_oled_copyrect },
	{ "eraserect", vm_lua_oled_eraserect },
	{ 0 }
};

// A custom stripped-down version of the stock os.date function
//
// Avoids having to expose the entire os. library as several
// functions are dangerous in case the monitoring app runs as
// root.
int vm_lua_os_date( lua_State *state ) {
	const char *s = luaL_checkstring( state, -2 );
	time_t t = luaL_checkinteger( state, -1 );
	struct tm *stm;
	struct tm rtm;
	static char buffer[ 64 ];
	size_t len;

	if (*s == '!') {  /* UTC? */
		s++;  /* Skip '!' */
		stm = gmtime_r(&t, &rtm);
	} else {
		stm = localtime_r(&t, &rtm);
	}

	if (stm == NULL) {  /* Invalid date? */
		lua_pushstring( state, "" );
		return 1;
	}

	if ( s[ 0 ] == '\0' ) {
		lua_pushstring( state, "" );
		return 1;
	}

	len = strftime( buffer, sizeof( buffer ), s, stm );
	if ( len == 0 ) {
		lua_pushstring( state, "" );
		return 1;
	}

	lua_pushlstring( state, buffer, len );

	return 1;
}

void vm_init( void ) {
	vm = luaL_newstate();

	if ( vm == NULL ) {
		perror( "initializing LuaJIT" );
		exit( EXIT_FAILURE );
	}

	lua_pushcfunction( vm, luaopen_base );
	lua_pushstring( vm, "" );
	lua_call( vm, 1, 0 );

	// We don't enable the entire os. library
	// as most of it is very dangerous and we
	// manually pass in the 'time' value with
	// each frame so we only need the os.date
	// function, which we provide a minimized
	// version of here.

	lua_newtable( vm );
	lua_pushcfunction( vm, vm_lua_os_date );
	lua_setfield( vm, -2, "date" );
	lua_setglobal( vm, "os" );

	lua_pushcfunction( vm, luaopen_string );
	lua_pushstring( vm, LUA_STRLIBNAME );
	lua_call( vm, 1, 0 );

	lua_pushcfunction( vm, luaopen_math );
	lua_pushstring( vm, LUA_MATHLIBNAME );
	lua_call( vm, 1, 0 );

	lua_pushcfunction( vm, luaopen_bit );
	lua_pushstring( vm, LUA_BITLIBNAME );
	lua_call( vm, 1, 0 );

	lua_newtable( vm );
	luaL_setfuncs( vm, oled_lib, 0 );
	lua_setglobal( vm, "oled" );

	lua_newtable( vm );
	lua_setglobal( vm, "sensors" );

	if ( luaL_loadstring( vm, ( arguments.script == NULL ) ? __default_script : arguments.script ) != LUA_OK ) {
		perror( "pre-compiling Lua script" );
		exit( EXIT_FAILURE );
	}
}
