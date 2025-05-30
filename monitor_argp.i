// Unnamed enum as we're only using it to auto-generate values, not for typecasting
enum {
	ARGP_OPTION_VERBOSE = 'v',
	ARGP_OPTION_LUA_SCRIPT = 'l',
	ARGP_OPTION_TERMINAL_PREVIEW = '6',

	ARGP_OPTION_LONG_ONLY = 0x10FFFF, // Highest possible unicode code point
	ARGP_OPTION_OLED_DEVICE,
	ARGP_OPTION_OLED_ADDRESS,
	ARGP_OPTION_OLED_MASK,
	ARGP_OPTION_TEMPERATURE_DEVICE,
};

const char *argp_program_version = "v" __PACKAGE_VERSION__;
const char *argp_program_bug_address = "https://github.com/wolfwings/pironman5-lite";

static struct argp_option options[] = {
	{ .name = "verbose"
	, .key = ARGP_OPTION_VERBOSE, .arg = 0
	, .flags = 0, .doc = "Increase output verbosity" },

	{ .name = "terminal-preview"
	, .key = ARGP_OPTION_TERMINAL_PREVIEW, .arg = 0
	, .flags = 0, .doc = "Enable terminal preview with sixels" },

	{ .name = "lua-script"
	, .key = ARGP_OPTION_LUA_SCRIPT, .arg = "FILENAME"
	, .flags = 0, .doc = "Lua script to load, executed once per frame" },

	{ .name = "oled-device"
	, .key = ARGP_OPTION_OLED_DEVICE, .arg = "DEVICE"
	, .flags = 0, .doc = "OLED I2C device; defaults to /dev/i2c-1" },

	{ .name = "oled-address"
	, .key = ARGP_OPTION_OLED_ADDRESS, .arg = "ADDRESS"
	, .flags = 0, .doc = "OLED I2C address; defaults to 0x3C" },

	{ .name = "oled-mask"
	, .key = ARGP_OPTION_OLED_MASK, .arg = "FILENAME"
	, .flags = 0, .doc = "Text file specifying the 'forced mask' bits for OLED rendering; defaults to one suitable for the default rendering script" },

	{ .name = "temperature-device"
	, .key = ARGP_OPTION_TEMPERATURE_DEVICE, .arg = "DEVICE"
	, .flags = 0, .doc = "Temperature monitoring device; defaults to /sys/class/thermal/thermal_zone0/temp" },

	{ 0 }
};

struct {
	struct {
		char *device;
		uint32_t address;
		char *mask;
	} oled;
	struct {
		char *device;
	} temperature;
	unsigned int verbosity;
	unsigned int terminal_preview;
	char *script;
} arguments = {
	.oled.device        = "/dev/i2c-1",
	.oled.address       = 0x3C,
	.oled.mask          = NULL,
	.temperature.device = "/sys/class/thermal/thermal_zone0/temp",
	.verbosity          = 0,
	.terminal_preview   = 0,
	.script             = NULL,
};

static error_t parse_opt( int key, char *arg, struct argp_state *state ) {
	int h; // File handle
	off_t bytes;

	switch( key ) {
	case ARGP_OPTION_VERBOSE:
		arguments.verbosity++;
		return 0;

	case ARGP_OPTION_TERMINAL_PREVIEW:
		arguments.terminal_preview = 1;
		return 0;

	case ARGP_OPTION_LUA_SCRIPT:
		h = open( arg, O_RDONLY );

		if ( h < 0 ) {
			warn( "opening lua script %s for read access", arg );
			argp_usage( state );
		}

		bytes = lseek( h, 0, SEEK_END );
		if ( bytes < 0 ) {
			warn( "getting file size of lua script %s", arg );
			close( h );
			return 0;
		}

		if ( arguments.script != NULL ) {
			free( arguments.script );
		}

		// malloc is safe here as we immediately load the full buffer
		arguments.script = malloc( bytes + 1 );
		if ( arguments.script == NULL ) {
			warn( "allocating memory to load lua script %s", arg );
			argp_usage( state );
		}

		for (;;) {
			errno = 0;
			lseek( h, 0, SEEK_SET );
			if ( read( h, arguments.script, bytes ) == (ssize_t)bytes ) {
				break;
			}

			// Incomplete read or unlucky race condition, reset + retry
			if ( ( errno == EINTR )
			  || ( errno == 0 ) ) {
				continue;
			}

			// Actual error
			warn( "incomplete read of lua script %s", arg );
			argp_usage( state );

			__builtin_unreachable();
		}

		// Zero-terminate buffer to safely treat as a string
		arguments.script[ bytes ] = 0;

		close( h );

		return 0;

	case ARGP_OPTION_TEMPERATURE_DEVICE:
		arguments.temperature.device = arg;
		return 0;

	case ARGP_OPTION_OLED_DEVICE:
		arguments.oled.device = arg;
		return 0;

	case ARGP_OPTION_OLED_ADDRESS:
		errno = 0;
		arguments.oled.address = strtoul( arg, NULL, 0 );

		if ( errno != 0 ) {
			warn( "parsing OLED I2C address %s", arg );
			argp_usage( state );
		}

		return 0;

	case ARGP_OPTION_OLED_MASK:
		arguments.oled.mask = arg;
		return 0;

	case ARGP_KEY_ARG:
		argp_usage( state );
		return 0;

	case ARGP_KEY_END:
		if ( state->arg_num > 0 ) {
			argp_usage( state );
		}

		return 0;

	default:
		return ARGP_ERR_UNKNOWN;
	}

	__builtin_unreachable();
}

static struct argp argp = {
	.options = options,
	.parser = parse_opt,
	.args_doc = "",
	.doc = "monitoring utility for display on tiny I2C OLED screens"
};
