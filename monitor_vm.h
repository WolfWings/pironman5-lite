#include <luajit-2.1/lauxlib.h>
#include <luajit-2.1/lualib.h>

lua_State *vm = NULL;

static const char *__default_script =
	"oled.print( 0,  0, string.format( '%3.3fC', sensors.temperature ), 2 )"
"\n"	"oled.print( 0, 16, string.format( '%3.4fF', ( sensors.temperature * 1.8 ) + 32 ), 2 )"
"\n"	"oled.print( 0, 32, string.format( '%3.3f%%', sensors.cpu ), 2 )"
"\n"	"oled.print( 0, 48, string.format( '%10i', sensors.time ), 1 )"
;

int vm_lua_oled_print( lua_State *state ) {
	int x = luaL_checkinteger( state, 1 );
	int y = luaL_checkinteger( state, 2 );
	const char *text = luaL_checkstring( state, 3 );
	int size = luaL_checkinteger( state, 4 );
	ssize_t len = strlen( text );

	for ( int i = 0; i < len; i++ ) {
		oled_char( x + ( i * font_widths[ size - 1 ] ), y, text[ i ], size );
	}

	return 0;
}

const struct luaL_Reg oled_lib[] = {
	{ "print", vm_lua_oled_print },
	{ 0 }
};

void vm_init( void ) {
	vm = luaL_newstate();

	if ( vm == NULL ) {
		perror( "initializing LuaJIT" );
		exit( EXIT_FAILURE );
	}

	lua_pushcfunction( vm, luaopen_base );
	lua_pushstring( vm, "" );
	lua_call( vm, 1, 0 );

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
