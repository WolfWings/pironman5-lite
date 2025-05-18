#!/bin/env python3
from bdfparser.src.bdfparser.bdfparser import *

fonts = open( "fonts.h", "w" )

def process_font( filename ):
	f = Font( filename )

	width = f.headers[ 'fbbx' ]
	height = f.headers[ 'fbby' ]
	print( f"Processing { height } pixel tall font..." )
	if ( height % 8 ) != 0:
		raise Exception( f"Font generator only supports fonts a multiple of 8 pixels tall" )

	print( f"unsigned char font_{ height // 8 }[] = {{", file = fonts )

	for c in range( 32, 126 + 1 ):
		g = f.glyph( chr( c ) )
		if g is None:
			g = f.glyph( " " )
			if g is None:
				raise Exception( f"Missing glyph [{ chr( c ) }] and no space character" )

		bmp = g.draw().todata( 2 )

		for y in range( 0, height - 1, 8 ):
			for x in range( width ):
				bits = 0
				for yo in range( 8 ):
					if bmp[ y + yo ][ x ] != 0:
						bits |= ( 1 << yo )
				print( '0x%02x,' % bits, end = '', file = fonts )

			if y == 0:
				print( f" // Character \"{ chr( c ) }\"", end = '', file = fonts )

			print( '', file = fonts )

	print( "};", file = fonts )

[ process_font( f"spleen_font/spleen-{ size }.bdf" ) for size in [ "5x8", "8x16", "12x24", "16x32" ] ]
