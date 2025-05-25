#!/bin/env python3

import sys

if len( sys.argv ) < 2:
	print( f'Usage: { sys.argv[0] } <mask_file.txt>' )
	exit( 0 )

lines = []
with open( sys.argv[ 1 ], 'r' ) as file:
	for line in file:
		lines.append( line.rstrip()[:128].ljust( 128 ) )
		if len( lines ) >= 64:
			break

lines = ( lines + [ '' for _ in range( 64 ) ] )[:64]

masks = open( 'masks.h', 'w' )

print( 'unsigned char oled_mask_and[ 1024 ] = {', file = masks )

for ys in range( 0, 64, 8 ):
	if ys > 0:
		print( '', file = masks )

	for xs in range( 0, 128, 16 ):
		for xo in range( 16 ):
			mask = 0;
			for yo in range( 8 ):
				x = xs + xo
				if lines[ ys + yo ][ x : x + 1 ] == 'X':
					mask |= 1 << yo
			mask ^= 0xFF
			print( f'0x{mask:0>2x},', end = '', file = masks )
		print( '', file = masks )

print( '};\n\nunsigned char oled_mask_or[ 1024 ] = {', file = masks )

for ys in range( 0, 64, 8 ):
	if ys > 0:
		print( '', file = masks )

	for xs in range( 0, 128, 16 ):
		for xo in range( 16 ):
			mask = 0;
			for yo in range( 8 ):
				x = xs + xo
				if lines[ ys + yo ][ x : x + 1 ] == '+':
					mask |= 1 << yo
			print( f'0x{mask:0>2x},', end = '', file = masks )
		print( '', file = masks )

print( '};', file = masks )
