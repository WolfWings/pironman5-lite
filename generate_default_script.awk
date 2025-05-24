BEGIN {
	PREFIX = ""
	print( "static const char *__default_script =" )
}

{
	gsub( /\\/, "\\\\" )
	gsub( /"/, "\\\"" )
	LINE = $0
	print PREFIX "\t\"" substr( LINE, 0, 64 ) "\""
	while ( length( LINE ) > 64 ) {
		LINE = substr( LINE, 65 )
		print "\t\"" substr( LINE, 0, 64 ) "\""
	}
	PREFIX = "\"\\n\""
}

END {
	print( ";" )
}
