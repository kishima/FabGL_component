# Converts an image (png, jpeg, ....) to FabGL Bitmap structure
#
# usage:
#    python img2bitmap filename [-t x y] [-s width height] [-d] [-f0 | -f1 | -f2]
#
# -t  = pixel where to take transparent color
# -s  = resize to specified values
# -d  = enable dithering
# -f0 = format is RGB2222 (default)
# -f1 = format is RGB8888
# -f2 = format is 1 bit monochrome (color/transparent)
#
# Example:
#   python img2bitmap.py test.png -s 64 64 >out.c
#
# Requires PIL library:
#   sudo pip install pillow
