/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_image.c
#include "tr_local.h"

static byte			 s_intensitytable[256];
static unsigned char s_gammatable[256];

int		gl_filter_min = GL_LINEAR_MIPMAP_NEAREST;
int		gl_filter_max = GL_LINEAR;

float		gl_tex_max = 0;

int		force32upload;		// leilei - hack to get bloom/post to always do 32bit texture
int		detailhack;		// leilei - hack to fade detail textures, kill repeat patterns
#ifndef GL_VERSION_ES_CM_1_0
int		palettedformat = GL_COLOR_INDEX8_EXT;	// leilei - paletted texture support
#endif

#define FILE_HASH_SIZE		1024
static	image_t*		hashTable[FILE_HASH_SIZE];


extern int ismaptexture;

//
// leilei - paletted texture support START
//
#ifndef GL_VERSION_ES_CM_1_0
byte		*palettemain;		
byte		palmap[32][32][32];		// 15bpp lookup table
unsigned 	d_8to24table[256];  		// for non-palette supporting hardware

qboolean	paletteability;			// If our hardware has paletted texture support...
qboolean	paletteavailable;		// If we got a palette...
qboolean	paletteenabled;			// If we wish to enable the 32-to-8 conversions (and 8-to-32 also)
#endif
//
// leilei - blending hack support
//
//
//		Some cards just cannot use blending functions such as additive or subtractive.
//	So to get around this, I try to turn them into alpha channels and then alpha blend them.
//	Chipsets affected by this (That can run OA otherwise) are PowerVR PCX2 and Matrox Mystique / MGA-100A
//
//		Furthermore, this may also be useful for a software rasterizer by turning additives into
//	1-bit alpha textures.
//

int		hackoperation;			// 0 - do nothing
						// 1 - additive to alpha texture
						// 2 - demote alpha texture if not alphaed on the edges
						// 3 - subtractive to alpha texture
						// 4 - modulate to alpha texture

byte BestColor (int r, int g, int b, int start, int stop)
{
#ifndef GL_VERSION_ES_CM_1_0
	int	i;
	int	dr, dg, db;
	int	bestdistortion, distortion;
	int	berstcolor;
	byte	*pal;

//
// let any color go to 0 as a last resort
//
	bestdistortion = 256*256*4;
	berstcolor = 0;

	pal = palettemain + start*3;
	for (i=start ; i<= stop ; i++)
	{
		dr = r - (int)pal[0];
		dg = g - (int)pal[1];
		db = b - (int)pal[2];
		pal += 3;
		distortion = dr*dr + dg*dg + db*db;
		if (distortion < bestdistortion)
		{
			if (!distortion)
				return i;		// perfect match

			bestdistortion = distortion;
			berstcolor = i;
		}
	}
	if (berstcolor)
	//ri.Printf( PRINT_ALL, "returned %i\n", berstcolor );
	return berstcolor;
#endif
}

// From QUAKE2, but modified to support a transparent color

unsigned char paltable[1024]; 
unsigned char paltablergb[768]; 

void R_SetTexturePalette( int palette[256])
{
#ifndef GL_VERSION_ES_CM_1_0
	int i;

	
	{
		for ( i = 0; i < 256; i++ )
		{
			paltable[i*4+0] = ( palette[i] >> 0 ) & 0xff;
			paltable[i*4+1] = ( palette[i] >> 8 ) & 0xff;
			paltable[i*4+2] = ( palette[i] >> 16 ) & 0xff;
			paltable[i*4+3] = 0xFF;	// all of these colors will be opaque
			paltablergb[i*3+0] = ( palette[i] >> 0 ) & 0xff;
			paltablergb[i*3+1] = ( palette[i] >> 8 ) & 0xff;
			paltablergb[i*3+2] = ( palette[i] >> 16 ) & 0xff;
		}
			paltable[255*4+3] = 0x00;	// EXCEPT DREADED INDEX 255!!!!

	
	}
#endif
}

// leilei - paletted texture
extern GLvoid (APIENTRYP qglColorTableEXT)( GLint, GLint, GLint, GLint, GLint, const GLvoid *);

void R_PickTexturePalette(int alpha)
{
#ifndef GL_VERSION_ES_CM_1_0
		qglEnable(GL_SHARED_TEXTURE_PALETTE_EXT);

		if (alpha)
		qglColorTableEXT( GL_SHARED_TEXTURE_PALETTE_EXT, GL_RGBA, 256,  GL_RGBA, GL_UNSIGNED_BYTE,  paltable );
			else
		qglColorTableEXT( GL_SHARED_TEXTURE_PALETTE_EXT, GL_RGB, 256,  GL_RGB, GL_UNSIGNED_BYTE,  paltablergb );
		
		qglEnable(GL_SHARED_TEXTURE_PALETTE_EXT);
#endif
}

unsigned r_rawpalette[256];
void R_SetPalette ( const unsigned char *palette)
{
#ifndef GL_VERSION_ES_CM_1_0
	int		i;

	byte *rp = ( byte * ) r_rawpalette;

	if ( palette )
	{
		for ( i = 0; i < 256; i++ )
		{
			rp[i*4+0] = palette[i*3+0];
			rp[i*4+1] = palette[i*3+1];
			rp[i*4+2] = palette[i*3+2];
			rp[i*4+3] = 0xff;
		}
		rp[255*4+3] = 0x00; // alpha?
	}
	else
	{
		for ( i = 0; i < 256; i++ )
		{
			rp[i*4+0] = d_8to24table[i] & 0xff;
			rp[i*4+1] = ( d_8to24table[i] >> 8 ) & 0xff;
			rp[i*4+2] = ( d_8to24table[i] >> 16 ) & 0xff;
			rp[i*4+3] = 0xff;
		}
		rp[255*4+3] = 0x00; // alpha?
	}
	R_SetTexturePalette( r_rawpalette );

#endif
}

// leilei - for palettizing GLSL shaders, dump palette values to console
void R_GLSLPalette ( const unsigned char *palette)
{
#ifndef GL_VERSION_ES_CM_1_0
	int		i;

	float redf, greenf, bluef;

	if ( palette )
	{

		for ( i = 0; i < 256; i++ )
		{
			redf = (d_8to24table[i] & 0xff)  		* 0.003921568627451;
			greenf = (( d_8to24table[i] >> 8 ) & 0xff) 	* 0.003921568627451;
			bluef = (( d_8to24table[i] >> 16 ) & 0xff) 	* 0.003921568627451;
			ri.Printf( PRINT_ALL, "    vec3(%f, %f, %f),\n", redf, greenf, bluef );
		}
	}

#endif
}

void R_GLSLPalette_f ( )
{
	R_GLSLPalette(palettemain);

}

void R_InitPalette( void ) {
#ifndef GL_VERSION_ES_CM_1_0
	byte           *buff;
	int             len;
	int i, v;
	ri.Printf( PRINT_ALL, "INIT PALETTE......\n");

	len = ri.FS_ReadFile("gfx/palette.lmp", (void **)&buff);
	if(!buff){


	ri.Printf( PRINT_ALL, "PALLETE FALED :(!\n" );
	paletteavailable = 0;	// Don't have a palette
	paletteenabled   = 0;	// Don't do 8-bit textures
		return;
	}

	palettemain = buff;
	ri.Printf( PRINT_ALL, "PALETTE LOADDEEEED!!!!!!!!!!!!1\n" );
	paletteavailable = 1;	// Do have a palette

	if (palettedTextureSupport)
		paletteability = 1;
	else
		paletteability = 0;

	// change the transparent color to black
		palettemain[255*3] = 0;
		palettemain[255*3+1] = 0;
		palettemain[255*3+2] = 0;


	if (paletteability)		// load this palette for GL
		{
		qglEnable( GL_SHARED_TEXTURE_PALETTE_EXT );
		R_SetTexturePalette( palettemain );
		R_SetPalette(palettemain);

		}

	// 15bpp lookup, straight out of Engoo
	{
		int r, g, b, beastcolor;
		ri.Printf( PRINT_ALL, "15bpp lookup generation.\n" );
		for (r=0 ; r<256 ; r+=8)
			{
			ri.Printf( PRINT_ALL, "." );
				for (g=0 ; g<256 ; g+=8)
				{
					for (b=0 ; b<256 ; b+=8)
					{
						beastcolor = BestColor (r, g, b, 1, 254);
						palmap[r>>3][g>>3][b>>3] = beastcolor;
					}
				}
			}
	}


	{
	int re, ge, be;
	for (i=0 ; i<256 ; i++)
	{
		re = palettemain[0];
		ge = palettemain[1];
		be = palettemain[2];
		palettemain += 3;

		v = (255<<24) + (re<<0) + (ge<<8) + (be<<16);
		d_8to24table[i] = v;
	}
	}
#endif
}


//
// leilei - paletted texture support END
//


/*
** R_GammaCorrect
*/
void R_GammaCorrect( byte *buffer, int bufSize ) {
	int i;

	for ( i = 0; i < bufSize; i++ ) {
		buffer[i] = s_gammatable[buffer[i]];
	}
}

typedef struct {
	char *name;
	int	minimize, maximize;
} textureMode_t;

textureMode_t modes[] = {
	{"GL_NEAREST", GL_NEAREST, GL_NEAREST},
	{"GL_LINEAR", GL_LINEAR, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR}
};

/*
================
return a hash value for the filename
================
*/
static long generateHashValue( const char *fname ) {
	int		i;
	long	hash;
	char	letter;

	hash = 0;
	i = 0;
	while (fname[i] != '\0') {
		letter = tolower(fname[i]);
		if (letter =='.') break;				// don't include extension
		if (letter =='\\') letter = '/';		// damn path names
		hash+=(long)(letter)*(i+119);
		i++;
	}
	hash &= (FILE_HASH_SIZE-1);
	return hash;
}

/*
===============
GL_TextureMode
===============
*/
void GL_TextureMode( const char *string ) {
	int		i;
	image_t	*glt;

	for ( i=0 ; i< 6 ; i++ ) {
		if ( !Q_stricmp( modes[i].name, string ) ) {
			break;
		}
	}

	// hack to prevent trilinear from being set on voodoo,
	// because their driver freaks...
	if ( (i == 5 && glConfig.hardwareType == GLHW_3DFX_2D3D) || (r_legacycard->integer == 4 && r_legacycard->integer == 5) ) {
		ri.Printf( PRINT_ALL, "Refusing to set trilinear on a voodoo.\n" );
		i = 3;
	}


	if ( i == 6 ) {
		ri.Printf (PRINT_ALL, "bad filter name\n");
		return;
	}

	gl_filter_min = modes[i].minimize;
	gl_filter_max = modes[i].maximize;

	gl_tex_max = r_picLod->value * -1; 	// leilei - adjustable tex lod
	if (gl_tex_max > 2) gl_tex_max = 2; 	// clamp it to here

	// change all the existing mipmap texture objects
	for ( i = 0 ; i < tr.numImages ; i++ ) {
		glt = tr.images[ i ];
		if ( glt->flags & IMGFLAG_MIPMAP ) {
			GL_Bind (glt);
			qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
			qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);

			qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, gl_tex_max);
		}
	}
}

/*
===============
R_SumOfUsedImages
===============
*/
int R_SumOfUsedImages( void ) {
	int	total;
	int i;

	total = 0;
	for ( i = 0; i < tr.numImages; i++ ) {
		if ( tr.images[i]->frameUsed == tr.frameCount ) {
			total += tr.images[i]->uploadWidth * tr.images[i]->uploadHeight;
		}
	}

	return total;
}


/*
===============
R_ImageList_f
===============
*/
void R_ImageList_f( void ) {
	int i;
	int estTotalSize = 0;
	float estTotalTimeLoaded = 0;

	ri.Printf(PRINT_ALL, "\n      -w-- -h-- type  -size- --name-------\n");

	for ( i = 0 ; i < tr.numImages ; i++ )
	{
		image_t *image = tr.images[i];
		char *format = "???? ";
		char *sizeSuffix;
		int estSize;
		int displaySize;

		estSize = image->uploadHeight * image->uploadWidth;

		switch(image->internalFormat)
		{
#ifndef GL_VERSION_ES_CM_1_0			// GLES1
			case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
				format = "sDXT1";
				// 64 bits per 16 pixels, so 4 bits per pixel
				estSize /= 2;
				break;
			case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
				format = "sDXT5";
				// 128 bits per 16 pixels, so 1 byte per pixel
				break;
			case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB:
				format = "sBPTC";
				// 128 bits per 16 pixels, so 1 byte per pixel
				break;
			case GL_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT:
				format = "LATC ";
				// 128 bits per 16 pixels, so 1 byte per pixel
				break;
			case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
				format = "DXT1 ";
				// 64 bits per 16 pixels, so 4 bits per pixel
				estSize /= 2;
				break;
			case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
				format = "DXT5 ";
				// 128 bits per 16 pixels, so 1 byte per pixel
				break;
			case GL_COMPRESSED_RGBA_BPTC_UNORM_ARB:
				format = "BPTC ";
				// 128 bits per 16 pixels, so 1 byte per pixel
				break;
			case GL_RGB4_S3TC:
				format = "S3TC ";
				// same as DXT1?
				estSize /= 2;
				break;
			case GL_RGBA4:
			case GL_RGBA8:
			case GL_RGBA:
				format = "RGBA ";
				// 4 bytes per pixel
				estSize *= 4;
				break;
			case GL_LUMINANCE8:
			case GL_LUMINANCE16:
			case GL_LUMINANCE:
				format = "L    ";
				// 1 byte per pixel?
				break;
			case GL_RGB5:
			case GL_RGB8:
			case GL_RGB:
				format = "RGB  ";
				// 3 bytes per pixel?
				estSize *= 3;
				break;
			case GL_LUMINANCE8_ALPHA8:
			case GL_LUMINANCE16_ALPHA16:
			case GL_LUMINANCE_ALPHA:
				format = "LA   ";
				// 2 bytes per pixel?
				estSize *= 2;
				break;
			case GL_SRGB_EXT:
			case GL_SRGB8_EXT:
				format = "sRGB ";
				// 3 bytes per pixel?
				estSize *= 3;
				break;
			case GL_SRGB_ALPHA_EXT:
			case GL_SRGB8_ALPHA8_EXT:
				format = "sRGBA";
				// 4 bytes per pixel?
				estSize *= 4;
				break;
			case GL_SLUMINANCE_EXT:
			case GL_SLUMINANCE8_EXT:
				format = "sL   ";
				// 1 byte per pixel?
				break;
			case GL_SLUMINANCE_ALPHA_EXT:
			case GL_SLUMINANCE8_ALPHA8_EXT:
				format = "sLA  ";
				// 2 byte per pixel?
				estSize *= 2;
				break;
#else
		case 1:
			ri.Printf( PRINT_ALL, "I    " );
			break;
		case 2:
			ri.Printf( PRINT_ALL, "IA   " );
			break;
		case 3:
			ri.Printf( PRINT_ALL, "RGB  " );
			break;
		case 4:
			ri.Printf( PRINT_ALL, "RGBA " );
			break;
#endif
		}

		// mipmap adds about 50%
		if (image->flags & IMGFLAG_MIPMAP)
			estSize += estSize / 2;

		sizeSuffix = "b ";
		displaySize = estSize;

		if (displaySize > 1024)
		{
			displaySize /= 1024;
			sizeSuffix = "kb";
		}

		if (displaySize > 1024)
		{
			displaySize /= 1024;
			sizeSuffix = "Mb";
		}

		if (displaySize > 1024)
		{
			displaySize /= 1024;
			sizeSuffix = "Gb";
		}

		//ri.Printf(PRINT_ALL, "%4i: %4ix%4i %s %4i%s %s\n", i, image->uploadWidth, image->uploadHeight, format, displaySize, sizeSuffix, image->imgName);
		ri.Printf(PRINT_ALL, "%4i: %4ix%4i %s %4i%s %s %f %f\n", i, image->uploadWidth, image->uploadHeight, format, displaySize, sizeSuffix, image->imgName, image->loadTime, image->procTime);

		estTotalSize += estSize;
		estTotalTimeLoaded += image->loadTime + image->procTime;
	}

	ri.Printf (PRINT_ALL, " ---------\n");
	ri.Printf (PRINT_ALL, " approx %i bytes\n", estTotalSize);
	ri.Printf (PRINT_ALL, " %i total images\n\n", tr.numImages );
	ri.Printf (PRINT_ALL, " %f msec loaded\n\n", estTotalTimeLoaded );
}


/*
===============
R_ImageListMapOnly_f


	This version is used to make life easier to see which map textures are used
	and also provide a bunch of lame zip commands to modularize maps better
	(i.e. packing only used stuff by the *good* maps for releases)
===============
*/
void R_ImageListMapOnly_f( void ) {
	int i;

	for ( i = 0 ; i < tr.numImages ; i++ )
	{
		image_t *image = tr.images[i];
		char *zipcommand = "zip -9 ";
		char localName[ MAX_QPATH ];
		char *sizeSuffix;
		int estSize;

		estSize = image->uploadHeight * image->uploadWidth;

		
		// mipmap adds about 50%
		if (image->flags & IMGFLAG_MIPMAP)
			estSize += estSize / 2;

		sizeSuffix = "b ";

		if (image->maptexture){
		COM_StripExtension( image->imgName, localName, MAX_QPATH );
		ri.Printf(PRINT_ALL, "%s pak1-map-mapname.pk3 %s.*\n", zipcommand, localName);
		}
	}

}



//=======================================================================

/*
================
ResampleTexture

Used to resample images in a more general than quartering fashion.

This will only be filtered properly if the resampled size
is greater than half the original size.

If a larger shrinking is needed, use the mipmap function 
before or after.
================
*/
static void ResampleTexture( unsigned *in, int inwidth, int inheight, unsigned *out,  
							int outwidth, int outheight ) {
	int		i, j;
	unsigned	*inrow, *inrow2;
	unsigned	frac, fracstep;
	unsigned	p1[2048], p2[2048];
	byte		*pix1, *pix2, *pix3, *pix4;

	if (outwidth>2048)
		ri.Error(ERR_DROP, "ResampleTexture: max width");
								
	fracstep = inwidth*0x10000/outwidth;

	frac = fracstep>>2;
	for ( i=0 ; i<outwidth ; i++ ) {
		p1[i] = 4*(frac>>16);
		frac += fracstep;
	}
	frac = 3*(fracstep>>2);
	for ( i=0 ; i<outwidth ; i++ ) {
		p2[i] = 4*(frac>>16);
		frac += fracstep;
	}

	for (i=0 ; i<outheight ; i++, out += outwidth) {
		inrow = in + inwidth*(int)((i+0.25)*inheight/outheight);
		inrow2 = in + inwidth*(int)((i+0.75)*inheight/outheight);
		for (j=0 ; j<outwidth ; j++) {
			pix1 = (byte *)inrow + p1[j];
			pix2 = (byte *)inrow + p2[j];
			pix3 = (byte *)inrow2 + p1[j];
			pix4 = (byte *)inrow2 + p2[j];
			((byte *)(out+j))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0])>>2;
			((byte *)(out+j))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1])>>2;
			((byte *)(out+j))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2])>>2;
			((byte *)(out+j))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3])>>2;
		}
	}
}





/*
================
R_LightScaleTexture

Scale up the pixel values in a texture to increase the
lighting range
================
*/
void R_LightScaleTexture (unsigned *in, int inwidth, int inheight, qboolean only_gamma )
{
	return; // leilei - quake2 "gamma" code is obsolete here. breaks shaders
}


/*
================
R_MipMap2

Operates in place, quartering the size of the texture
Proper linear filter
================
*/
static void R_MipMap2( unsigned *in, int inWidth, int inHeight ) {
	int			i, j, k;
	byte		*outpix;
	int			inWidthMask, inHeightMask;
	int			total;
	int			outWidth, outHeight;
	unsigned	*temp;

	outWidth = inWidth >> 1;
	outHeight = inHeight >> 1;
	temp = ri.Hunk_AllocateTempMemory( outWidth * outHeight * 4 );

	inWidthMask = inWidth - 1;
	inHeightMask = inHeight - 1;

	for ( i = 0 ; i < outHeight ; i++ ) {
		for ( j = 0 ; j < outWidth ; j++ ) {
			outpix = (byte *) ( temp + i * outWidth + j );
			for ( k = 0 ; k < 4 ; k++ ) {
				total = 
					1 * ((byte *)&in[ ((i*2-1)&inHeightMask)*inWidth + ((j*2-1)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2-1)&inHeightMask)*inWidth + ((j*2)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2-1)&inHeightMask)*inWidth + ((j*2+1)&inWidthMask) ])[k] +
					1 * ((byte *)&in[ ((i*2-1)&inHeightMask)*inWidth + ((j*2+2)&inWidthMask) ])[k] +

					2 * ((byte *)&in[ ((i*2)&inHeightMask)*inWidth + ((j*2-1)&inWidthMask) ])[k] +
					4 * ((byte *)&in[ ((i*2)&inHeightMask)*inWidth + ((j*2)&inWidthMask) ])[k] +
					4 * ((byte *)&in[ ((i*2)&inHeightMask)*inWidth + ((j*2+1)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2)&inHeightMask)*inWidth + ((j*2+2)&inWidthMask) ])[k] +

					2 * ((byte *)&in[ ((i*2+1)&inHeightMask)*inWidth + ((j*2-1)&inWidthMask) ])[k] +
					4 * ((byte *)&in[ ((i*2+1)&inHeightMask)*inWidth + ((j*2)&inWidthMask) ])[k] +
					4 * ((byte *)&in[ ((i*2+1)&inHeightMask)*inWidth + ((j*2+1)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2+1)&inHeightMask)*inWidth + ((j*2+2)&inWidthMask) ])[k] +

					1 * ((byte *)&in[ ((i*2+2)&inHeightMask)*inWidth + ((j*2-1)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2+2)&inHeightMask)*inWidth + ((j*2)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2+2)&inHeightMask)*inWidth + ((j*2+1)&inWidthMask) ])[k] +
					1 * ((byte *)&in[ ((i*2+2)&inHeightMask)*inWidth + ((j*2+2)&inWidthMask) ])[k];
				outpix[k] = total / 36;
			}
		}
	}

	Com_Memcpy( in, temp, outWidth * outHeight * 4 );
	ri.Hunk_FreeTempMemory( temp );
}

/*
================
R_MipMap

Operates in place, quartering the size of the texture
================
*/
static void R_MipMap (byte *in, int width, int height) {
	int		i, j;
	byte	*out;
	int		row;

	if ( !r_simpleMipMaps->integer ) {
		R_MipMap2( (unsigned *)in, width, height );
		return;
	}

	if ( width == 1 && height == 1 ) {
		return;
	}

	row = width * 4;
	out = in;
	width >>= 1;
	height >>= 1;

	if ( width == 0 || height == 0 ) {
		width += height;	// get largest
		for (i=0 ; i<width ; i++, out+=4, in+=8 ) {
			out[0] = ( in[0] + in[4] )>>1;
			out[1] = ( in[1] + in[5] )>>1;
			out[2] = ( in[2] + in[6] )>>1;
			out[3] = ( in[3] + in[7] )>>1;
		}
		return;
	}

	for (i=0 ; i<height ; i++, in+=row) {
		for (j=0 ; j<width ; j++, out+=4, in+=8) {
			out[0] = (in[0] + in[4] + in[row+0] + in[row+4])>>2;
			out[1] = (in[1] + in[5] + in[row+1] + in[row+5])>>2;
			out[2] = (in[2] + in[6] + in[row+2] + in[row+6])>>2;
			out[3] = (in[3] + in[7] + in[row+3] + in[row+7])>>2;
		}
	}
}


//
// leilei - paletted texture support START
//
// This function came out of GLQuake
// with modifications 

static void R_MipMap8 (byte *in, int width, int height)
{
#ifndef GL_VERSION_ES_CM_1_0
	int		i, j;
	byte	*out, *at1;//, *at2, *at3, *at4;

	height >>= 1;
	out = in;

	for (i=0 ; i<height ; i++, in+=width)
	{
		for (j=0 ; j<width ; j+=2, out+=1, in+=2)
		{
			at1 = in[0];
			out[0] = at1;
		}
	}
#endif
}
//
// leilei - paletted texture support END
//

/*
==================
R_BlendOverTexture

Apply a color blend over a set of pixels
==================
*/
static void R_BlendOverTexture( byte *data, int pixelCount, byte blend[4] ) {
	int		i;
	int		inverseAlpha;
	int		premult[3];

	inverseAlpha = 255 - blend[3];
	premult[0] = blend[0] * blend[3];
	premult[1] = blend[1] * blend[3];
	premult[2] = blend[2] * blend[3];

	for ( i = 0 ; i < pixelCount ; i++, data+=4 ) {
		data[0] = ( data[0] * inverseAlpha + premult[0] ) >> 9;
		data[1] = ( data[1] * inverseAlpha + premult[1] ) >> 9;
		data[2] = ( data[2] * inverseAlpha + premult[2] ) >> 9;
	}
}

byte	mipBlendColors[16][4] = {
	{0,0,0,0},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
};

#ifdef GL_VERSION_ES_CM_1_0		// GLES1

static unsigned * ConvertPixels_RGBA_RGB( unsigned *src, unsigned size )
{
	unsigned i;
	unsigned char * dst = (unsigned char *) malloc( size * 3 );
	for( i = 0; i < size; i++ )
	{
		unsigned c = src[i];
		dst[ i * 3 + 0 ] = (c & 0xff);
		dst[ i * 3 + 1 ] = (c & 0xff00) / 0x100;
		dst[ i * 3 + 2 ] = (c & 0xff0000) / 0x10000;
	}
	return (unsigned *)dst;
}

static void R_qglTexImage2D( GLint miplevel, GLint internalFormat,
								GLsizei width, GLsizei height,
								unsigned *pixels )
{
	qglPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	if( internalFormat == GL_RGBA )
		qglTexImage2D (GL_TEXTURE_2D, miplevel, internalFormat, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	else if( internalFormat == GL_RGB ) {
		unsigned *converted = ConvertPixels_RGBA_RGB( pixels, width * height );
		qglTexImage2D (GL_TEXTURE_2D, miplevel, internalFormat, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, converted);
		free(converted);
	} else
		Com_Printf("R_qglTexImage2D: texture format %d (0x%x) not supported in GLES\n", internalFormat, internalFormat);

}
#endif

/*
==================
R_BlendToGray

leilei - Tries to fade mips to gray for detail texture pattern artifact stomping
==================
*/

static void R_BlendToGray( byte *data, int pixelCount, int fadeto, int toblack) {
	int		i, j;
	float 		gary = 127 * 0.5;
	float		blended;
	float		alphed, alpher;

	if (fadeto < 1)
		return;	//  we don't need to for the highest mip.

	if (fadeto == 1){	alphed = 0.75; alpher = 0.25; }
	else if (fadeto == 2){	alphed = 0.50; alpher = 0.50; }
	else if (fadeto == 3){	alphed = 0.25; alpher = 0.75; }
	else {	alphed = 0.0; alpher = 1.00; }

	fadeto += 1;

	gary /= fadeto;

	for ( i = 0 ; i < pixelCount ; i++, data+=4 ) {
		for(j=0;j<3;j++){
			if (toblack)
			blended = (data[j] * alphed) + (0 * alpher);
			else
			blended = (data[j] * alphed) + (127 * alpher);
			data[j] = (int)blended;
			if (alpher > 0.75) data[j+4]=0; // hack to give alpha channel to the lowest
			}


	}
}


static int dither2x2[2][2] =
{
	{ 4,  6,},
	{ 7,  5 }
};




/*
===============
Upload32

===============
*/
extern qboolean charSet;

int	isicon;			// leilei  - for determining if it's an icon.

static void Upload32( unsigned *data, 
						  int width, int height, 
						  qboolean mipmap, 
						  qboolean picmip, 
							qboolean lightMap,
						  int *format, 
						  int *pUploadWidth, int *pUploadHeight )
{
	int			samples;
	unsigned	*scaledBuffer = NULL;
	unsigned	*resampledBuffer = NULL;
	int			scaled_width, scaled_height;
	int			orig_width, orig_height;
	int			i, c;
	byte		*scan;
	GLenum		internalFormat = GL_RGB;
	GLenum		temp_GLformat = GL_RGBA;
	GLenum		temp_GLtype = GL_UNSIGNED_BYTE;
	float		rMax = 0, gMax = 0, bMax = 0;
	int		texsizex, texsizey;
	int		forceBits = 0; 


	if (lightMap && r_parseStageSimple->integer) hackoperation = 4;

	// leilei - npot support
	orig_width = width;
	orig_height = height;

	//
	// convert to exact power of 2 sizes
	//


	if (r_roundImagesDown->integer == 2)
	{

	scaled_width = width;
	scaled_height = height;

		//for (scaled_width = 1 ; scaled_width < width ; scaled_width<<=1)
		//	;
		//for (scaled_height = 1 ; scaled_height < height ; scaled_height<<=1)
		//	;

		if ( scaled_width != width || scaled_height != height ) {
			resampledBuffer = ri.Hunk_AllocateTempMemory( scaled_width * scaled_height * 4 );
			ResampleTexture (data, width, height, resampledBuffer, scaled_width, scaled_height);
			data = resampledBuffer;
			width = scaled_width;
			height = scaled_height;
		}


	}
	else 
	{

		for (scaled_width = 1 ; scaled_width < width ; scaled_width<<=1)
			;
		for (scaled_height = 1 ; scaled_height < height ; scaled_height<<=1)
			;
		if ( (r_roundImagesDown->integer == 1) && scaled_width > width )
			scaled_width >>= 1;
		if ( (r_roundImagesDown->integer == 1) && scaled_height > height )
			scaled_height >>= 1;

		if ( scaled_width != width || scaled_height != height ) {
			resampledBuffer = ri.Hunk_AllocateTempMemory( scaled_width * scaled_height * 4 );
			ResampleTexture (data, width, height, resampledBuffer, scaled_width, scaled_height);
			data = resampledBuffer;
			width = scaled_width;
			height = scaled_height;
		}

	}


	texsizex = glConfig.maxTextureSize;
	texsizey = glConfig.maxTextureSize;
	

	if ( (r_legacycard->integer == 1 && r_legacycard->integer == 2 && r_legacycard->integer == 4 && r_legacycard->integer == 5 && r_legacycard->integer == 1001) && !force32upload){	// leilei
	texsizex = 256;	// 3dfx 
	texsizey = 256;	// 3dfx 
	}


	//
	// perform optional picmip operation
	//
	if ( picmip && !detailhack ) {
		scaled_width >>= r_picmip->integer;
		scaled_height >>= r_picmip->integer;
	}


	//
	// leilei - icon picmip for certain 2d graphics elements that get wasted in lower resolutions, saving vram
	//

	if ( isicon ){
	
		if (r_iconmip->integer){
	
	
			// Auto-determine from resolution division
			
			if (r_iconmip->integer == 1){
				int wadth, haght, dev = 0;
	
				wadth = floor(1280 / glConfig.vidWidth) - 1;
				haght = floor(960 / glConfig.vidHeight) - 1;
				if (wadth > haght) dev = wadth;
				else if (haght > wadth) dev = haght;
				if (dev < 0) dev = 0;

				scaled_width >>= dev;
				scaled_height >>= dev;
	
				if (scaled_width < 32) scaled_width = 32;
				if (scaled_height < 32) scaled_height = 32;
	
				}
			else
			// Force it 
			{
			scaled_width >>= (r_iconmip->integer - 1);
			scaled_height >>= (r_iconmip->integer - 1);
			if (scaled_width < 16) scaled_width = 16;
			if (scaled_height < 16) scaled_height = 16;
	
			}
	
	
		}
		if (r_iconBits->integer){
			forceBits = r_iconBits->integer;
		}
	}


	// leilei - lightmap color bits, for saving vram/tex cache
	if (lightMap){
		if (r_lightmapBits->integer){
			forceBits = r_lightmapBits->integer;
			force32upload = 0;
		}
	}

	//
	// clamp to minimum size
	//
	if (scaled_width < 1) {
		scaled_width = 1;
	}
	if (scaled_height < 1) {
		scaled_height = 1;
	}



	//
	// clamp to the current upper OpenGL limit
	// scale both axis down equally so we don't have to
	// deal with a half mip resampling
	//
	while ( scaled_width > texsizex
		|| scaled_height > texsizey ) {
		scaled_width >>= 1;
		scaled_height >>= 1;
	}

	


	scaledBuffer = ri.Hunk_AllocateTempMemory( sizeof( unsigned ) * scaled_width * scaled_height );

	//
	// scan the texture for each channel's max values
	// and verify if the alpha channel is being used or not
	//
	c = width*height;
	scan = ((byte *)data);
	samples = 3;


	if (hackoperation == 1)
	{
			// leilei - additive to alpha
			for ( i = 0; i < c; i++ )
				{
					int r, g, b;
					vec3_t rgb;
					byte alfa = LUMA(scan[i*4], scan[i*4 + 1], scan[i*4 + 2]);
					//byte alfa = (scan[i*4]+ scan[i*4 + 1]+ scan[i*4 + 2]) / 3;

					r = scan[i*4 + 0];
					g = scan[i*4 + 1];
					b = scan[i*4 + 2];
					rgb[0] = r;
					rgb[1] = g;
					rgb[2] = b;
					VectorNormalize(rgb);
					r = rgb[0] * 256;
					g = rgb[1] * 256;
					b = rgb[2] * 256;

					if (r>255) r=255;
					if (g>255) g=255;
					if (b>255) b=255;

					if (scan[i*4 + 0] > r) r = scan[i*4 + 0];
					if (scan[i*4 + 1] > g) g = scan[i*4 + 1];
					if (scan[i*4 + 2] > b) b = scan[i*4 + 2];

					scan[i*4 + 0] = r;
					scan[i*4 + 1] = g;
					scan[i*4 + 2] = b;
					scan[i*4 + 3] = alfa;

				}

	}


	else if (hackoperation == 2)
	{
			// leilei - alpha killing
			for ( i = 0; i < c; i++ )
				{
					scan[i*4 + 3] = 255;

				}

	}


	else if(hackoperation == 3 )	// Subtractives
	{
		for ( i = 0; i < c; i++ )
		{

			byte alfa = LUMA(scan[i*4], scan[i*4 + 1], scan[i*4 + 2]);
			scan[i*4] 	= 0;
			scan[i*4 + 1] 	= 0;
			scan[i*4 + 2] 	= 0;
			scan[i*4 + 3] 	= alfa;

		}
	}

	else if (hackoperation == 4)	// modulateply
	{
			
			for ( i = 0; i < c; i++ )
				{
					int r, g, b;
					vec3_t rgb;
					
					float amplify;
					byte alfa = LUMA(scan[i*4], scan[i*4 + 1], scan[i*4 + 2]);
					//byte alfa = (scan[i*4]+ scan[i*4 + 1]+ scan[i*4 + 2]) / 3;
					alfa = sin(alfa/64) * 32;

					amplify = (alfa - 128) / 255;
					r = scan[i*4 + 0];
					g = scan[i*4 + 1];
					b = scan[i*4 + 2];
					rgb[0] = r;
					rgb[1] = g;
					rgb[2] = b;
					VectorNormalize(rgb);
					r = rgb[0] * 256;
					g = rgb[1] * 256;
					b = rgb[2] * 256;

					r *= amplify;
					g *= amplify;
					b *= amplify;


					if (r<0) r=0;
					if (g<0) g=0;
					if (b<0) b=0;

					if (r>255) r=255;
					if (g>255) g=255;
					if (b>255) b=255;

					if (scan[i*4 + 0] > r) r = scan[i*4 + 0];
					if (scan[i*4 + 1] > g) g = scan[i*4 + 1];
					if (scan[i*4 + 2] > b) b = scan[i*4 + 2];

					scan[i*4 + 0] = r;
					scan[i*4 + 1] = g;
					scan[i*4 + 2] = b;
					scan[i*4 + 3] = alfa;

				}

	}

	
	if (detailhack && r_detailTextureSub->integer)	// invert for subtractive
	{
			
		for ( i = 0; i < c; i++ )
			{
				int eh;
				for (eh=0;eh<3;eh++){
					scan[i*4 + eh] = ((scan[i*4 + eh] * -1) + 255) * 1.0;
					}
			}
	}


	if(r_textureDither->integer)		// possibly the stupidest texture dithering ever
	{
		for ( i = 0; i < c; i++ )
		{
			int ren = (crandom() * 4);
			int rg,gg,bb;
			int ex, yy;

			ex = i&1;
			yy = (i+1) &1;
			rg = scan[i*4] - dither2x2[ex][yy]; 
			gg = scan[i*4+1] - dither2x2[ex][yy]; 
			bb = scan[i*4+2] - dither2x2[ex][yy]; 

			if (rg < 0) rg = 0;
			if (gg < 0) gg = 0;
			if (bb < 0) bb = 0;
	
			if (rg > 255) rg = 255;
			if (gg > 255) gg = 255;
			if (bb > 255) bb = 255;
	
			scan[i*4] = rg;
			scan[i*4 + 1] = gg;
			scan[i*4 + 2] =  bb;
		}
		
	}




	if( r_greyscale->value )
	{
			// leilei - replaced with saturation processing
		for ( i = 0; i < c; i++ )
		{
				float saturated = (scan[i*4] * 0.333) + (scan[i*4 + 1] * 0.333) + (scan[i*4 + 2] * 0.333);
				scan[i*4] 	= saturated + (scan[i*4] - saturated) 	  * (1-r_greyscale->value);
				scan[i*4 + 1] 	= saturated + (scan[i*4 + 1] - saturated) * (1-r_greyscale->value);
				scan[i*4 + 2] 	= saturated + (scan[i*4 + 2] - saturated) * (1-r_greyscale->value);
	
				if (scan[i*4]  		> 255) scan[i*4]      	= 255;
				if (scan[i*4 + 1]  	> 255) scan[i*4 + 1]  	= 255;
				if (scan[i*4 + 2]  	> 255) scan[i*4 + 2]  	= 255;
		}
	}

	if(lightMap)
	{
		if( r_monolightmaps->value )
		{
			for ( i = 0; i < c; i++ )
			{
					float saturated = (scan[i*4] * 0.333) + (scan[i*4 + 1] * 0.333) + (scan[i*4 + 2] * 0.333);
					scan[i*4] 	= saturated + (scan[i*4] - saturated) 	  * (1-r_monolightmaps->value);
					scan[i*4 + 1] 	= saturated + (scan[i*4 + 1] - saturated) * (1-r_monolightmaps->value );
					scan[i*4 + 2] 	= saturated + (scan[i*4 + 2] - saturated) * (1-r_monolightmaps->value );
		
					if (scan[i*4]  		> 255) scan[i*4]      	= 255;
					if (scan[i*4 + 1]  	> 255) scan[i*4 + 1]  	= 255;
					if (scan[i*4 + 2]  	> 255) scan[i*4 + 2]  	= 255;
			}
		}

		if(r_greyscale->integer)
			internalFormat = GL_LUMINANCE;
		else if(r_monolightmaps->integer)
			internalFormat = GL_LUMINANCE;
		else
			internalFormat = GL_RGB;

	// leilei - lightmap color bits, for saving vram/tex cache
		if (r_lightmapBits->integer){
			forceBits = r_lightmapBits->integer;
			force32upload = 0;

				if ( forceBits == 16)
				{
					internalFormat = GL_RGB5;
				}
				else if ( forceBits == 15)
				{
					internalFormat = GL_RGB5;
				}
				else if ( forceBits == 12)
				{
					internalFormat = GL_RGB4;
				}
				else if ( forceBits == 6)
				{
					internalFormat = GL_R3_G3_B2;
				}
				else if ( forceBits == 32)
				{
					internalFormat = GL_RGB8;
				}
				else
				{
					internalFormat = GL_RGB;
				}

		}
	
	}
	else
	{
		for ( i = 0; i < c; i++ )
		{
			if ( scan[i*4+0] > rMax )
			{
				rMax = scan[i*4+0];
			}
			if ( scan[i*4+1] > gMax )
			{
				gMax = scan[i*4+1];
			}
			if ( scan[i*4+2] > bMax )
			{
				bMax = scan[i*4+2];
			}
			if ( scan[i*4 + 3] != 255 ) 
			{
				samples = 4;
				break;
			}
		}
		// select proper internal format
		if ( samples == 3 )
		{
			if(r_greyscale->integer)
			{
#ifndef GL_VERSION_ES_CM_1_0
				if(r_texturebits->integer == 16 || forceBits == 16)
					internalFormat = GL_LUMINANCE8;
				else if(r_texturebits->integer == 32 || forceBits == 32)
					internalFormat = GL_LUMINANCE16;
				else
#endif
					internalFormat = GL_LUMINANCE;
			}
			else
			{
#ifndef GL_VERSION_ES_CM_1_0
				if ( glConfig.textureCompression == TC_S3TC_ARB )
				{
					internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
				}
				else if ( glConfig.textureCompression == TC_S3TC )
				{
					internalFormat = GL_RGB4_S3TC;
				}
				else if ( r_texturebits->integer == 16 || forceBits == 16)
				{
					internalFormat = GL_RGB5;
				}
				else if ( r_texturebits->integer == 15 || forceBits == 15)
				{
					internalFormat = GL_RGB5;
				}
				else if ( r_texturebits->integer == 12 || forceBits == 12)
				{
					internalFormat = GL_RGB4;
				}
				else if ( r_texturebits->integer == 6 || forceBits == 6)
				{
					internalFormat = GL_R3_G3_B2;
				}
				else if ( r_texturebits->integer == 32 || forceBits == 32)
				{
					internalFormat = GL_RGB8;
				}
				else
#endif
				{
					internalFormat = GL_RGB;
				}
#ifndef GL_VERSION_ES_CM_1_0
					if (detailhack) internalFormat = GL_LUMINANCE; // leilei - use paletted mono format for detail textures
					if (force32upload) internalFormat = GL_RGB8;   // leilei - gets bloom and postproc working on s3tc & 8bit & palettes
					if ((r_legacycard->integer > 0) && (r_legacycard->integer < 6) && (!force32upload)) internalFormat = GL_RGB5;
#endif
			}
		}
		else if ( samples == 4 )
		{
			if(r_greyscale->integer)
			{
#ifndef GL_VERSION_ES_CM_1_0
				if(r_texturebits->integer == 16 || forceBits == 16)
					internalFormat = GL_LUMINANCE8_ALPHA8;
				else if(r_texturebits->integer == 32 || forceBits == 32)
					internalFormat = GL_LUMINANCE16_ALPHA16;
				else
#endif
					internalFormat = GL_LUMINANCE_ALPHA;
			}
			else
			{
#ifndef GL_VERSION_ES_CM_1_0
				if ( glConfig.textureCompression == TC_S3TC_ARB )
				{
					internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; // leilei - this was missing
				}
				else 
				if ( r_texturebits->integer == 16 || forceBits == 16)
				{
					internalFormat = GL_RGBA4;
				}
				else if ( r_texturebits->integer == 15 || forceBits == 15)
				{
					internalFormat = GL_RGB5_A1;
				}
				else if ( r_texturebits->integer == 12 || forceBits == 12)
				{
					internalFormat = GL_RGBA4;
				}
				else if ( r_texturebits->integer == 6 || forceBits == 6)
				{
					internalFormat = GL_RGBA2;
				}
				else if ( r_texturebits->integer == 32 || forceBits == 32)
				{
					internalFormat = GL_RGBA8;
				}
				else
#endif
				{
					internalFormat = GL_RGBA;
				}
#ifndef GL_VERSION_ES_CM_1_0
					if (force32upload) internalFormat = GL_RGBA8;   // leilei - gets bloom and postproc working on s3tc & 8bit & palettes
					if ((r_legacycard->integer > 0) && (r_legacycard->integer < 6) && (!force32upload)) internalFormat = GL_RGBA4;
#endif
			}
		}
	}

#ifndef GL_VERSION_ES_CM_1_0
	if (depthimage) 
		{ mipmap=0; internalFormat = GL_DEPTH_COMPONENT; temp_GLformat=GL_DEPTH_COMPONENT; temp_GLtype=GL_FLOAT; } 
			else 
		{ temp_GLformat=GL_RGBA; temp_GLtype=GL_UNSIGNED_BYTE; }
#endif


	// copy or resample data as appropriate for first MIP level
	if ( ( scaled_width == width ) && 
		( scaled_height == height ) ) {
		if (!mipmap)
		{
#ifdef GL_VERSION_ES_CM_1_0
			R_qglTexImage2D (0, internalFormat, scaled_width, scaled_height, data);
#else
			qglTexImage2D (GL_TEXTURE_2D, 0, internalFormat, scaled_width, scaled_height, 0, temp_GLformat, temp_GLtype, data);
#endif
			*pUploadWidth = scaled_width;
			*pUploadHeight = scaled_height;
			*format = internalFormat;

			goto done;
		}


		

		Com_Memcpy (scaledBuffer, data, width*height*4);
	}
	else
	{
		// use the normal mip-mapping function to go down from here
		while ( width > scaled_width || height > scaled_height ) {
			R_MipMap( (byte *)data, width, height );
			width >>= 1;
			height >>= 1;
			if ( width < 1 ) {
				width = 1;
			}
			if ( height < 1 ) {
				height = 1;
			}
		}
		Com_Memcpy( scaledBuffer, data, width * height * 4 );
	}

	//R_LightScaleTexture (scaledBuffer, scaled_width, scaled_height, !mipmap );

	*pUploadWidth = scaled_width;
	*pUploadHeight = scaled_height;
	*format = internalFormat;

#ifndef GL_VERSION_ES_CM_1_0
	qglTexImage2D (GL_TEXTURE_2D, 0, internalFormat, scaled_width, scaled_height, 0, temp_GLformat, temp_GLtype, scaledBuffer );
#else
	R_qglTexImage2D (0, internalFormat, scaled_width, scaled_height, scaledBuffer);
#endif


	if (mipmap)
	{
		int		miplevel;

		miplevel = 0;
		while (scaled_width > 1 || scaled_height > 1)
		{
			R_MipMap( (byte *)scaledBuffer, scaled_width, scaled_height );
			scaled_width >>= 1;
			scaled_height >>= 1;
			if (scaled_width < 1)
				scaled_width = 1;
			if (scaled_height < 1)
				scaled_height = 1;
			miplevel++;

			if ( r_colorMipLevels->integer ) {
				R_BlendOverTexture( (byte *)scaledBuffer, scaled_width * scaled_height, mipBlendColors[miplevel] );
			}

			if (detailhack)		// leilei	- blend detail textures to gray to defeat pattern effects in distances
			{
				if (r_detailTextureSub->integer)
				R_BlendToGray( (byte *)scaledBuffer, scaled_width * scaled_height, miplevel, 1 );
				else
				R_BlendToGray( (byte *)scaledBuffer, scaled_width * scaled_height, miplevel, 0 );
			}

#ifndef GL_VERSION_ES_CM_1_0		
			qglTexImage2D (GL_TEXTURE_2D, miplevel, internalFormat, scaled_width, scaled_height, 0, temp_GLformat, temp_GLtype, scaledBuffer );
#else
			R_qglTexImage2D (miplevel, internalFormat, scaled_width, scaled_height, scaledBuffer);
#endif
		}
	}
done:

	

	if (mipmap)
	{
		if ( textureFilterAnisotropic )
			qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
					(GLint)Com_Clamp( 1, maxAnisotropy, r_ext_max_anisotropy->integer ) );

		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, gl_tex_max);
	}
	else
	{
		if ( textureFilterAnisotropic )
			qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1 );

		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	}

#ifndef GL_VERSION_ES_CM_1_0
	if (depthimage) {
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
		glTexParameteri (GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_ALPHA);
	}
	if (softwaremode) {	// leilei - software speedup
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		}
#endif
	GL_CheckErrors();

	if ( scaledBuffer != 0 )
		ri.Hunk_FreeTempMemory( scaledBuffer );
	if ( resampledBuffer != 0 )
		ri.Hunk_FreeTempMemory( resampledBuffer );
}

//
// leilei - paletted texture support START
//
static void Upload8( unsigned *data, 
						  int width, int height, 
						  qboolean mipmap, 
						  qboolean picmip, 
							qboolean lightMap,
						  int *format, 
						  int *pUploadWidth, int *pUploadHeight  )
{
#ifndef GL_VERSION_ES_CM_1_0			// GLES1 doesn't know what a palette is.
	int			samples;
	unsigned	*scaledBuffer = NULL;
	unsigned	*resampledBuffer = NULL;
	unsigned char	*palettedBuffer = NULL;
	int			scaled_width, scaled_height;
	int			i, c;
	byte		*scan;
	GLenum		internalFormat = GL_RGB;
	GLenum		temp_GLformat = GL_RGBA;
	GLenum		temp_GLtype = GL_UNSIGNED_BYTE;
	int		texsizex, texsizey;	// leilei
	int		superfactor = 1;
	int		isalphaedrgba = 0;		// for cards that SCUK at it
	//unsigned char	*data8;
	
	//
	// convert to exact power of 2 sizes
	//
	for (scaled_width = 1 ; scaled_width < width ; scaled_width<<=1)
		;
	for (scaled_height = 1 ; scaled_height < height ; scaled_height<<=1)
		;
	if ( r_roundImagesDown->integer && scaled_width > width )
		scaled_width >>= 1;
	if ( r_roundImagesDown->integer && scaled_height > height )
		scaled_height >>= 1;

	scaled_width *= superfactor;
	scaled_height *= superfactor;

	if ( scaled_width != width || scaled_height != height ) {
		resampledBuffer = ri.Hunk_AllocateTempMemory( scaled_width * scaled_height * 4 );
		ResampleTexture (data, width, height, resampledBuffer, scaled_width, scaled_height);
		data = resampledBuffer;
		width = scaled_width;
		height = scaled_height;
	}

	//
	// perform optional picmip operation
	//
	if ( picmip ) {
		scaled_width >>= r_picmip->integer;
		scaled_height >>= r_picmip->integer;
	}

	//
	// clamp to minimum size
	//
	if (scaled_width < 1) {
		scaled_width = 1;
	}
	if (scaled_height < 1) {
		scaled_height = 1;
	}



	//
	// clamp to the current upper OpenGL limit
	// scale both axis down equally so we don't have to
	// deal with a half mip resampling
	//
	
	texsizex = glConfig.maxTextureSize;
	texsizey = glConfig.maxTextureSize;
	

		if ( (r_legacycard->integer == 1 && r_legacycard->integer == 2 && r_legacycard->integer == 4 && r_legacycard->integer == 5 && r_legacycard->integer == 1001) && (!force32upload)){	// leilei
	texsizex = 256;	// 3dfx 
	texsizey = 256;	// 3dfx 
	}






	while ( scaled_width > texsizex
		|| scaled_height > texsizey ) {
		scaled_width >>= 1;
		scaled_height >>= 1;
	}

	scaledBuffer = ri.Hunk_AllocateTempMemory( sizeof( unsigned ) * scaled_width * scaled_height );

	//
	// scan the texture for each channel's max values
	// and verify if the alpha channel is being used or not
	//
	c = width*height;
	scan = ((byte *)data);
	samples = 3;
	// LEILEI - paletted texturing hack
	// Check for an alpha

	for ( i = 0; i < c; i++ )
		{
			int a;
			a = scan[i*4 +3];
			a *= 1.9;
			a /= 255;
			a *= 255;
			if (a > 1 && voodootype ==1 )	isalphaedrgba = 1; // voodoo graphics fix
		}


	if (paletteability)
	 R_PickTexturePalette(1);


	if (paletteability && !isalphaedrgba)				// Preparing for native upload
	{

	for ( i = 0; i < c; i++ )
		{
			int thecol;
			int r, g, b, a;
			r = scan[i*4];
			g = scan[i*4 +1];
			b = scan[i*4 +2];
			a = scan[i*4 +3];
			thecol = palmap[r>>3][g>>3][b>>3];
			a *= 1.9;
			a /= 255;
			a *= 255;
			if (!a){
			thecol = 255; // transparent color
			}
			scan[i] = thecol;
			samples = 1;
		}

	}
	else						// Preparing for simulated RGBA upload
	{
	for ( i = 0; i < c; i++ )
		{
			int thecol;
			int r, g, b, a;
			unsigned char *pix24;
			r = scan[i*4];
			g = scan[i*4 +1];
			b = scan[i*4 +2];
			a = scan[i*4 +3];
			thecol = palmap[r>>3][g>>3][b>>3];
		

			a *= 1.9;
			a /= 255;
			a *= 255;
			
			pix24 = (unsigned char *)&d_8to24table[thecol]; 
			if (!a){
			thecol = 255; // transparent color
			samples = 4;
			}
			scan[i*4] = 	pix24[0];
			scan[i*4+1] = 	pix24[1];
			scan[i*4+2] = 	pix24[2];
			scan[i*4+3] = 	a;


		}
	}



	{

		// select proper internal format


		if ( samples == 1 )
		{
			internalFormat = GL_LUMINANCE;	// leilei - gl has no knowledge of a paletted format, so use this for imagelist
		}
		else if ( samples == 3 )
		{
			internalFormat = GL_RGB;
		}
		else if ( samples == 4 )
		{
			internalFormat = GL_RGBA;
		}

	}

	// copy or resample data as appropriate for first MIP level

	


	if ( ( scaled_width == width ) && 
		( scaled_height == height ) ) {
		if (!mipmap)
		{
	if (paletteability && !isalphaedrgba)	
		qglTexImage2D( GL_TEXTURE_2D,  0,  palettedformat,	  scaled_width,	  scaled_height,  0,	  GL_COLOR_INDEX,  GL_UNSIGNED_BYTE,  data);
			else
			qglTexImage2D (GL_TEXTURE_2D, 0, internalFormat, scaled_width, scaled_height, 0, temp_GLformat, temp_GLtype, data);
			*pUploadWidth = scaled_width;
			*pUploadHeight = scaled_height;
			*format = internalFormat;

			goto done;
		}
	if (!paletteability || isalphaedrgba)	
		Com_Memcpy (scaledBuffer, data, width*height*4);
	else	Com_Memcpy (scaledBuffer, data, width*height);

	}
	else
	{
		// use the normal mip-mapping function to go down from here
		while ( width > scaled_width || height > scaled_height ) {
	if (paletteability && !isalphaedrgba)	
			R_MipMap8( (byte *)data, width, height );
		else
			R_MipMap( (byte *)data, width, height );
			width >>= 1;
			height >>= 1;
			if ( width < 1 ) {
				width = 1;
			}
			if ( height < 1 ) {
				height = 1;
			}
		}
	if (!paletteability || isalphaedrgba)	
		Com_Memcpy( scaledBuffer, data, width * height * 4 );
	else	Com_Memcpy( scaledBuffer, data, width * height);
	}


	*pUploadWidth = scaled_width;
	*pUploadHeight = scaled_height;
	*format = internalFormat;


	if (paletteability && !isalphaedrgba)	
	qglTexImage2D( GL_TEXTURE_2D,  0,  palettedformat,	  scaled_width,	  scaled_height,  0,	  GL_COLOR_INDEX,  GL_UNSIGNED_BYTE,  scaledBuffer );
		else
	qglTexImage2D (GL_TEXTURE_2D, 0, internalFormat, scaled_width, scaled_height, 0, temp_GLformat, temp_GLtype, scaledBuffer );

	if (mipmap)
	{
		int		miplevel;

		miplevel = 0;
		while (scaled_width > 1 || scaled_height > 1)
		{
	if (paletteability && !isalphaedrgba)
			R_MipMap8( (byte *)scaledBuffer, scaled_width, scaled_height );
		else
			R_MipMap( (byte *)scaledBuffer, scaled_width, scaled_height );

			scaled_width >>= 1;
			scaled_height >>= 1;
			if (scaled_width < 1)
				scaled_width = 1;
			if (scaled_height < 1)
				scaled_height = 1;
			miplevel++;


	if (paletteability && !isalphaedrgba)
		qglTexImage2D( GL_TEXTURE_2D,  miplevel, palettedformat,  scaled_width,  scaled_height,  0,  GL_COLOR_INDEX,  GL_UNSIGNED_BYTE, scaledBuffer );
		else
		qglTexImage2D (GL_TEXTURE_2D, miplevel, internalFormat, scaled_width, scaled_height, 0, temp_GLformat, temp_GLtype, scaledBuffer );

		}
	}
done:

	if (mipmap)
	{
		if ( textureFilterAnisotropic )
			qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
					(GLint)Com_Clamp( 1, maxAnisotropy, r_ext_max_anisotropy->integer ) );

		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, gl_tex_max);
	}
	else
	{
		if ( textureFilterAnisotropic )
			qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1 );

		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	}

	if (softwaremode) {	// leilei - software speedup
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		}

	GL_CheckErrors();

	if ( scaledBuffer != 0 )
		ri.Hunk_FreeTempMemory( scaledBuffer );
	if ( resampledBuffer != 0 )
		ri.Hunk_FreeTempMemory( resampledBuffer );
	if ( palettedBuffer != 0 )
		ri.Hunk_FreeTempMemory( palettedBuffer );
#endif
}

//
// leilei - paletted texture support END
//



/*
================
R_CreateImage

This is the only way any image_t are created
================
*/
image_t *R_CreateImage( const char *name, byte *pic, int width, int height,
		imgType_t type, imgFlags_t flags, int internalFormat ) {
	image_t		*image;
	qboolean	isLightmap = qfalse;
	long		hash;
	int         glWrapClampMode;
	float oldtime = backEnd.refdef.floatTime;
	if (strlen(name) >= MAX_QPATH ) {
		ri.Error (ERR_DROP, "R_CreateImage: \"%s\" is too long", name);
	}
	if ( !strncmp( name, "*lightmap", 9 ) ) {
		isLightmap = qtrue;
	}

	if ( tr.numImages == MAX_DRAWIMAGES ) {
		ri.Error( ERR_DROP, "R_CreateImage: MAX_DRAWIMAGES hit");
	}

	image = tr.images[tr.numImages] = ri.Hunk_Alloc( sizeof( image_t ), h_low );
	image->texnum = 1024 + tr.numImages;
	tr.numImages++;

	image->type = type;
	image->flags = flags;

	strcpy (image->imgName, name);

	image->width = width;
	image->height = height;
	if (voodootype){			// leilei - 3dfx needs the old behavior
		if (flags & IMGFLAG_CLAMPTOEDGE)
			glWrapClampMode = GL_CLAMP; 
		else
			glWrapClampMode = GL_REPEAT;
	}
	else
	{
		if (flags & IMGFLAG_CLAMPTOEDGE)
			glWrapClampMode = GL_CLAMP_TO_EDGE; 
		else
			glWrapClampMode = GL_REPEAT;
	}

	// lightmaps are always allocated on TMU 1
	if ( qglActiveTextureARB && (isLightmap || detailhack == 2) ) {
		image->TMU = 2;
	} else {
		image->TMU = 0;
	}

	if ( qglActiveTextureARB ) {
		GL_SelectTexture( image->TMU );
	}

	GL_Bind(image);

#ifndef GL_VERSION_ES_CM_1_0			// GLES1 doesn't know palettes
	if (paletteavailable && r_texturebits->integer == 8 && !isLightmap && !depthimage && !force32upload)
	Upload8( (unsigned *)pic, image->width, image->height, 
								image->flags & IMGFLAG_MIPMAP,
								image->flags & IMGFLAG_PICMIP,
								isLightmap,
								&image->internalFormat,
								&image->uploadWidth,
								&image->uploadHeight );

	else
#endif
	Upload32( (unsigned *)pic, image->width, image->height, 
								image->flags & IMGFLAG_MIPMAP,
								image->flags & IMGFLAG_PICMIP,
								isLightmap,
								&image->internalFormat,
								&image->uploadWidth,
								&image->uploadHeight );

	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, glWrapClampMode );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, glWrapClampMode );

	// FIXME: this stops fog from setting border color?
	glState.currenttextures[glState.currenttmu] = 0;
	qglBindTexture( GL_TEXTURE_2D, 0 );

	if ( image->TMU == 1 ) {
		GL_SelectTexture( 0 );
	}

	// leilei - map texture listing hack

	image->maptexture = ismaptexture;

	hash = generateHashValue(name);
	image->next = hashTable[hash];
	hashTable[hash] = image;

	image->procTime = backEnd.refdef.floatTime - oldtime;
	return image;
}

//===================================================================

typedef struct
{
	char *ext;
	void (*ImageLoader)( const char *, unsigned char **, int *, int * );
} imageExtToLoaderMap_t;

// Note that the ordering indicates the order of preference used
// when there are multiple images of different formats available
static imageExtToLoaderMap_t imageLoaders[ ] =
{
	{ "tga",  R_LoadTGA },
	{ "jpg",  R_LoadJPG },
	{ "jpeg", R_LoadJPG },
	{ "png",  R_LoadPNG },
	{ "pcx",  R_LoadPCX },
	{ "bmp",  R_LoadBMP }
};

static int numImageLoaders = ARRAY_LEN( imageLoaders );

/*
=================
R_LoadImage

Loads any of the supported image types into a cannonical
32 bit format.
=================
*/
void R_LoadImage( const char *name, byte **pic, int *width, int *height )
{
	qboolean orgNameFailed = qfalse;
	int orgLoader = -1;
	int i;
	char localName[ MAX_QPATH ];
	const char *ext;
	char *altName;

	*pic = NULL;
	*width = 0;
	*height = 0;

	Q_strncpyz( localName, name, MAX_QPATH );

	ext = COM_GetExtension( localName );



	if( *ext )
	{
		// Look for the correct loader and use it
		for( i = 0; i < numImageLoaders; i++ )
		{
			if( !Q_stricmp( ext, imageLoaders[ i ].ext ) )
			{
				// Load
				imageLoaders[ i ].ImageLoader( localName, pic, width, height );
				break;
			}
		}

		// A loader was found
		if( i < numImageLoaders )
		{
			if( *pic == NULL )
			{
				// Loader failed, most likely because the file isn't there;
				// try again without the extension
				orgNameFailed = qtrue;
				orgLoader = i;
				COM_StripExtension( name, localName, MAX_QPATH );
			}
			else
			{
				// Something loaded
				return;
			}
		}
	}

	// Try and find a suitable match using all
	// the image formats supported
	for( i = 0; i < numImageLoaders; i++ )
	{
		if (i == orgLoader)
			continue;

		altName = va( "%s.%s", localName, imageLoaders[ i ].ext );

		// Load
		imageLoaders[ i ].ImageLoader( altName, pic, width, height );

		if( *pic )
		{
			if( orgNameFailed )
			{
				ri.Printf( PRINT_DEVELOPER, "WARNING: %s not present, using %s instead\n",
						name, altName );
			}

			break;
		}
	}
}


/*
===============
R_FindImageFile

Finds or loads the given image.
Returns NULL if it fails, not a default image.
==============
*/
image_t	*R_FindImageFile( const char *name, imgType_t type, imgFlags_t flags )
{
	image_t	*image;
	int		width, height;
	int dontgotsafe;
	byte	*pic;
	long	hash;
	float oldtime;
	float loadtime;
	float proctime;

	if (!name) {
		return NULL;
	}

	detailhack = 0;			
	hash = generateHashValue(name);

	//
	// see if the image is already loaded
	//
	for (image=hashTable[hash]; image; image=image->next) {
		if ( !strcmp( name, image->imgName ) ) {
			// the white image can be used with any set of parms, but other mismatches are errors
			if ( strcmp( name, "*white" ) ) {
				if ( image->flags != flags ) {
					ri.Printf( PRINT_DEVELOPER, "WARNING: reused image %s with mixed flags (%i vs %i)\n", name, image->flags, flags );
				}
			}
			return image;
		}
	}

	// leilei - Detail texture hack
	//	    to kill artifacts of shimmer of pattern of terrible
	if ( !Q_strncmp( name, "textures/detail/", 16 )  || !Q_strncmp( name, "gfx/fx/detail/", 14 ))  {
		ri.Printf( PRINT_DEVELOPER, "DETAILHACK: %s - mips will be gray\n", name );
		detailhack = 1;		// leilei - attempt to fade detail mips to gray, EXPECTS DST_COLOR/SRC_COLOR for this to work right
		if ( r_detailTextureTMU->value ) 
			detailhack = 2; 
		}

	// leilei - iconmip hack

	if ( !Q_strncmp( name, "icons/", 5 )  || ((!Q_strncmp( name, "gfx/2d", 6 )) && (Q_strncmp( name, "gfx/2d/bigchars", 14 )))){
		isicon = 1;
		}
	else
		isicon = 0;

	//
	// load the pic from disk
	//

	oldtime = ri.Milliseconds() * 100;
	dontgotsafe = 0;
	if (r_suggestiveThemes->integer == 1) dontgotsafe = 1;


	// leilei - load safe or lewd textures if desired.
	if (!Q_strncmp( name, "models/player", 13) ){

		
		
		if (r_suggestiveThemes->integer < 1){
			char	narm[ MAX_QPATH ];
			COM_StripExtension( name, narm, MAX_QPATH );
			R_LoadImage( va("%s_safe", narm), &pic, &width, &height );
			if ( pic == NULL ) 
				dontgotsafe = 1;
			else
				dontgotsafe = 0;
			
		}
	
		else if (r_suggestiveThemes->integer > 1){
			char	narm[ MAX_QPATH ];
			COM_StripExtension( name, narm, MAX_QPATH );
			R_LoadImage( va("%s_lewd", narm), &pic, &width, &height );
			if ( pic == NULL ) 
				dontgotsafe = 1;
			else
				dontgotsafe = 0;
			
		}
	}
	
	else
	{
		dontgotsafe = 1;
	}

	//oldtime = backEnd.refdef.floatTime;


	if (dontgotsafe){
	R_LoadImage( name, &pic, &width, &height );
		if ( pic == NULL ) {
			return NULL;
		}
	}
	loadtime = (ri.Milliseconds() * 100) - oldtime;
//	loadtime = backEnd.refdef.floatTime - oldtime;


	// leilei - if we need to change the texture upload with a special image prefix to separate from differently blended things
	if (hackoperation)
		{
			char hackName[MAX_QPATH];
			char *hackedName;
			COM_StripExtension( name, hackName, MAX_QPATH );
			if(hackoperation==1) hackedName = va("%shackadd", hackName);
			if(hackoperation==2) hackedName = va("%shacknob", hackName);
			if(hackoperation==3) hackedName = va("%shacksub", hackName);
			if(hackoperation==4) hackedName = va("%shackmod", hackName);
			else hackedName = va("%shackblend", hackName);
			image = R_CreateImage( ( char * ) hackedName, pic, width, height, type, flags, 0 );
		}

	else

	{
	image = R_CreateImage( ( char * ) name, pic, width, height, type, flags, 0 );
	}


	ri.Free( pic );
	

	image->loadTime = loadtime;

	return image;
}



/*
===============
R_FindImageFileIfItsThere

leilei
Finds the given image and does not load it.
==============
*/
image_t	*R_FindImageFileIfItsThere( const char *name, imgType_t type, imgFlags_t flags )
{
	image_t	*image;
	int		width, height;
	byte	*pic;
	long	hash;

	if (!name) {
		return NULL;
	}

	detailhack = 0;			
	hash = generateHashValue(name);

	//
	// see if the image is already loaded
	//
	for (image=hashTable[hash]; image; image=image->next) {
		if ( !strcmp( name, image->imgName ) ) {
			// the white image can be used with any set of parms, but other mismatches are errors
			if ( strcmp( name, "*white" ) ) {
				if ( image->flags != flags ) {
					ri.Printf( PRINT_DEVELOPER, "WARNING: reused image %s with mixed flags (%i vs %i)\n", name, image->flags, flags );
				}
			}
			return image;
		}
	}

	// leilei - Detail texture hack
	//	    to kill artifacts of shimmer of pattern of terrible
	if ( !Q_strncmp( name, "textures/detail/", 16 )  || !Q_strncmp( name, "gfx/fx/detail/", 14 ))  {
		ri.Printf( PRINT_DEVELOPER, "DETAILHACK: %s - mips will be gray\n", name );
		detailhack = 1;		// leilei - attempt to fade detail mips to gray, EXPECTS DST_COLOR/SRC_COLOR for this to work right
		if ( r_detailTextureTMU->value ) 
			detailhack = 2; 
		}



	//
	// load the pic from disk
	//
	R_LoadImage( name, &pic, &width, &height );
	if ( pic == NULL ) {
		return NULL;
	}

	image = R_CreateImage( ( char * ) name, pic, width, height, type, flags, 0 );
	ri.Free( pic );
	ismaptexture = 0;
	return image;
}


/*
================
R_CreateDlightImage
================
*/
#define	DLIGHT_SIZE	16
static void R_CreateDlightImage( void ) {
	int		x,y;
	byte	data[DLIGHT_SIZE][DLIGHT_SIZE][4];
	int		b;

	// make a centered inverse-square falloff blob for dynamic lighting
	for (x=0 ; x<DLIGHT_SIZE ; x++) {
		for (y=0 ; y<DLIGHT_SIZE ; y++) {
			float	d;

			d = ( DLIGHT_SIZE/2 - 0.5f - x ) * ( DLIGHT_SIZE/2 - 0.5f - x ) +
				( DLIGHT_SIZE/2 - 0.5f - y ) * ( DLIGHT_SIZE/2 - 0.5f - y );
			b = 4000 / d;
			if (b > 255) {
				b = 255;
			} else if ( b < 75 ) {
				b = 0;
			}
			data[y][x][0] = 
			data[y][x][1] = 
			data[y][x][2] = b;
			data[y][x][3] = 255;			
		}
	}
	tr.dlightImage = R_CreateImage("*dlight", (byte *)data, DLIGHT_SIZE, DLIGHT_SIZE, IMGTYPE_COLORALPHA, IMGFLAG_CLAMPTOEDGE, 0 );
//	tr.dlightImage = R_CreateImage("*dlight", (byte *)data, DLIGHT_SIZE, DLIGHT_SIZE, IMGTYPE_COLORALPHA, IMGFLAG_CLAMPTOEDGE, 0 );

}


static void R_CreateDlightImageEx( int size ) {
	int		x,y;
	byte	data[size][size][4];
	int		b;

	// make a centered inverse-square falloff blob for dynamic lighting
	for (x=0 ; x<size ; x++) {
		for (y=0 ; y<size ; y++) {
			float	d;

			d = ( size/2 - 0.5f - x ) * ( size/2 - 0.5f - x ) +
				( size/2 - 0.5f - y ) * ( size/2 - 0.5f - y );
			b = ((4000/16) * (size*2)) / d;
			if (b > 255) {
				b = 255;
			} else if ( b < 75 ) {
				b = 0;
			}
			data[y][x][0] = b;
			data[y][x][1] = b;
			data[y][x][2] = b;
			data[y][x][3] = b;			
		}
	}
	tr.dlightImage = R_CreateImage("*dlight", (byte *)data, size, size, IMGTYPE_COLORALPHA, IMGFLAG_CLAMPTOEDGE, 0 );
//	tr.dlightImage = R_CreateImage("*dlight", (byte *)data, DLIGHT_SIZE, DLIGHT_SIZE, IMGTYPE_COLORALPHA, IMGFLAG_CLAMPTOEDGE, 0 );

}


/*
=================
R_InitFogTable
=================
*/
void R_InitFogTable( void ) {
	int		i;
	float	d;
	float	exp;
	
	exp = 0.5;

	for ( i = 0 ; i < FOG_TABLE_SIZE ; i++ ) {
		d = pow ( (float)i/(FOG_TABLE_SIZE-1), exp );

		tr.fogTable[i] = d;
	}
}

/*
================
R_FogFactor

Returns a 0.0 to 1.0 fog density value
This is called for each texel of the fog texture on startup
and for each vertex of transparent shaders in fog dynamically
================
*/
float	R_FogFactor( float s, float t ) {
	float	d;

	s -= 1.0/512;
	if ( s < 0 ) {
		return 0;
	}
	if ( t < 1.0/32 ) {
		return 0;
	}
	if ( t < 31.0/32 ) {
		s *= (t - 1.0f/32.0f) / (30.0f/32.0f);
	}

	// we need to leave a lot of clamp range
	s *= 8;

	if ( s > 1.0 ) {
		s = 1.0;
	}

	d = tr.fogTable[ (int)(s * (FOG_TABLE_SIZE-1)) ];

	return d;
}

/*
================
R_CreateFogImage
================
*/
#define	FOG_S	256
#define	FOG_T	32
static void R_CreateFogImage( void ) {
	int		x,y;
	byte	*data;
	float	d;
	float	borderColor[4];
	force32upload = 1;		// leilei - paletted fog fix
	data = ri.Hunk_AllocateTempMemory( FOG_S * FOG_T * 4 );

	// S is distance, T is depth
	for (x=0 ; x<FOG_S ; x++) {
		for (y=0 ; y<FOG_T ; y++) {
		//	d = R_FogFactor( ( x + 0.5f ) / FOG_S, ( y + 0.5f ) / FOG_T );
			d = R_FogFactor( ( x + 0.5f ) / FOG_S, ( y + 0.5f ) / FOG_T );


			data[(y*FOG_S+x)*4+0] = 
			data[(y*FOG_S+x)*4+1] = 
			data[(y*FOG_S+x)*4+2] = 255;
			data[(y*FOG_S+x)*4+3] = 255*d;
		}
	}


	// standard openGL clamping doesn't really do what we want -- it includes
	// the border color at the edges.  OpenGL 1.2 has clamp-to-edge, which does
	// what we want.
	tr.fogImage = R_CreateImage("*fog", (byte *)data, FOG_S, FOG_T, IMGTYPE_COLORALPHA, IMGFLAG_CLAMPTOEDGE, 0 );
	ri.Hunk_FreeTempMemory( data );

	borderColor[0] = 1.0;
	borderColor[1] = 1.0;
	borderColor[2] = 1.0;
	borderColor[3] = 1;

#ifndef GL_VERSION_ES_CM_1_0
	qglTexParameterfv( GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor );
#endif
	force32upload = 0;		// leilei - paletted fog fix
}

/*
==================
R_CreateDefaultImage
==================
*/
#define	DEFAULT_SIZE	16
static void R_CreateDefaultImage( void ) {
	int		x;
	byte	data[DEFAULT_SIZE][DEFAULT_SIZE][4];

	// the default image will be a box, to allow you to see the mapping coordinates
	Com_Memset( data, 32, sizeof( data ) );
	for ( x = 0 ; x < DEFAULT_SIZE ; x++ ) {
		data[0][x][0] =
		data[0][x][1] =
		data[0][x][2] =
		data[0][x][3] = 255;

		data[x][0][0] =
		data[x][0][1] =
		data[x][0][2] =
		data[x][0][3] = 255;

		data[DEFAULT_SIZE-1][x][0] =
		data[DEFAULT_SIZE-1][x][1] =
		data[DEFAULT_SIZE-1][x][2] =
		data[DEFAULT_SIZE-1][x][3] = 255;

		data[x][DEFAULT_SIZE-1][0] =
		data[x][DEFAULT_SIZE-1][1] =
		data[x][DEFAULT_SIZE-1][2] =
		data[x][DEFAULT_SIZE-1][3] = 255;
	}
	tr.defaultImage = R_CreateImage("*default", (byte *)data, DEFAULT_SIZE, DEFAULT_SIZE, IMGTYPE_COLORALPHA, IMGFLAG_MIPMAP, 0);
}

/*
==================
R_CreateBuiltinImages
==================
*/
void R_CreateBuiltinImages( void ) {
	int		x,y;
	byte	data[DEFAULT_SIZE][DEFAULT_SIZE][4];

	hackoperation = 0;
	R_CreateDefaultImage();

	// we use a solid white image instead of disabling texturing
	Com_Memset( data, 255, sizeof( data ) );
	ismaptexture = 0;
	tr.whiteImage = R_CreateImage("*white", (byte *)data, 8, 8, IMGTYPE_COLORALPHA, IMGFLAG_NONE, 0);

	// with overbright bits active, we need an image which is some fraction of full color,
	// for default lightmaps, etc
	for (x=0 ; x<DEFAULT_SIZE ; x++) {
		for (y=0 ; y<DEFAULT_SIZE ; y++) {
			data[y][x][0] = 
			data[y][x][1] = 
			data[y][x][2] = tr.identityLightByte;
			data[y][x][3] = 255;			
		}
	}

	tr.identityLightImage = R_CreateImage("*identityLight", (byte *)data, 8, 8, IMGTYPE_COLORALPHA, IMGFLAG_NONE, 0);


	for(x=0;x<32;x++) {
		// scratchimage is usually used for cinematic drawing
		tr.scratchImage[x] = R_CreateImage("*scratch", (byte *)data, DEFAULT_SIZE, DEFAULT_SIZE, IMGTYPE_COLORALPHA, IMGFLAG_PICMIP | IMGFLAG_CLAMPTOEDGE, 0);
	}


	R_CreateDlightImage();
	//R_CreateDlightImageEx( 128 );

	R_CreateFogImage();
	//tr.fogImage = R_FindImageFile( "gfx/engine/fog.tga", 0, IMGFLAG_CLAMPTOEDGE )
	//tr.dlightImage = R_FindImageFile( "gfx/engine/dlight.tga", 0, IMGFLAG_CLAMPTOEDGE );
}


extern cvar_t *r_alternateBrightness;
/*
===============
R_SetColorMappings
===============
*/
void R_SetColorMappings( void ) {
	int		i, j;
	float	g;
	int		inf;
	int		shift;

	// setup the overbright lighting
	tr.overbrightBits = r_overBrightBits->integer;
	if ( !glConfig.deviceSupportsGamma && !r_alternateBrightness->integer) {
		tr.overbrightBits = 0;		// need hardware gamma for overbright
	}

	// never overbright in windowed mode
	if ( !glConfig.isFullscreen && !r_alternateBrightness->integer)
	{
		tr.overbrightBits = 0;
	}

	// allow 2 overbright bits in 24 bit, but only 1 in 16 bit
	if ( glConfig.colorBits > 16 ) {
		if ( tr.overbrightBits > 2 ) {
			tr.overbrightBits = 2;
		}
	} else {
		if ( tr.overbrightBits > 1 ) {
			tr.overbrightBits = 1;
		}
	}
	if ( tr.overbrightBits < 0 ) {
		tr.overbrightBits = 0;
	}

	tr.identityLight = 1.0f / ( 1 << tr.overbrightBits );
	tr.identityLightByte = 255 * tr.identityLight;


	if ( r_intensity->value <= 1 ) {
		ri.Cvar_Set( "r_intensity", "1" );
	}

	if ( r_gamma->value < 0.5f ) {
		ri.Cvar_Set( "r_gamma", "0.5" );
	} else if ( r_gamma->value > 3.0f ) {
		ri.Cvar_Set( "r_gamma", "3.0" );
	}

	g = r_gamma->value;

	if (r_alternateBrightness->integer != 1)	// leilei - don't do the shift to the brightness when we do alternate. This allows
	shift = tr.overbrightBits;		// hardware gamma to work (if available) since we can't do alternate gamma via blends
	else shift = 0;	// don't

	for ( i = 0; i < 256; i++ ) {
		if ( g == 1 ) {
			inf = i;
		} else {
			inf = 255 * pow ( i/255.0f, 1.0f / g ) + 0.5f;
		}
		inf <<= shift;
		if (inf < 0) {
			inf = 0;
		}
		if (inf > 255) {
			inf = 255;
		}
		s_gammatable[i] = inf;
	}

	for (i=0 ; i<256 ; i++) {
		j = i * 1;	// leilei - disable this because it breaks everything.
		if (j > 255) {
			j = 255;
		}
		s_intensitytable[i] = j;
	}

	if ( glConfig.deviceSupportsGamma && r_alternateBrightness->integer != 2)
	{
		GLimp_SetGamma( s_gammatable, s_gammatable, s_gammatable );
	}
}

/*
===============
R_InitImages
===============
*/
void	R_InitImages( void ) {
	Com_Memset(hashTable, 0, sizeof(hashTable));
	// build brightness translation tables
	R_SetColorMappings();

	// leilei - paletted texture support
#ifndef GL_VERSION_ES_CM_1_0		// GLES1 thinks it's too good for palettes
	if (r_texturebits->integer == 8)
	R_InitPalette();
#endif
	// create default texture and white texture
	R_CreateBuiltinImages();
}

/*
===============
R_DeleteTextures
===============
*/
void R_DeleteTextures( void ) {
	int		i;

	for ( i=0; i<tr.numImages ; i++ ) {
		qglDeleteTextures( 1, &tr.images[i]->texnum );
	}
	Com_Memset( tr.images, 0, sizeof( tr.images ) );

	tr.numImages = 0;

	Com_Memset( glState.currenttextures, 0, sizeof( glState.currenttextures ) );
	if ( qglActiveTextureARB ) {
		GL_SelectTexture( 1 );
		qglBindTexture( GL_TEXTURE_2D, 0 );
		GL_SelectTexture( 0 );
		qglBindTexture( GL_TEXTURE_2D, 0 );
	} else {
		qglBindTexture( GL_TEXTURE_2D, 0 );
	}
}

/*
============================================================================

SKINS

============================================================================
*/

/*
==================
CommaParse

This is unfortunate, but the skin files aren't
compatable with our normal parsing rules.
==================
*/
static char *CommaParse( char **data_p ) {
	int c = 0, len;
	char *data;
	static	char	com_token[MAX_TOKEN_CHARS];

	data = *data_p;
	len = 0;
	com_token[0] = 0;

	// make sure incoming data is valid
	if ( !data ) {
		*data_p = NULL;
		return com_token;
	}

	while ( 1 ) {
		// skip whitespace
		while( (c = *data) <= ' ') {
			if( !c ) {
				break;
			}
			data++;
		}


		c = *data;

		// skip double slash comments
		if ( c == '/' && data[1] == '/' )
		{
			while (*data && *data != '\n')
				data++;
		}
		// skip /* */ comments
		else if ( c=='/' && data[1] == '*' ) 
		{
			while ( *data && ( *data != '*' || data[1] != '/' ) ) 
			{
				data++;
			}
			if ( *data ) 
			{
				data += 2;
			}
		}
		else
		{
			break;
		}
	}

	if ( c == 0 ) {
		return "";
	}

	// handle quoted strings
	if (c == '\"')
	{
		data++;
		while (1)
		{
			c = *data++;
			if (c=='\"' || !c)
			{
				com_token[len] = 0;
				*data_p = ( char * ) data;
				return com_token;
			}
			if (len < MAX_TOKEN_CHARS)
			{
				com_token[len] = c;
				len++;
			}
		}
	}

	// parse a regular word
	do
	{
		if (len < MAX_TOKEN_CHARS)
		{
			com_token[len] = c;
			len++;
		}
		data++;
		c = *data;
	} while (c>32 && c != ',' );

	if (len == MAX_TOKEN_CHARS)
	{
//		ri.Printf (PRINT_DEVELOPER, "Token exceeded %i chars, discarded.\n", MAX_TOKEN_CHARS);
		len = 0;
	}
	com_token[len] = 0;

	*data_p = ( char * ) data;
	return com_token;
}


/*
===============
RE_RegisterSkin

===============
*/
qhandle_t RE_RegisterSkin( const char *name ) {
	qhandle_t	hSkin;
	skin_t		*skin;
	skinSurface_t	*surf;
	union {
		char *c;
		void *v;
	} text;
	char		*text_p;
	char		*token;
	char		surfName[MAX_QPATH];

	if ( !name || !name[0] ) {
		ri.Printf( PRINT_DEVELOPER, "Empty name passed to RE_RegisterSkin\n" );
		return 0;
	}

	if ( strlen( name ) >= MAX_QPATH ) {
		ri.Printf( PRINT_DEVELOPER, "Skin name exceeds MAX_QPATH\n" );
		return 0;
	}


	// see if the skin is already loaded
	for ( hSkin = 1; hSkin < tr.numSkins ; hSkin++ ) {
		skin = tr.skins[hSkin];
		if ( !Q_stricmp( skin->name, name ) ) {
			if( skin->numSurfaces == 0 ) {
				return 0;		// default skin
			}
			return hSkin;
		}
	}

	// allocate a new skin
	if ( tr.numSkins == MAX_SKINS ) {
		ri.Printf( PRINT_WARNING, "WARNING: RE_RegisterSkin( '%s' ) MAX_SKINS hit\n", name );
		return 0;
	}
	tr.numSkins++;
	skin = ri.Hunk_Alloc( sizeof( skin_t ), h_low );
	tr.skins[hSkin] = skin;
	Q_strncpyz( skin->name, name, sizeof( skin->name ) );
	skin->numSurfaces = 0;

	R_IssuePendingRenderCommands();

	// If not a .skin file, load as a single shader
	if ( strcmp( name + strlen( name ) - 5, ".skin" ) ) {
		skin->numSurfaces = 1;
		skin->surfaces[0] = ri.Hunk_Alloc( sizeof(skin->surfaces[0]), h_low );
		skin->surfaces[0]->shader = R_FindShader( name, LIGHTMAP_NONE, qtrue );
		return hSkin;
	}

	// load and parse the skin file
    ri.FS_ReadFile( name, &text.v );
	if ( !text.c ) {
		return 0;
	}

	text_p = text.c;
	while ( text_p && *text_p ) {
		// get surface name
		token = CommaParse( &text_p );
		Q_strncpyz( surfName, token, sizeof( surfName ) );

		if ( !token[0] ) {
			break;
		}
		// lowercase the surface name so skin compares are faster
		Q_strlwr( surfName );

		if ( *text_p == ',' ) {
			text_p++;
		}

		if ( strstr( token, "tag_" ) ) {
			continue;
		}
		
		// parse the shader name
		token = CommaParse( &text_p );

		if ( skin->numSurfaces >= MD3_MAX_SURFACES ) {
			ri.Printf( PRINT_WARNING, "WARNING: Ignoring surfaces in '%s', the max is %d surfaces!\n", name, MD3_MAX_SURFACES );
			break;
		}

		surf = skin->surfaces[ skin->numSurfaces ] = ri.Hunk_Alloc( sizeof( *skin->surfaces[0] ), h_low );
		Q_strncpyz( surf->name, surfName, sizeof( surf->name ) );
		surf->shader = R_FindShader( token, LIGHTMAP_NONE, qtrue );
		skin->numSurfaces++;
	}

	ri.FS_FreeFile( text.v );


	// never let a skin have 0 shaders
	if ( skin->numSurfaces == 0 ) {
		return 0;		// use default skin
	}

	return hSkin;
}


/*
===============
R_InitSkins
===============
*/
void	R_InitSkins( void ) {
	skin_t		*skin;

	tr.numSkins = 1;

	// make the default skin have all default shaders
	skin = tr.skins[0] = ri.Hunk_Alloc( sizeof( skin_t ), h_low );
	Q_strncpyz( skin->name, "<default skin>", sizeof( skin->name )  );
	skin->numSurfaces = 1;
	skin->surfaces[0] = ri.Hunk_Alloc( sizeof( *skin->surfaces ), h_low );
	skin->surfaces[0]->shader = tr.defaultShader;
}

/*
===============
R_GetSkinByHandle
===============
*/
skin_t	*R_GetSkinByHandle( qhandle_t hSkin ) {
	if ( hSkin < 1 || hSkin >= tr.numSkins ) {
		return tr.skins[0];
	}
	return tr.skins[ hSkin ];
}

/*
===============
R_SkinList_f
===============
*/
void	R_SkinList_f( void ) {
	int			i, j;
	skin_t		*skin;

	ri.Printf (PRINT_ALL, "------------------\n");

	for ( i = 0 ; i < tr.numSkins ; i++ ) {
		skin = tr.skins[i];

		ri.Printf( PRINT_ALL, "%3i:%s\n", i, skin->name );
		for ( j = 0 ; j < skin->numSurfaces ; j++ ) {
			ri.Printf( PRINT_ALL, "       %s = %s\n", 
				skin->surfaces[j]->name, skin->surfaces[j]->shader->name );
		}
	}
	ri.Printf (PRINT_ALL, "------------------\n");
}

