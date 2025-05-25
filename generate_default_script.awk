BEGIN {
	PREFIX = ""
	print( "static const char *__default_script =" )
}

{
	# escape backslashes
	gsub( /\\/, "\\\\" )

	# escape double quotes
	gsub( /"/, "\\\"" )

	# chunk the line into 64-character pieces
	LINE = $0
	print PREFIX "\t\"" substr( LINE, 0, 64 ) "\""
	while ( length( LINE ) > 64 ) {
		LINE = substr( LINE, 65 )
		print "\t\"" substr( LINE, 0, 64 ) "\""
	}

	# all lines except the first need the newline re-inserted
	PREFIX = "\"\\n\""
}

END {
	print( ";" )
}
