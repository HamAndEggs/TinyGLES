/*
   Copyright (C) 2017, Richard e Collins.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
	Original code base is at https://github.com/HamAndEggs/TinyGLES   
   
*/
#include "TinyGLES.h"

#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <string_view>

#include <math.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdarg>

#include <sys/stat.h>
#include <linux/input.h>
#include <linux/fb.h>
#include <linux/videodev2.h>

#ifdef PLATFORM_X11_GL
	#define GL_GLEXT_PROTOTYPES
	#include <X11/Xlib.h>
	#include <X11/Xutil.h>
	#include <GL/gl.h>
	#include <GL/glext.h>
	#include <GL/glx.h>
	#include <GL/glu.h>
#endif

// This is for RPi 3,2 and 1 (including zero)
#ifdef BROADCOM_NATIVE_WINDOW // All included from /opt/vc/include
	#include "bcm_host.h"
#endif

// This is for linux systems that have no window manager. Like RPi4 running their light version of raspbian or a distro built with Yocto.
#ifdef PLATFORM_DIRECT_RENDER_MANAGER
//sudo apt install libdrm
	#include <xf86drm.h>
	#include <xf86drmMode.h>
	#include <gbm.h>	// This is used to get the egl stuff going. DRM is used to do the page flip to the display. Goes.. DRM -> GDM -> GLES (I think)
	#include <drm_fourcc.h>
	#include "EGL/egl.h"

	#define EGL_NO_X11
	#define MESA_EGL_NO_X11_HEADERS

#endif

#ifdef PLATFORM_GLES
	#include "GLES2/gl2.h"
#endif

#ifdef PLATFORM_EGL
	#include "EGL/egl.h"
#endif


namespace tinygles{	// Using a namespace to try to prevent name clashes as my class name is kind of obvious. :)
///////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef DEBUG_BUILD
	/**
	 * @brief For debug and when verbose mode is on will display errors coming from GLES.
	 * In debug and verbose is false, will say the error once.
	 * In release code is not included as checking errors all the time can stall the pipeline.
	 * @param pSource_file_name 
	 * @param pLine_number 
	 */
	static void ReadOGLErrors(const char *pSource_file_name,int pLine_number);

	#define CHECK_OGL_ERRORS()	ReadOGLErrors(__FILE__,__LINE__)
#else
	#define CHECK_OGL_ERRORS()
#endif

#ifdef VERBOSE_BUILD
	#define VERBOSE_MESSAGE(THE_MESSAGE__)	{std::clog << THE_MESSAGE__ << "\n";}
#else
	#define VERBOSE_MESSAGE(THE_MESSAGE__)
#endif

#ifdef VERBOSE_SHADER_BUILD
	static std::string gCurrentShaderName;
	#define VERBOSE_SHADER_MESSAGE(THE_MESSAGE__)	{std::clog << "Shader: " << THE_MESSAGE__ << "\n";}
#else
	#define VERBOSE_SHADER_MESSAGE(THE_MESSAGE__)
#endif


#define THROW_MEANINGFUL_EXCEPTION(THE_MESSAGE__)	{throw std::runtime_error("At: " + std::to_string(__LINE__) + " In " + std::string(__FILE__) + " : " + std::string(THE_MESSAGE__));}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal structures.

struct Vert2Df
{
	float x,y;
};

struct Vec2Db
{
	Vec2Db() = default;
	Vec2Db(int8_t pX,int8_t pY):x(pX),y(pY){};
	int8_t x,y;
};

struct Quad2D
{
	VertShortXY v[4];

	const int16_t* data()const{return &v[0].x;}
};

struct Quad2Df
{
	Vert2Df v[4];

	const float* data()const{return &v[0].x;}
};

/**
 * @brief This is a pain in the arse, because we can't query the values used to create a gl texture we have to store them. horrid API GLES 2.0
 */
struct GLTexture
{
	GLTexture() = delete; // Forces use to use references.
	GLTexture(TextureFormat pFormat,int pWidth,int pHeight):mFormat(pFormat),mWidth(pWidth),mHeight(pHeight){}

	const TextureFormat mFormat;
	const int mWidth;
	const int mHeight;
};

/**
 * @brief Defines our nine patch
 */
struct NinePatch
{
	NinePatch() = delete;

	NinePatch(int pWidth,int pHeight,const VertShortXY& pScaleFrom,const VertShortXY& pScaleTo,const VertShortXY& pFillFrom,const VertShortXY& pFillTo)
	{
		mScalable.from = pScaleFrom;
		mScalable.to = pScaleTo;

		mFillable.from = pFillFrom;
		mFillable.to = pFillTo;

		// Now build the verts, that are zero offset, when we render we'll add x and y to them and scale.
		int k = 0;
		const std::array<int,4>YCords = {0,pScaleFrom.y,pScaleTo.y,pHeight};
		for( int y : YCords )
		{
			int n = 0;
			const std::array<int,4>XCords = {0,pScaleFrom.x,pScaleTo.y,pWidth};
			for( int x : XCords )
			{
				mVerts[n][k].x = x;
				mVerts[n][k].y = y;

				// Not happy with why I had to swap these around but not for the verts. I need to investigate at some point. Could be to do with the normalization flag.
				mUVs[k][n].x = (0x7fff * x) / pWidth;
				mUVs[k][n].y = (0x7fff * y) / pHeight;
				n++;
			}
			k++;
		}
	}

	struct
	{
		VertShortXY from,to;
	}mScalable,mFillable;

	VertShortXY mVerts[4][4];
	VertShortXY mUVs[4][4];
};

enum struct StreamIndex
{
	VERTEX				= 0,		//!< Vertex positional data.
	TEXCOORD			= 1,		//!< Texture coordinate information.
	COLOUR				= 2,		//!< Colour type is in the format RGBA.
	TRANSFORM			= 3,		//!< Used for sprite batches.
};

/**
 * @brief Defines a sprite that has a lot of the work needed to render pre-computed with position, rotation and scale done in the shader for speed.
 */
struct Sprite
{
	uint32_t mTexture;
	float mWidth,mHeight,mCX,mCY;
	Quad2Df mVert;
	Quad2D mUV;

	void BuildVerts()
	{
		mVert.v[0].x = -mCX;
		mVert.v[0].y = -mCY;

		mVert.v[1].x = mWidth - mCX;
		mVert.v[1].y = -mCY;

		mVert.v[2].x = mWidth - mCX;
		mVert.v[2].y = mHeight - mCY;

		mVert.v[3].x = -mCX;
		mVert.v[3].y = mHeight - mCY;
	}

	void BuildUVs(int pTextureWidth,int pTextureHeight,int pTexFromX,int pTexFromY,int pTexToX,int pTexToY)
	{
		auto scaleUV = [](int pSize,int pCoord)
		{
			return (0x7fff * pCoord) / pSize;
		};

		mUV.v[0].x = scaleUV(pTextureWidth,pTexFromX);
		mUV.v[0].y = scaleUV(pTextureHeight,pTexFromY);

		mUV.v[1].x = scaleUV(pTextureWidth,pTexToX);
		mUV.v[1].y = scaleUV(pTextureHeight,pTexFromY);

		mUV.v[2].x = scaleUV(pTextureWidth,pTexToX);
		mUV.v[2].y = scaleUV(pTextureHeight,pTexToY);

		mUV.v[3].x = scaleUV(pTextureWidth,pTexFromX);
		mUV.v[3].y = scaleUV(pTextureHeight,pTexToY);
	}
};

/**
 * @brief Represents a large number of quads, handy when you want to render many at once. Like particles.
 */
struct QuadBatch
{
	const uint32_t mNumQuads;
	uint32_t mTexture;

	std::vector<Quad2D> mUVs;
	std::vector<QuadBatchTransform> mTransforms;

	inline size_t GetNumQuads()const{return mNumQuads;}

	QuadBatch(int pCount,uint32_t pTexture,int pTextureWidth,int pTextureHeight,int pTexFromX,int pTexFromY,int pTexToX,int pTexToY) :
		mNumQuads(pCount),
		mTexture(pTexture)
	{
		mTransforms.resize(pCount);

		auto scaleUV = [](int pSize,int pCoord)
		{
			return (0x7fff * pCoord) / pSize;
		};

		Quad2D uv;
		uv.v[0].x = scaleUV(pTextureWidth,pTexFromX);
		uv.v[0].y = scaleUV(pTextureHeight,pTexFromY);
		uv.v[1].x = scaleUV(pTextureWidth,pTexToX);
		uv.v[1].y = scaleUV(pTextureHeight,pTexFromY);
		uv.v[2].x = scaleUV(pTextureWidth,pTexToX);
		uv.v[2].y = scaleUV(pTextureHeight,pTexToY);
		uv.v[3].x = scaleUV(pTextureWidth,pTexFromX);
		uv.v[3].y = scaleUV(pTextureHeight,pTexToY);

		mUVs.resize(pCount,uv);
	}
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////
// scratch memory buffer utility
///////////////////////////////////////////////////////////////////////////////////////////////////////////
template<typename SCRATCH_MEMORY_TYPE,int START_TYPE_COUNT,int GROWN_TYPE_COUNT,int MAXIMUN_GROWN_COUNT> struct ScratchBuffer
{
	ScratchBuffer():mMemory(new SCRATCH_MEMORY_TYPE[START_TYPE_COUNT]),mCount(START_TYPE_COUNT),mNextIndex(0){}
	~ScratchBuffer(){delete []mMemory;}
	
	/**
	 * @brief Start filling your data from the start of the buffer, overwriting what maybe there. This is the core speed up we get.
	 */
	void Restart(){mNextIndex = 0;}

	/**
	 * @brief For when you know in advance how much space you need.
	 */
	SCRATCH_MEMORY_TYPE* Restart(size_t pCount)
	{
		mNextIndex = 0;
		return Next(pCount);
	}

	/**
	 * @brief Return enough memory to put the next N items into, you can only safety write the number of items requested.
	 */
	SCRATCH_MEMORY_TYPE* Next(size_t pCount = 1)
	{
		EnsureSpace(pCount);
		SCRATCH_MEMORY_TYPE* next = mMemory + mNextIndex;
		mNextIndex += pCount;
		return next;
	}

	/**
	 * @brief How many items have been written since Restart was called.
	 */
	const size_t Used()const{return mNextIndex;}

	/**
	 * @brief Diagnostics tool, how many bytes we're using.
	 */
	const size_t MemoryUsed()const{return mCount * sizeof(SCRATCH_MEMORY_TYPE);}

	/**
	 * @brief The root of our memory, handy for when you've finished filling the buffer and need to now do work with it.
	 * You should fetch this memory pointer AFTER you have done your work as it may change as you fill the data.
	 */
	const SCRATCH_MEMORY_TYPE* Data()const{return mMemory;}

private:
	SCRATCH_MEMORY_TYPE* mMemory; //<! Our memory, only reallocated when it's too small. That is the speed win!
	size_t mCount; //<! How many there are available to write too.
	size_t mNextIndex; //<! Where we can write to next.

	/**
	 * @brief Makes sure we always have space.
	 */
	void EnsureSpace(size_t pExtraSpaceNeeded)
	{
		if( pExtraSpaceNeeded > MAXIMUN_GROWN_COUNT )
		{
			throw std::runtime_error("Scratch memory type tried to grow too large in one go, you may have a memory bug. Tried to add " + std::to_string(pExtraSpaceNeeded) + " items");
		}

		if( (mNextIndex + pExtraSpaceNeeded) >= mCount )
		{
			const size_t newCount = mCount + pExtraSpaceNeeded + GROWN_TYPE_COUNT;
			SCRATCH_MEMORY_TYPE* newMemory = new SCRATCH_MEMORY_TYPE[newCount];
			std::memmove(newMemory,mMemory,mCount);
			delete []mMemory;
			mMemory = newMemory;
			mCount = newCount;
		}
	}
};

/**
 * @brief Simple utility for building quads on the fly.
 */
struct Vert2DShortScratchBuffer : public ScratchBuffer<VertShortXY,256,64,1024>
{
	/**
	 * @brief Writes six vertices to the buffer.
	 */
	inline void BuildQuad(int pX,int pY,int pWidth,int pHeight)
	{
		VertShortXY* verts = Next(6);
		verts[0].x = pX;			verts[0].y = pY;
		verts[1].x = pX + pWidth;	verts[1].y = pY;
		verts[2].x = pX + pWidth;	verts[2].y = pY + pHeight;

		verts[3].x = pX;			verts[3].y = pY;
		verts[4].x = pX + pWidth;	verts[4].y = pY + pHeight;
		verts[5].x = pX;			verts[5].y = pY + pHeight;
	}

	/**
	 * @brief Writes the UV's to six vertices in the correct order to match the quad built above.
	 */
	inline void AddUVRect(int U0,int V0,int U1,int V1)
	{
		VertShortXY* verts = Next(6);
		verts[0].x = U0;	verts[0].y = V0;
		verts[1].x = U1;	verts[1].y = V0;
		verts[2].x = U1;	verts[2].y = V1;

		verts[3].x = U0;	verts[3].y = V0;
		verts[4].x = U1;	verts[4].y = V1;
		verts[5].x = U0;	verts[5].y = V1;
	}

	/**
	 * @brief Adds a number of quads to the buffer, moving STEP for each one.
	 */
	inline void BuildQuads(int pX,int pY,int pWidth,int pHeight,int pCount,int pXStep,int pYStep)
	{
		for(int n = 0 ; n < pCount ; n++, pX += pXStep, pY += pYStep )
		{
			BuildQuad(pX,pY,pWidth,pHeight);
		}
	}
};

struct WorkBuffers
{
	ScratchBuffer<uint8_t,128,16,512*512*4> scratchRam;// gets used for some temporary texture operations.
	ScratchBuffer<Vert2Df,128,16,128> vertices2Df;
	Vert2DShortScratchBuffer vertices2DShort;
	Vert2DShortScratchBuffer uvShort;
};

// End of scratch memory buffer utility
///////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Mainly for debugging, returns a string representation of the enum.
 */
constexpr std::string_view TextureFormatToString(TextureFormat pFormat)
{
	switch( pFormat )
	{
	case TextureFormat::FORMAT_RGBA:
		return "FORMAT_RGBA";
		
	case TextureFormat::FORMAT_RGB:
		return "FORMAT_RGB";

	case TextureFormat::FORMAT_ALPHA:
		return "FORMAT_ALPHA";
	}
	return "Invalid TextureFormat";
}
 
constexpr GLint TextureFormatToGLFormat(TextureFormat pFormat)
{
	switch( pFormat )
	{
	case TextureFormat::FORMAT_RGB:
		return GL_RGB;

	case TextureFormat::FORMAT_RGBA:
		return GL_RGBA;

	case TextureFormat::FORMAT_ALPHA:
		return GL_ALPHA; // This is mainly used for the fonts.
	}
	return GL_INVALID_ENUM;
}

#ifdef USE_FREETYPEFONTS
/**
 * @brief Optional freetype font library support. Is optional as the code is dependant on a library tha may not be avalibel for the host platform.
 * One note, I don't do localisation. ASCII here. If you need all the characters then maybe add yourself or use a commercial grade GL engine. :) localisation is a BIG job!
 * Rendering is done in the GL code, this class is more of just a container.
 */
struct FreeTypeFont
{
	struct Glyph
	{
		int width;
		int height;
		int pitch;
		int advance;
		int x_off,y_off;	//!< offset from current x and y that the quad is rendered.
		struct
		{// Where, in 16bit UV's, the glyph is.
			int x,y;
		}uv[2];
	};

	FreeTypeFont(FT_Face pFontFace,int pPixelHeight);
	~FreeTypeFont();

	/**
	 * @brief Get the Glyph object of an ASCII character. All that is needed to render as well as build the texture.
	 */
	bool GetGlyph(char pChar,FreeTypeFont::Glyph& rGlyph,std::vector<uint8_t>& rPixels);

	/**
	 * @brief Builds our texture object.
	 */
	void BuildTexture(
			int pMaximumAllowedGlyph,
			std::function<uint32_t(int pWidth,int pHeight)> pCreateTexture,
			std::function<void(uint32_t pTexture,int pX,int pY,int pWidth,int pHeight,const uint8_t* pPixels)> pFillTexture);

	const std::string mFontName; //<! Helps with debugging.
	FT_Face mFace;								//<! The font we are rending from.
	uint32_t mTexture;							//<! This is the texture that the glyphs are in so we can render using GL and quads. It's crud but works. ;)
	std::array<FreeTypeFont::Glyph,96>mGlyphs;	//<! Meta data needed to render the characters.

	struct
	{
		uint8_t R = 255;
		uint8_t G = 255;
		uint8_t B = 255;
		uint8_t A = 255;
	}mColour;	

};
#endif // #ifdef USE_FREETYPEFONTS

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// Some basic matrix operations
static const double PI = 3.141592654;
static const double DEGTORAD = (PI / 180.0);

void Matrix::SetIdentity()
{
	m[0][0] = 1.0f;
	m[0][1] = 0.0f;
	m[0][2] = 0.0f;
	m[0][3] = 0.0f;

	m[1][0] = 0.0f;
	m[1][1] = 1.0f;
	m[1][2] = 0.0f;
	m[1][3] = 0.0f;

	m[2][0] = 0.0f;
	m[2][1] = 0.0f;
	m[2][2] = 1.0f;
	m[2][3] = 0.0f;

	m[3][0] = 0.0f;
	m[3][1] = 0.0f;
	m[3][2] = 0.0f;
	m[3][3] = 1.0f;
}

void Matrix::SetTranslation(float pX,float pY,float pZ)
{
	m[0][0] = 1.0f;
	m[0][1] = 0.0f;
	m[0][2] = 0.0f;
	m[0][3] = 0.0f;

	m[1][0] = 0.0f;
	m[1][1] = 1.0f;
	m[1][2] = 0.0f;
	m[1][3] = 0.0f;

	m[2][0] = 0.0f;
	m[2][1] = 0.0f;
	m[2][2] = 1.0f;
	m[2][3] = 0.0f;

	m[3][0] = pX;
	m[3][1] = pY;
	m[3][2] = pZ;
	m[3][3] = 1.0f;
}

void Matrix::SetRotationX(float pPitch)
{
	float sinX = std::sin(pPitch * DEGTORAD);
	float cosX = std::cos(pPitch * DEGTORAD);

	m[0][0] = 1.0f;
	m[0][1] = 0.0f;
	m[0][2] = 0.0f;
	m[0][3] = 0.0f;

	m[1][0] = 0.0f;
	m[1][1] = cosX;
	m[1][2] = sinX;
	m[1][3] = 0.0f;

	m[2][0] = 0.0f;
	m[2][1] = -sinX;
	m[2][2] = cosX;
	m[2][3] = 0.0f;

	m[3][0] = 0.0f;
	m[3][1] = 0.0f;
	m[3][2] = 0.0f;
	m[3][3] = 1.0f;
}

void Matrix::SetRotationY(float pYaw)
{
	float sinY = std::sin(pYaw * DEGTORAD);
	float cosY = std::cos(pYaw * DEGTORAD);

	m[0][0] = cosY;
	m[0][1] = 0.0f;
	m[0][2] = -sinY;
	m[0][3] = 0.0f;

	m[1][0] = 0.0f;
	m[1][1] = 1.0f;
	m[1][2] = 0.0f;
	m[1][3] = 0.0f;

	m[2][0] = sinY;
	m[2][1] = 0.0f;
	m[2][2] = cosY;
	m[2][3] = 0.0f;

	m[3][0] = 0.0f;
	m[3][1] = 0.0f;
	m[3][2] = 0.0f;
	m[3][3] = 1.0f;
}

void Matrix::SetRotationZ(float pRoll)
{
	float sinZ = std::sin(pRoll * DEGTORAD);
	float cosZ = std::cos(pRoll * DEGTORAD);

	m[0][0] = cosZ;
	m[0][1] = sinZ;
	m[0][2] = 0.0f;
	m[0][3] = 0.0f;

	m[1][0] = -sinZ;
	m[1][1] = cosZ;
	m[1][2] = 0.0f;
	m[1][3] = 0.0f;

	m[2][0] = 0.0f;
	m[2][1] = 0.0f;
	m[2][2] = 1.0f;
	m[2][3] = 0.0f;

	m[3][0] = 0.0f;
	m[3][1] = 0.0f;
	m[3][2] = 0.0f;
	m[3][3] = 1.0f;
}

void Matrix::Mul(const Matrix &pA,const Matrix &pB)
{
	m[0][0] = (pA.m[0][0]*pB.m[0][0]) + (pA.m[0][1]*pB.m[1][0]) + (pA.m[0][2]*pB.m[2][0]) + (pA.m[0][3]*pB.m[3][0]);
	m[0][1] = (pA.m[0][0]*pB.m[0][1]) + (pA.m[0][1]*pB.m[1][1]) + (pA.m[0][2]*pB.m[2][1]) + (pA.m[0][3]*pB.m[3][1]);
	m[0][2] = (pA.m[0][0]*pB.m[0][2]) + (pA.m[0][1]*pB.m[1][2]) + (pA.m[0][2]*pB.m[2][2]) + (pA.m[0][3]*pB.m[3][2]);
	m[0][3] = (pA.m[0][0]*pB.m[0][3]) + (pA.m[0][1]*pB.m[1][3]) + (pA.m[0][2]*pB.m[2][3]) + (pA.m[0][3]*pB.m[3][3]);

	m[1][0] = (pA.m[1][0]*pB.m[0][0]) + (pA.m[1][1]*pB.m[1][0]) + (pA.m[1][2]*pB.m[2][0]) + (pA.m[1][3]*pB.m[3][0]);
	m[1][1] = (pA.m[1][0]*pB.m[0][1]) + (pA.m[1][1]*pB.m[1][1]) + (pA.m[1][2]*pB.m[2][1]) + (pA.m[1][3]*pB.m[3][1]);
	m[1][2] = (pA.m[1][0]*pB.m[0][2]) + (pA.m[1][1]*pB.m[1][2]) + (pA.m[1][2]*pB.m[2][2]) + (pA.m[1][3]*pB.m[3][2]);
	m[1][3] = (pA.m[1][0]*pB.m[0][3]) + (pA.m[1][1]*pB.m[1][3]) + (pA.m[1][2]*pB.m[2][3]) + (pA.m[1][3]*pB.m[3][3]);

	m[2][0] = (pA.m[2][0]*pB.m[0][0]) + (pA.m[2][1]*pB.m[1][0]) + (pA.m[2][2]*pB.m[2][0]) + (pA.m[2][3]*pB.m[3][0]);
	m[2][1] = (pA.m[2][0]*pB.m[0][1]) + (pA.m[2][1]*pB.m[1][1]) + (pA.m[2][2]*pB.m[2][1]) + (pA.m[2][3]*pB.m[3][1]);
	m[2][2] = (pA.m[2][0]*pB.m[0][2]) + (pA.m[2][1]*pB.m[1][2]) + (pA.m[2][2]*pB.m[2][2]) + (pA.m[2][3]*pB.m[3][2]);
	m[2][3] = (pA.m[2][0]*pB.m[0][3]) + (pA.m[2][1]*pB.m[1][3]) + (pA.m[2][2]*pB.m[2][3]) + (pA.m[2][3]*pB.m[3][3]);

	m[3][0] = (pA.m[3][0]*pB.m[0][0]) + (pA.m[3][1]*pB.m[1][0]) + (pA.m[3][2]*pB.m[2][0]) + (pA.m[3][3]*pB.m[3][0]);
	m[3][1] = (pA.m[3][0]*pB.m[0][1]) + (pA.m[3][1]*pB.m[1][1]) + (pA.m[3][2]*pB.m[2][1]) + (pA.m[3][3]*pB.m[3][1]);
	m[3][2] = (pA.m[3][0]*pB.m[0][2]) + (pA.m[3][1]*pB.m[1][2]) + (pA.m[3][2]*pB.m[2][2]) + (pA.m[3][3]*pB.m[3][2]);
	m[3][3] = (pA.m[3][0]*pB.m[0][3]) + (pA.m[3][1]*pB.m[1][3]) + (pA.m[3][2]*pB.m[2][3]) + (pA.m[3][3]*pB.m[3][3]);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// GLES Shader definition
///////////////////////////////////////////////////////////////////////////////////////////////////////////
struct GLShader
{
	GLShader(const std::string& pName,const char* pVertex, const char* pFragment);
	~GLShader();

	int GetUniformLocation(const char* pName);
	void BindAttribLocation(int location,const char* pName);
	void Enable(const float projInvcam[4][4]);
	void SetTransform(float transform[4][4]);
	void SetGlobalColour(uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha);
	void SetGlobalColour(float red,float green,float blue,float alpha);
	void SetTexture(GLint texture);

	bool GetUsesTexture()const{return mUniforms.tex0 > -1;}
	bool GetUsesTransform()const{return mUniforms.trans > -1;}

	const std::string mName;	//!< Mainly to help debugging.
	const bool mEnableStreamUV;
	const bool mEnableStreamTrans;
	const bool mEnableStreamColour;

	GLint mShader = 0;
	GLint mVertexShader = 0;
	GLint mFragmentShader = 0;
	struct
	{
		GLint trans;
		GLint proj_cam;
		GLint global_colour;
		GLint tex0;
	}mUniforms;

	int LoadShader(int type, const char* shaderCode);
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// EGL hidden definition.
#ifdef PLATFORM_EGL
struct PlatformInterface
{
	PlatformInterface();
	~PlatformInterface();

	void SwapBuffers();
	void InitialiseDisplay();

	void FindEGLConfiguration();

	int GetWidth()const{return mScreenInfo.xres;}
	int GetHeight()const{return mScreenInfo.yres;}

	struct fb_var_screeninfo mScreenInfo;
	EGLDisplay mDisplay = nullptr;				//!<GL display
	EGLSurface mSurface = nullptr;				//!<GL rendering surface
	EGLContext mContext = nullptr;				//!<GL rendering context
	EGLConfig mConfig = nullptr;				//!<Configuration of the display.

#ifdef BROADCOM_NATIVE_WINDOW
	EGL_DISPMANX_WINDOW_T mNativeWindow;		//!<The RPi window object needed to create the render surface.
#else
	EGLNativeWindowType mNativeWindow = nullptr;
#endif
};
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// DRM Direct Render Manager hidden definition.
// Used for more model systems like RPi4 and proper GL / GLES implementations.
#ifdef PLATFORM_DIRECT_RENDER_MANAGER
struct PlatformInterface
{
	bool mIsFirstFrame = true;
	int mDRMFile = -1;

	// This is used for the EGL bring up and getting GLES going along with DRM.
	struct gbm_device *mBufferManager = nullptr;
	struct gbm_bo *mCurrentFrontBufferObject = nullptr;

	drmModeEncoder *mModeEncoder = nullptr;
	drmModeConnector* mConnector = nullptr;
	drmModeModeInfo* mModeInfo = nullptr;
	uint32_t mFOURCC_Format = DRM_FORMAT_INVALID;
	uint32_t mCurrentFrontBufferID = 0;

	EGLDisplay mDisplay = nullptr;				//!<GL display
	EGLSurface mSurface = nullptr;				//!<GL rendering surface
	EGLContext mContext = nullptr;				//!<GL rendering context
	EGLConfig mConfig = nullptr;				//!<Configuration of the display.

	struct gbm_surface *mNativeWindow = nullptr;

	PlatformInterface();
	~PlatformInterface();

	int GetWidth()const{assert(mModeInfo);if(mModeInfo){return mModeInfo->hdisplay;}return 0;}
	int GetHeight()const{assert(mModeInfo);if(mModeInfo){return mModeInfo->vdisplay;}return 0;}


	void InitialiseDisplay();
	void FindEGLConfiguration();

	void UpdateCurrentBuffer();
	void SwapBuffers();
};
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// X11 GL emulation hidden definition.
// Implementation is at the bottom of the source file.
// This code is intended to allow development on a full desktop system for applications that
// will eventually be deployed on a minimal linux system without all the X11 + fancy UI rendering bloat.
///////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef PLATFORM_X11_GL
/**
 * @brief Emulation layer for X11
 * 
 */
struct PlatformInterface
{
	Display *mXDisplay = nullptr;
	Window mWindow = 0;
	Atom mDeleteMessage;
	GLXContext mGLXContext = 0;
	XSetWindowAttributes mWindowAttributes;
	XVisualInfo* mVisualInfo;

	bool mWindowReady;

	PlatformInterface();
	~PlatformInterface();

	/**
	 * @brief Creates the X11 window and all the bits needed to get rendering with.
	 */
	void InitialiseDisplay();

	/**
	 * @brief Processes the X11 events then exits when all are done. Returns true if the app is asked to quit.
	 */
	bool ProcessX11Events(tinygles::GLES::SystemEventHandler pEventHandler);

	/**
	 * @brief Draws the frame buffer to the X11 window.
	 */
	void SwapBuffers();

	int GetWidth()const{return X11_EMULATION_WIDTH;}
	int GetHeight()const{return X11_EMULATION_HEIGHT;}

};
#endif //#ifdef USE_X11_EMULATION

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// GLES Implementation
///////////////////////////////////////////////////////////////////////////////////////////////////////////
GLES::GLES(uint32_t pFlags) :
	mCreateFlags(pFlags),
	mPlatform(std::make_unique<PlatformInterface>()),
	mWorkBuffers(std::make_unique<WorkBuffers>())
{
	// Lets hook ctrl + c.
	mUsersSignalAction = signal(SIGINT,CtrlHandler);

	const char* MouseDeviceName = "/dev/input/event0";
	mPointer.mDevice = open(MouseDeviceName,O_RDONLY|O_NONBLOCK); // May fail, this is ok. They may not have one.
#ifdef VERBOSE_BUILD
	if(  mPointer.mDevice >  0 )
	{
		VERBOSE_MESSAGE("Opened mouse device " << MouseDeviceName);
		char name[256] = "Unknown";
		if( ioctl(mPointer.mDevice, EVIOCGNAME(sizeof(name)), name) == 0 )
		{
			VERBOSE_MESSAGE("Reading mouse from: handle = " << mPointer.mDevice << " name = " << name);
		}
	}
	else
	{// Not an error, may not have one connected. Depends on the usecase.
		VERBOSE_MESSAGE("Failed to open mouse device " << MouseDeviceName);
	}
#endif

	mPhysical.Width = mPlatform->GetWidth();
	mPhysical.Height = mPlatform->GetHeight();

	if( mCreateFlags&ROTATE_FRAME_PORTRATE )
	{
		mCreateFlags &= ~ROTATE_FRAME_PORTRATE;
		if( mPhysical.Width > mPhysical.Height )
		{
			mCreateFlags |= ROTATE_FRAME_BUFFER_90;
		}
	}

	if( mCreateFlags&ROTATE_FRAME_LANDSCAPE )
	{
		mCreateFlags &= ~ROTATE_FRAME_LANDSCAPE;
		if( mPhysical.Width < mPhysical.Height )
		{
			mCreateFlags |= ROTATE_FRAME_BUFFER_90;
		}
	}

	if( mCreateFlags&(ROTATE_FRAME_BUFFER_90|ROTATE_FRAME_BUFFER_270) )
	{
		mReported.Width = mPhysical.Height;
		mReported.Height = mPhysical.Width;
	}
	else
	{
		mReported.Width = mPhysical.Width;
		mReported.Height = mPhysical.Height;
	}

	VERBOSE_MESSAGE("Physical display resolution is " << mPhysical.Width << "x" << mPhysical.Height );

	mPlatform->InitialiseDisplay();

	SetRenderingDefaults();
	BuildShaders();
	BuildDebugTexture();
	BuildPixelFontTexture();
	InitFreeTypeFont();
	AllocateQuadBuffers();

	VERBOSE_MESSAGE("GLES Ready");
}

GLES::~GLES()
{
	VERBOSE_MESSAGE("GLES destructor called");

	VERBOSE_MESSAGE("On exit the following scratch memory buffers reached the sizes of...");
	VERBOSE_MESSAGE("    mWorkBuffers.vertices2Df " << mWorkBuffers->vertices2Df.MemoryUsed() << " bytes");
	VERBOSE_MESSAGE("    mWorkBuffers.vertices2DShort " << mWorkBuffers->vertices2DShort.MemoryUsed() << " bytes");
	VERBOSE_MESSAGE("    mWorkBuffers.uvShort " << mWorkBuffers->uvShort.MemoryUsed() << " bytes");

	glBindTexture(GL_TEXTURE_2D,0);
	CHECK_OGL_ERRORS();

	// Kill shaders.
	VERBOSE_MESSAGE("Deleting shaders");

	glUseProgram(0);
	CHECK_OGL_ERRORS();

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
	glDeleteBuffers(1,&mQuadBatch.IndicesBuffer);

	mShaders.CurrentShader.reset();
	mShaders.ColourOnly2D.reset();
	mShaders.TextureColour2D.reset();
	mShaders.TextureAlphaOnly2D.reset();
	mShaders.SpriteShader2D.reset();
	mShaders.QuadBatchShader2D.reset();

	mShaders.ColourOnly3D.reset();
	mShaders.TextureOnly3D.reset();


	// delete all free type fonts.
#ifdef USE_FREETYPEFONTS
	mFreeTypeFonts.clear();
	if( mFreetype != nullptr )
	{
		if( FT_Done_FreeType(mFreetype) == FT_Err_Ok )
		{
			mFreetype = nullptr;
			VERBOSE_MESSAGE("Freetype font library deleted");
		}
	}
#endif

	// delete all textures.
	for( auto& t : mTextures )
	{
		glDeleteTextures(1,&t.first);
		CHECK_OGL_ERRORS();
	}

	VERBOSE_MESSAGE("All done");
}

bool GLES::BeginFrame()
{
	mDiagnostics.frameNumber++;

	// Reset some items so that we have a working render setup to begin the frame with.
	// This is done so that I don't have to have a load of if statements to deal with first frame. Also makes life simpler for the more minimal applications.
	EnableShader(mShaders.ColourOnly2D);
	SetTransformIdentity();

	return GLES::mKeepGoing;
}

void GLES::EndFrame()
{
	glFlush();// This makes sure the display is fully up to date before we allow them to interact with any kind of UI. This is the specified use of this function.
	mPlatform->SwapBuffers();
	ProcessSystemEvents();
}

void GLES::Clear(uint8_t pRed,uint8_t pGreen,uint8_t pBlue)
{
	glClearColor((float)pRed / 255.0f,(float)pGreen / 255.0f,(float)pBlue / 255.0f,1.0f);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	CHECK_OGL_ERRORS();
}

void GLES::Clear(uint32_t pTexture)
{
	glClear(GL_DEPTH_BUFFER_BIT);
	CHECK_OGL_ERRORS();
	FillRectangle(0,0,GetWidth(),GetHeight(),pTexture);
}

void GLES::Begin2D()
{
	// Setup 2D frustum
	memset(mMatrices.projection,0,sizeof(mMatrices.projection));
	mMatrices.projection[3][3] = 1;

	if( mCreateFlags&ROTATE_FRAME_BUFFER_90 )
	{
		mMatrices.projection[0][1] = -2.0f / (float)mPhysical.Height;
		mMatrices.projection[1][0] = -2.0f / (float)mPhysical.Width;
				
		mMatrices.projection[3][0] = 1;
		mMatrices.projection[3][1] = 1;
	}
	else if( mCreateFlags&ROTATE_FRAME_BUFFER_180 )
	{
		mMatrices.projection[0][0] = -2.0f / (float)mPhysical.Width;
		mMatrices.projection[1][1] = 2.0f / (float)mPhysical.Height;
				
		mMatrices.projection[3][0] = 1;
		mMatrices.projection[3][1] = -1;
	}
	else if( mCreateFlags&ROTATE_FRAME_BUFFER_270 )
	{
		mMatrices.projection[0][1] = 2.0f / (float)mPhysical.Height;
		mMatrices.projection[1][0] = 2.0f / (float)mPhysical.Width;
				
		mMatrices.projection[3][0] = -1;
		mMatrices.projection[3][1] = -1;
	}
	else
	{
		mMatrices.projection[0][0] = 2.0f / (float)mPhysical.Width;
		mMatrices.projection[1][1] = -2.0f / (float)mPhysical.Height;
		mMatrices.projection[3][0] = -1;
		mMatrices.projection[3][1] = 1;
	}

	// No Depth buffer in 2D
	glDisable(GL_DEPTH_TEST);
	glDepthFunc(GL_ALWAYS);
	glDepthMask(false);

}

void GLES::Begin3D(float pFov, float pNear, float pFar)
{
	const float cotangent = 1.0f / tanf(DegreeToRadian(pFov));
	const float q = pFar / (pFar - pNear);
	const float aspect = GetDisplayAspectRatio();

	memset(mMatrices.projection,0,sizeof(mMatrices.projection));

	mMatrices.projection[0][0] = cotangent;

	mMatrices.projection[1][1] = aspect * cotangent;

	mMatrices.projection[2][2] = q;
	mMatrices.projection[2][3] = 1.0f;

	mMatrices.projection[3][2] = -q * pNear;

	if( mCreateFlags&ROTATE_FRAME_BUFFER_90 )
	{
		mMatrices.projection[0][1] = -mMatrices.projection[0][0];
		mMatrices.projection[0][0] = 0.0f;

		mMatrices.projection[1][0] = mMatrices.projection[1][1];
		mMatrices.projection[1][1] = 0.0f;
	}
	else if( mCreateFlags&ROTATE_FRAME_BUFFER_180 )
	{
		mMatrices.projection[0][0] = -mMatrices.projection[0][0];
		mMatrices.projection[1][1] = -mMatrices.projection[1][1];
	}
	else if( mCreateFlags&ROTATE_FRAME_BUFFER_270 )
	{
		mMatrices.projection[0][1] = mMatrices.projection[0][0];
		mMatrices.projection[0][0] = 0.0f;

		mMatrices.projection[1][0] = -mMatrices.projection[1][1];
		mMatrices.projection[1][1] = 0.0f;
	}

	// Depth buffer please
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDepthMask(GL_TRUE);

}

void GLES::SetTransform(float pTransform[4][4])
{
	assert(mShaders.CurrentShader);
	memcpy(mMatrices.transform,pTransform,sizeof(float) * 4 * 4);
	mShaders.CurrentShader->SetTransform(pTransform);
}

void GLES::SetTransform(float x,float y,float z)
{
	float transOnly[4][4] =
	{
		{1,0,0,0},
		{0,1,0,0},
		{0,0,1,0},
		{x,y,z,1}
	};
	SetTransform(transOnly);
}

void GLES::SetTransformIdentity()
{
	static float identity[4][4] = {{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}};
	SetTransform(identity);
}

void GLES::SetTransform2D(float pX,float pY,float pRotation,float pScale)
{
	float trans[4][4] =
	{
		{cos(pRotation)*pScale,sin(pRotation)*pScale,0,0},
		{-sin(pRotation)*pScale,cos(pRotation)*pScale,0,0},
		{0,0,pScale,0},
		{pX,pY,0,1}
	};
	SetTransform(trans);
}

void GLES::OnApplicationExitRequest()
{
	VERBOSE_MESSAGE("Exit request from user, quitting application");
	mKeepGoing = false;
	if( mSystemEventHandler != nullptr )
	{
		SystemEventData data(SystemEventType::EXIT_REQUEST);
		mSystemEventHandler(data);
	}
}

//*******************************************
// Primitive draw commands.
void GLES::DrawLine(int pFromX,int pFromY,int pToX,int pToY,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha)
{
	const int16_t quad[4] = {(int16_t)pFromX,(int16_t)pFromY,(int16_t)pToX,(int16_t)pToY};

	EnableShader(mShaders.ColourOnly2D);
	mShaders.CurrentShader->SetGlobalColour(pRed,pGreen,pBlue,pAlpha);

	VertexPtr(2,GL_SHORT,quad);
	glDrawArrays(GL_LINES,0,2);
	CHECK_OGL_ERRORS();
}

void GLES::DrawLine(int pFromX,int pFromY,int pToX,int pToY,int pWidth,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha)
{
	if( pWidth < 2 )
	{
		DrawLine(pFromX,pFromY,pToX,pToY,pRed,pGreen,pBlue);
	}
	else
	{
		pWidth /= 2;
		VertShortXY p[6];

		if( pFromY < pToY )
		{
			std::swap(pFromY,pToY);
			std::swap(pFromX,pToX);
		}

		if( pFromX < pToX )
		{
			p[0].x = pToX - pWidth;
			p[0].y = pToY - pWidth;

			p[1].x = pToX + pWidth;
			p[1].y = pToY - pWidth;

			p[2].x = pToX + pWidth;
			p[2].y = pToY + pWidth;

			p[3].x = pFromX + pWidth;
			p[3].y = pFromY + pWidth;

			p[4].x = pFromX - pWidth;
			p[4].y = pFromY + pWidth;

			p[5].x = pFromX - pWidth;
			p[5].y = pFromY - pWidth;
		}
		else
		{
			p[0].x = pFromX + pWidth;
			p[0].y = pFromY - pWidth;

			p[1].x = pFromX + pWidth;
			p[1].y = pFromY + pWidth;

			p[2].x = pFromX - pWidth;
			p[2].y = pFromY + pWidth;

			p[3].x = pToX - pWidth;
			p[3].y = pToY + pWidth;

			p[4].x = pToX - pWidth;
			p[4].y = pToY - pWidth;

			p[5].x = pToX + pWidth;
			p[5].y = pToY - pWidth;			
		}

		EnableShader(mShaders.ColourOnly2D);
		mShaders.CurrentShader->SetGlobalColour(pRed,pGreen,pBlue,pAlpha);

		VertexPtr(2,GL_SHORT,p);
		glDrawArrays(GL_TRIANGLE_FAN,0,6);
		CHECK_OGL_ERRORS();
	}
}

void GLES::DrawLineList(const VerticesShortXY& pPoints,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha)
{
	EnableShader(mShaders.ColourOnly2D);
	mShaders.CurrentShader->SetGlobalColour(pRed,pGreen,pBlue,pAlpha);

	VertexPtr(2,GL_SHORT,pPoints.data());
	glDrawArrays(GL_LINE_STRIP,0,pPoints.size());
	CHECK_OGL_ERRORS();
}

void GLES::DrawLineList(const VerticesShortXY& pPoints,int pWidth,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha)
{
	if( pWidth < 2 )
	{
		DrawLineList(pPoints,pRed,pGreen,pBlue,pAlpha);
	}
	else
	{
		for( size_t n = 0 ; n < pPoints.size() - 1 ; n++ )
		{
			DrawLine(pPoints[n].x,pPoints[n].y,pPoints[n+1].x,pPoints[n+1].y,pWidth,pRed,pGreen,pBlue,pAlpha);
		}
	}
}	

void GLES::Circle(int pCenterX,int pCenterY,int pRadius,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha,size_t pNumPoints,bool pFilled)
{
	if( pNumPoints < 1 )
	{
        pNumPoints = (int)(3 + (std::sqrt(pRadius)*3));
	}
	if( pNumPoints > 128 ){pNumPoints = 128;}	// Make sure we don't go silly with number of verts and loose all the FPS.

	Vert2Df* verts = mWorkBuffers->vertices2Df.Restart(pNumPoints);

	float rad = 0.0;
	const float step = GetRadian() / (float)(pNumPoints-2);// +2 is because of first triangle.
	const float r = (float)pRadius;
	const float x = (float)pCenterX;
	const float y = (float)pCenterY;
	for( size_t n = 0 ; n < pNumPoints ; n++, rad += step )
	{
		verts[n].x = x - (r*std::sin(rad));
		verts[n].y = y + (r*std::cos(rad));
	}

	EnableShader(mShaders.ColourOnly2D);
	mShaders.CurrentShader->SetGlobalColour(pRed,pGreen,pBlue,pAlpha);

	VertexPtr(2,GL_FLOAT,verts);
	glDrawArrays(pFilled?GL_TRIANGLE_FAN:GL_LINE_LOOP,0,pNumPoints);
	CHECK_OGL_ERRORS();
}

void GLES::Rectangle(int pFromX,int pFromY,int pToX,int pToY,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha,bool pFilled,uint32_t pTexture)
{
	const int16_t quad[8] = {(int16_t)pFromX,(int16_t)pFromY,(int16_t)pToX,(int16_t)pFromY,(int16_t)pToX,(int16_t)pToY,(int16_t)pFromX,(int16_t)pToY};
	const int16_t uv[8] = {0,0,1,0,1,1,0,1};

	SelectAndEnableShader(pTexture,pRed,pGreen,pBlue,pAlpha);

	if( mShaders.CurrentShader->GetUsesTexture() )
	{
		glVertexAttribPointer(
					(GLuint)StreamIndex::TEXCOORD,
					2,
					GL_SHORT,
					GL_FALSE,
					0,uv);
		CHECK_OGL_ERRORS();
	}

	VertexPtr(2,GL_SHORT,quad);
	glDrawArrays(pFilled?GL_TRIANGLE_FAN:GL_LINE_LOOP,0,4);
	CHECK_OGL_ERRORS();
}

void GLES::RoundedRectangle(int pFromX,int pFromY,int pToX,int pToY,int pRadius,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha,bool pFilled)
{
    size_t numPoints = (int)(7 + (std::sqrt(pRadius)*3));

	// Need a multiple of 4 points.
	numPoints = (numPoints+3)&~3;
	if( numPoints > 128 ){numPoints = 128;}	// Make sure we don't go silly with number of verts and loose all the FPS.
	Vert2Df* verts = mWorkBuffers->vertices2Df.Restart(numPoints);

	float rad = GetRadian();
	const float step = GetRadian() / (float)(numPoints-1);
	const float r = (float)pRadius;
	Vert2Df* p = verts;

	pToX -= pRadius;
	pToY -= pRadius;
	pFromX += pRadius;
	pFromY += pRadius;

	for( size_t n = 0 ; n < numPoints/4 ; n++ )
	{
		p->x = pFromX + (r*std::sin(rad));
		p->y = pToY + (r*std::cos(rad));
		p++;
		rad -= step;
	}

	for( size_t n = 0 ; n < numPoints/4 ; n++ )
	{
		p->x = pFromX + (r*std::sin(rad));
		p->y = pFromY + (r*std::cos(rad));
		p++;
		rad -= step;
	}

	for( size_t n = 0 ; n < numPoints/4 ; n++ )
	{
		p->x = pToX + (r*std::sin(rad));
		p->y = pFromY + (r*std::cos(rad));
		p++;
		rad -= step;
	}

	for( size_t n = 0 ; n < numPoints/4 ; n++ )
	{
		p->x = pToX + (r*std::sin(rad));
		p->y = pToY + (r*std::cos(rad));
		p++;
		rad -= step;
	}

	EnableShader(mShaders.ColourOnly2D);
	mShaders.CurrentShader->SetGlobalColour(pRed,pGreen,pBlue,pAlpha);

	VertexPtr(2,GL_FLOAT,verts);
	glDrawArrays(pFilled?GL_TRIANGLE_FAN:GL_LINE_LOOP,0,numPoints);
	CHECK_OGL_ERRORS();
}

void GLES::Blit(uint32_t pTexture,int pX,int pY,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha)
{
	const auto& tex = mTextures.find(pTexture);
	if( tex == mTextures.end() )
	{
		FillRectangle(pX,pY,pX+128,pY+128,pRed,pGreen,pBlue,pAlpha,mDiagnostics.texture);
	}
	else
	{

		FillRectangle(pX,pY,pX+tex->second->mWidth-1,pY+tex->second->mHeight-1,pRed,pGreen,pBlue,pAlpha,pTexture);
	}
}

// End of primitive draw commands.
//*******************************************
// Sprite functions
uint32_t GLES::SpriteCreate(uint32_t pTexture,float pWidth,float pHeight,float pCX,float pCY,int pTexFromX,int pTexFromY,int pTexToX,int pTexToY)
{
	// Will throw an exception if texture not found, done early on so we don't waste sprint indices.
	const int texWidth = GetTextureWidth(pTexture);
	const int texHeight = GetTextureHeight(pTexture);

	const uint32_t newSprite = mNextSpriteIndex++;
	if( newSprite == 0 )
	{
		THROW_MEANINGFUL_EXCEPTION("Failed to create sprite, sprite handles have wrapped around. You have some serious bugs and memory leaks!");
	}

	if( mSprites.find(newSprite) != mSprites.end() )
	{
		THROW_MEANINGFUL_EXCEPTION("Bug found in reindering code, sprite index is an index that we already know about.");
	}

	mSprites[newSprite] = std::make_unique<Sprite>();
	Sprite* s = mSprites[newSprite].get();

	s->mTexture = pTexture;
	s->mWidth = pWidth;
	s->mHeight = pHeight;
	s->mCX = pCX;
	s->mCY = pCY;

	s->BuildVerts();
	s->BuildUVs(texWidth,texHeight,pTexFromX,pTexFromY,pTexToX,pTexToY);

	return newSprite;
}

uint32_t GLES::SpriteCreate(uint32_t pTexture,float pWidth,float pHeight,float pCX,float pCY)
{
	const float texWidth = GetTextureWidth(pTexture);
	const float texHeight = GetTextureHeight(pTexture);
	return SpriteCreate(pTexture,pWidth,pHeight,pCX,pCY,0,0,texWidth,texHeight);
}

uint32_t GLES::SpriteCreate(uint32_t pTexture)
{
	const float texWidth = GetTextureWidth(pTexture);
	const float texHeight = GetTextureHeight(pTexture);
	return SpriteCreate(pTexture,texWidth,texHeight,texWidth / 2.0f,texHeight / 2.0f,0,0,texWidth,texHeight);	
}

void GLES::SpriteDelete(uint32_t pSprite)
{
	if( mSprites.find(pSprite) != mSprites.end() )
	{
		mSprites.erase(pSprite);
	}
}

void GLES::SpriteDraw(uint32_t pSprite)
{
	assert(mShaders.SpriteShader2D);

	auto& sprite = mSprites.at(pSprite);

	EnableShader(mShaders.SpriteShader2D);

	assert(mShaders.CurrentShader);
	mShaders.CurrentShader->SetTexture(sprite->mTexture);
	mShaders.CurrentShader->SetGlobalColour(1.0f,1.0f,1.0f,1.0f);

	VertexPtr(2,GL_FLOAT,sprite->mVert.data());

	// Because UV's are normalized.
	glVertexAttribPointer(
				(GLuint)StreamIndex::TEXCOORD,
				2,
				GL_SHORT,
				GL_TRUE,
				4,sprite->mUV.data());


	glDrawArrays(GL_TRIANGLE_FAN,0,4);
	CHECK_OGL_ERRORS();
}

void GLES::SpriteSetCenter(uint32_t pSprite,float pCX,float pCY)
{
	auto& sprite = mSprites.at(pSprite);
	sprite->mCX = pCX;
	sprite->mCY = pCY;
	sprite->BuildVerts();
}

uint32_t GLES::QuadBatchCreate(uint32_t pTexture,int pCount,int pTexFromX,int pTexFromY,int pTexToX,int pTexToY)
{
	// Will throw an exception if texture not found, done early on so we don't waste sprint indices.
	const int texWidth = GetTextureWidth(pTexture);
	const int texHeight = GetTextureHeight(pTexture);

	const uint32_t newBatch = mQuadBatch.NextIndex++;
	if( newBatch == 0 )
	{
		THROW_MEANINGFUL_EXCEPTION("Failed to create sprite, sprite handles have wrapped around. You have some serious bugs and memory leaks!");
	}

	if( mQuadBatch.Batchs.find(newBatch) != mQuadBatch.Batchs.end() )
	{
		THROW_MEANINGFUL_EXCEPTION("Bug found in rendering code, sprite index is an index that we already know about.");
	}

	mQuadBatch.Batchs[newBatch] = std::make_unique<QuadBatch>(pCount,pTexture,texWidth,texHeight,pTexFromX,pTexFromY,pTexToX,pTexToY);
	return newBatch;
}

uint32_t GLES::QuadBatchCreate(uint32_t pTexture,int pCount)
{
	const float texWidth = GetTextureWidth(pTexture);
	const float texHeight = GetTextureHeight(pTexture);
	return QuadBatchCreate(pTexture,pCount,0,0,texWidth,texHeight);
}

void GLES::QuadBatchDelete(uint32_t pQuadBatch)
{
	if( mQuadBatch.Batchs.find(pQuadBatch) != mQuadBatch.Batchs.end() )
	{
		mQuadBatch.Batchs.erase(pQuadBatch);
	}
}

void GLES::QuadBatchDraw(uint32_t pQuadBatch)
{
	assert(mShaders.QuadBatchShader2D);

	auto& QuadBatch = mQuadBatch.Batchs.at(pQuadBatch);

	EnableShader(mShaders.QuadBatchShader2D);

	assert(mShaders.CurrentShader == mShaders.QuadBatchShader2D);
	mShaders.CurrentShader->SetTexture(QuadBatch->mTexture);
	mShaders.CurrentShader->SetGlobalColour(1.0f,1.0f,1.0f,1.0f);

	glBindBuffer(GL_ARRAY_BUFFER,mQuadBatch.VerticesBuffer);
	glVertexAttribPointer(
				(GLuint)StreamIndex::VERTEX,
				2,
				GL_BYTE,
				GL_TRUE,
				0,0);
	glBindBuffer(GL_ARRAY_BUFFER,0);

	// Because UV's are normalized.
	glVertexAttribPointer(
				(GLuint)StreamIndex::TEXCOORD,
				2,
				GL_SHORT,
				GL_TRUE,
				0,QuadBatch->mUVs.data());

	glVertexAttribPointer(
				(GLuint)StreamIndex::TRANSFORM,
				4,
				GL_SHORT,
				GL_FALSE,
				0,
				QuadBatch->mTransforms.data());

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,mQuadBatch.IndicesBuffer);
	glDrawElements(GL_TRIANGLES,QuadBatch->GetNumQuads() * mQuadBatch.IndicesPerQuad,GL_UNSIGNED_SHORT,0);
	CHECK_OGL_ERRORS();
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
}

void GLES::QuadBatchDraw(uint32_t pQuadBatch,size_t pFromIndex,size_t pToIndex)
{
	if( pToIndex <= pFromIndex )
	{
		return;// Allow this as their code may use this case at the start or end of an effect.
	}

	assert(mShaders.QuadBatchShader2D);

	auto& QuadBatch = mQuadBatch.Batchs.at(pQuadBatch);

	assert( pFromIndex < QuadBatch->GetNumQuads() );
	assert( pToIndex < QuadBatch->GetNumQuads() );

	EnableShader(mShaders.QuadBatchShader2D);

	assert(mShaders.CurrentShader == mShaders.QuadBatchShader2D);
	mShaders.CurrentShader->SetTexture(QuadBatch->mTexture);
	mShaders.CurrentShader->SetGlobalColour(1.0f,1.0f,1.0f,1.0f);

	glBindBuffer(GL_ARRAY_BUFFER,mQuadBatch.VerticesBuffer);
	glVertexAttribPointer(
				(GLuint)StreamIndex::VERTEX,
				2,
				GL_BYTE,
				GL_TRUE,
				0,0);
	glBindBuffer(GL_ARRAY_BUFFER,0);


	// Because UV's are normalized.
	glVertexAttribPointer(
				(GLuint)StreamIndex::TEXCOORD,
				2,
				GL_SHORT,
				GL_TRUE,
				0,QuadBatch->mUVs.data());

	glVertexAttribPointer(
				(GLuint)StreamIndex::TRANSFORM,
				4,
				GL_FLOAT,
				GL_FALSE,
				0,
				QuadBatch->mTransforms.data());

/*	const uint16_t* idx = mQuadIndices.data();
	idx += pFromIndex * mQuadBatch.IndicesPerQuad;

	const int count = (pToIndex - pFromIndex) * mQuadBatch.IndicesPerQuad;

	glDrawElements(GL_TRIANGLES,count,GL_UNSIGNED_SHORT,idx);
	CHECK_OGL_ERRORS();*/
}

std::vector<QuadBatchTransform>& GLES::QuadBatchGetTransform(uint32_t pQuadBatch)
{
	auto& QuadBatch = mQuadBatch.Batchs.at(pQuadBatch);
	return QuadBatch->mTransforms;
}

//*******************************************
// Primitive rendering functions for user defined shapes
void GLES::RenderTriangles(const VerticesXYZC& pVertices)
{
	EnableShader(mShaders.ColourOnly3D);

	assert(mShaders.CurrentShader);
	mShaders.CurrentShader->SetGlobalColour(1.0f,1.0f,1.0f,1.0f);

	const uint8_t* verts = (const uint8_t*)pVertices.data();
	const uint8_t* c = verts + (sizeof(float)*3);

	glVertexAttribPointer(
				(GLuint)StreamIndex::VERTEX,
				3,
				GL_FLOAT,
				GL_FALSE,
				sizeof(VertXYZC),
				verts);
	CHECK_OGL_ERRORS();

	glVertexAttribPointer(
				(GLuint)StreamIndex::COLOUR,
				4,
				GL_UNSIGNED_BYTE,
				GL_TRUE,
				sizeof(VertXYZC),
				c);
	CHECK_OGL_ERRORS();

	glDrawArrays(GL_TRIANGLES,0,pVertices.size());
	CHECK_OGL_ERRORS();
}

void GLES::RenderTriangles(const VerticesXYZUV& pVertices,uint32_t pTexture)
{
	if(pTexture == 0)
	{
		pTexture = mDiagnostics.texture;
	}

	EnableShader(mShaders.TextureOnly3D);

	assert(mShaders.CurrentShader);
	mShaders.CurrentShader->SetTexture(pTexture);
	mShaders.CurrentShader->SetGlobalColour(1.0f,1.0f,1.0f,1.0f);

	const uint8_t* verts = (const uint8_t*)pVertices.data();
	const uint8_t* c = verts + (sizeof(float)*3);

	glVertexAttribPointer(
				(GLuint)StreamIndex::VERTEX,
				3,
				GL_FLOAT,
				GL_FALSE,
				sizeof(VertXYZUV),
				verts);
	CHECK_OGL_ERRORS();

	glVertexAttribPointer(
				(GLuint)StreamIndex::TEXCOORD,
				2,
				GL_SHORT,
				GL_TRUE,
				sizeof(VertXYZUV),
				c);
	CHECK_OGL_ERRORS();

	glDrawArrays(GL_TRIANGLES,0,pVertices.size());
	CHECK_OGL_ERRORS();
}

//*******************************************
// Texture functions
uint32_t GLES::CreateTexture(int pWidth,int pHeight,const uint8_t* pPixels,TextureFormat pFormat,bool pFiltered,bool pGenerateMipmaps)
{
	const GLint format = TextureFormatToGLFormat(pFormat);
	if( format == GL_INVALID_ENUM )
	{
		THROW_MEANINGFUL_EXCEPTION("CreateTexture passed an unknown texture format, I can not continue.");
	}

	GLuint newTexture;
	glGenTextures(1,&newTexture);
	CHECK_OGL_ERRORS();
	if( newTexture == 0 )
	{
		THROW_MEANINGFUL_EXCEPTION("Failed to create texture, glGenTextures returned zero");
	}

	if( mTextures.find(newTexture) != mTextures.end() )
	{
		THROW_MEANINGFUL_EXCEPTION("Bug found in GLES code, glGenTextures returned an index that we already know about.");
	}

	mTextures[newTexture] = std::make_unique<GLTexture>(pFormat,pWidth,pHeight);

	glBindTexture(GL_TEXTURE_2D,newTexture);
	CHECK_OGL_ERRORS();

	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		format,
		pWidth,
		pHeight,
		0,
		format,
		GL_UNSIGNED_BYTE,
		pPixels);

	CHECK_OGL_ERRORS();

	// Unlike GLES 1.1 this is called after texture creation, in GLES 1.1 you say that you want glTexImage2D to make the mips.
	// Don't call if we don't yet have pixels. Will be called when you fill the texture.
	if( pPixels != nullptr )
	{
		if( pGenerateMipmaps )
		{
			glGenerateMipmap(GL_TEXTURE_2D);
			CHECK_OGL_ERRORS();
			if( pFiltered )
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			}
			else
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			}
		}
		else
		{
			if( pFiltered )
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			}
			else
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			}
		}
	}

	// If it's alpha only we need to set the texture swizzle for RGB to one.
	// Leaving in for when I add GLES 3.0 support. But for now, grump, need two textures.
	// GL_TEXTURE_SWIZZLE_R not supported in GLES 2.0
	/*
	if( format == GL_ALPHA )
	{
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_R,GL_ONE);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_G,GL_ONE);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_B,GL_ONE);
	}
	*/

	CHECK_OGL_ERRORS();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glBindTexture(GL_TEXTURE_2D,0);//Because we had to change it to setup the texture! Stupid GL!
	CHECK_OGL_ERRORS();

	VERBOSE_MESSAGE("Texture " << newTexture << " created, " << pWidth << "x" << pHeight << " Format = " << TextureFormatToString(pFormat) << " Mipmaps = " << (pGenerateMipmaps?"true":"false") << " Filtered = " << (pFiltered?"true":"false"));


	return newTexture;
}

void GLES::FillTexture(uint32_t pTexture,int pX,int pY,int pWidth,int pHeight,const uint8_t* pPixels,TextureFormat pFormat,bool pGenerateMips)
{
	glBindTexture(GL_TEXTURE_2D,pTexture);

	const GLint format = TextureFormatToGLFormat(pFormat);
	if( format == GL_INVALID_ENUM )
	{
		THROW_MEANINGFUL_EXCEPTION("FillTexture passed an unknown texture format, I can not continue.");
	}

	glTexSubImage2D(GL_TEXTURE_2D,
		0,
		pX,pY,
		pWidth,pHeight,
		format,GL_UNSIGNED_BYTE,
		pPixels);

	if( pGenerateMips )
	{
		glGenerateMipmap(GL_TEXTURE_2D);
	}

	glBindTexture(GL_TEXTURE_2D,0);//Because we had to change it to setup the texture! Stupid GL!
}


/**
 * @brief Delete the texture, will throw an exception is texture not found.
 * All textures are deleted when the GLES context is torn down so you only need to use this if you need to reclaim some memory.
 */
void GLES::DeleteTexture(uint32_t pTexture)
{
	if( pTexture == mDiagnostics.texture )
	{
		THROW_MEANINGFUL_EXCEPTION("An attempt was made to delete the debug texture, do not do this!");
	}

	if( pTexture == mPixelFont.texture )
	{
		THROW_MEANINGFUL_EXCEPTION("An attempt was made to delete the pixel font texture, do not do this!");
	}
	

	if( mTextures.find(pTexture) != mTextures.end() )
	{
		glDeleteTextures(1,(GLuint*)&pTexture);
		mTextures.erase(pTexture);
	}
}

int GLES::GetTextureWidth(uint32_t pTexture)const
{
	return mTextures.at(pTexture)->mWidth;
}

int GLES::GetTextureHeight(uint32_t pTexture)const
{
	return mTextures.at(pTexture)->mHeight;
}

// End of Texture commands.
//*******************************************
// 9 Patch code
uint32_t GLES::CreateNinePatch(int pWidth,int pHeight,const uint8_t* pPixels,bool pFiltered)
{
	if( pWidth < 8 || pWidth < 8 )
	{
		THROW_MEANINGFUL_EXCEPTION("CreateNinePatch passed image data that is too small, min size for each axis is 8 pixels");
	}

	if( pPixels == nullptr )
	{
		THROW_MEANINGFUL_EXCEPTION("CreateNinePatch passed null image data, nine patch creation requires pixel data");
	}

	const int newWidth = pWidth - 2;
	const int newHeight = pHeight - 2;
	const int newStride = newWidth * 4;
	const int oldStride = pWidth * 4;

	// Extract the information
	VertShortXY scaleFrom = {-1,-1};
	VertShortXY scaleTo = {-1,-1};
	VertShortXY fillFrom = {-1,-1};
	VertShortXY fillTo = {-1,-1};

	auto ScanNinePatch = [](uint8_t pPixel,int16_t pIndex,int16_t &pFrom,int16_t &pTo,const std::string& pWhat)
	{
		if( pPixel == 0xff )
		{	// Record first hit of solid.
			if( pFrom == -1 )
				pFrom = pIndex;
		}
		else if( pPixel == 0x00 )
		{
			// Wait till we've found the start before finding the end.
			if( pFrom != -1 )
			{	// Record first hit of not solid after solid.
				if( pTo == -1 )
					pTo = pIndex - 1;// Previous value is what we want.
			}
		}
		else
		{
			THROW_MEANINGFUL_EXCEPTION("Nine patch edge definition pixels contain invalid pix value for " + pWhat + " index " + std::to_string(pIndex) + " value " + std::to_string(pPixel) + ", is it really a nine patch texture?");
		}
	};

	// Scan for X scale and X fill start
	const uint8_t* firstRow = pPixels + 3;// + 3 is to get to the alpha channel
	const uint8_t* lastRow = pPixels + (oldStride * (pHeight-1)) + 3;
	for( int x = 0 ; x < pWidth ; x++ )
	{
		ScanNinePatch(firstRow[x*4],x,scaleFrom.x,scaleTo.x,"Scalable X");
		ScanNinePatch(lastRow[x*4],x,fillFrom.x,fillTo.x,"Fillable X");
	}

	// Scan to for scale and X fill start
	const uint8_t* firstColumn = pPixels + 3;// + 3 is to get to the alpha channel
	const uint8_t* lastColumn = pPixels + ((pWidth-1)*4) + 3;
	for( int y = 0 ; y < pHeight ; y++ )
	{
		ScanNinePatch(firstColumn[y * oldStride],y,scaleFrom.y,scaleTo.y,"Scalable Y");
		ScanNinePatch(lastColumn[y * oldStride],y,fillFrom.y,fillTo.y,"Fillable Y");
	}

	if( scaleFrom.x == -1 || scaleFrom.y == -1 || scaleTo.x == -1 || scaleTo.y == -1 || 
		fillFrom.x  == -1 || fillFrom.y  == -1 || fillTo.x  == -1 || fillTo.y  == -1 )
	{
#ifdef VERBOSE_BUILD
		std::clog << "Nine patch failure,\n";
		std::clog << "   Scalable X " << scaleFrom.x << " " << scaleTo.x << "\n";
		std::clog << "   Scalable Y " << scaleFrom.y << " " << scaleTo.y << "\n";
		std::clog << "   Fillable X " << fillFrom.x << " " << fillTo.x << "\n";
		std::clog << "   Fillable Y " << fillFrom.y << " " << fillTo.y << "\n";
#endif
		THROW_MEANINGFUL_EXCEPTION("Nine patch edge definition invlaid, not all scaling and filling information found. Is it a nine patch texture?");
	}

	// Remove the outer edge from pixel data
	uint8_t* newPixels = mWorkBuffers->scratchRam.Restart( newWidth * newHeight * 4 );
	uint8_t* dst = newPixels;
	const uint8_t* src = pPixels + oldStride + 4;
	for( int y = 1 ; y < pHeight-1 ; y++ )
	{
		memcpy(dst,src,newStride);
		dst += newStride;
		src += oldStride;
	}

	// Create the texture
	const uint32_t newTexture = CreateTexture(newWidth,newHeight,newPixels,TextureFormat::FORMAT_RGBA,pFiltered,false);
	if( newTexture == 0 )
	{
		THROW_MEANINGFUL_EXCEPTION("CreateNinePatch failed to create it's texture, you out of vram?");
	}

	if( mNinePatchs.find(newTexture) != mNinePatchs.end() )
	{
		THROW_MEANINGFUL_EXCEPTION("Bug found in GLES CreateNinePatch code, CreateTexture returned an index that we already know about.");
	}

	// Create the nine patch entry and return.
	mNinePatchs[newTexture] = std::make_unique<NinePatch>(newWidth,newHeight,scaleFrom,scaleTo,fillFrom,fillTo);

	return newTexture;
}

void GLES::DeleteNinePatch(uint32_t pNinePatch)
{
	if( mNinePatchs.find(pNinePatch) == mNinePatchs.end() )
	{
		THROW_MEANINGFUL_EXCEPTION("An attempt to delete a nine patch that is not a nine patch was made");
	}

	DeleteTexture(pNinePatch);	// The nine patch handle is also the texture handle.
	mNinePatchs.erase(pNinePatch);
}


/**
 * @brief 
 * @return const NinePatchDrawInfo& Don't hold onto this, will go away / change. Returned to help with rending of content in the fillable area of the nine patch.
 */
const NinePatchDrawInfo& GLES::DrawNinePatch(uint32_t pNinePatch,int pX,int pY,float pXScale,float pYScale)
{
	// Grab out nine pinch object with the data we need.
	auto found = mNinePatchs.find(pNinePatch);
	if( found == mNinePatchs.end() )
	{
		THROW_MEANINGFUL_EXCEPTION("An attempt to draw a nine patch that is not a nine patch was made");
	}
	const auto& ninePinch = found->second.get();

	// We have to draw 9 rects, with the center scaling the texture.
	const int xMove = pX + ((ninePinch->mScalable.to.x - ninePinch->mScalable.from.x) * pXScale);
	const int yMove = pY + ((ninePinch->mScalable.to.y - ninePinch->mScalable.from.y) * pYScale);
	VertShortXY* verts = mWorkBuffers->vertices2DShort.Restart(16);
	for( int y = 0 ; y < 4 ; y++ )
	{
		for( int x = 0 ; x < 4 ; x++, verts++ )
		{
			if( x < 2 )
			{
				verts->x = ninePinch->mVerts[x][y].x + pX;
			}
			else
			{
				verts->x = ninePinch->mVerts[x][y].x + xMove;
			}

			if( y < 2 )
			{
				verts->y = ninePinch->mVerts[x][y].y + pY;
			}
			else
			{
				verts->y = ninePinch->mVerts[x][y].y + yMove;
			}
		}
	}

	SelectAndEnableShader(pNinePatch,255,255,255,255);

	// Because UV's are normalised.
	glVertexAttribPointer(
				(GLuint)StreamIndex::TEXCOORD,
				2,
				GL_SHORT,
				GL_TRUE,
				4,ninePinch->mUVs);
	CHECK_OGL_ERRORS();

	static const uint8_t indices[9*6] =
	{
		0,1,5,0,5,4,
		1,2,6,1,6,5,
		2,3,7,2,7,6,

		4,5,9,4,9,8,
		5,6,10,5,10,9,
		6,7,11,6,11,10,

		8,9,13,8,13,12,
		9,10,14,9,14,13,
		10,11,15,10,15,14		
	};

	VertexPtr(2,GL_SHORT,mWorkBuffers->vertices2DShort.Data());
	glDrawElements(GL_TRIANGLES,9*6,GL_UNSIGNED_BYTE,indices);
	CHECK_OGL_ERRORS();


	return mNinePatchDrawInfo;
}


//*******************************************
// Pixel font, low res, mainly for debugging.
void GLES::FontPrint(int pX,int pY,const char* pText)
{
	const std::string_view s(pText);
	mWorkBuffers->vertices2DShort.Restart();
	mWorkBuffers->uvShort.Restart();

	// Get where the uvs will be written too.
	const int quadSize = 16 * mPixelFont.scale;
	const int squishHack = 3 * mPixelFont.scale;
	mWorkBuffers->vertices2DShort.BuildQuads(pX,pY,quadSize,quadSize,s.size(),quadSize - squishHack,0);

	// Get where the uvs will be written too.
	const int maxUV = 32767;
	const int charSize = maxUV / 16;
	for( auto c : s )
	{
		const int x = ((int)c&0x0f) * charSize;
		const int y = ((int)c>>4) * charSize;
		mWorkBuffers->uvShort.BuildQuad(x+64,y+64,charSize-128,charSize-128);// The +- 64 is because of filtering. Makes font look nice at normal size.
	}

	// Continue adding uvs to the buffer after the verts.
	EnableShader(mShaders.TextureAlphaOnly2D);
	mShaders.CurrentShader->SetTexture(mPixelFont.texture);
	mShaders.CurrentShader->SetGlobalColour(mPixelFont.R,mPixelFont.G,mPixelFont.B,mPixelFont.A);

	// how many?
	const int numVerts = mWorkBuffers->vertices2DShort.Used();

	glVertexAttribPointer(
				(GLuint)StreamIndex::TEXCOORD,
				2,
				GL_SHORT,
				GL_TRUE,
				4,mWorkBuffers->uvShort.Data());
	CHECK_OGL_ERRORS();

	VertexPtr(2,GL_SHORT,mWorkBuffers->vertices2DShort.Data());
	CHECK_OGL_ERRORS();
	glDrawArrays(GL_TRIANGLES,0,numVerts);
	CHECK_OGL_ERRORS();
}

void GLES::FontPrintf(int pX,int pY,const char* pFmt,...)
{
	char buf[1024];	
	va_list args;
	va_start(args, pFmt);
	vsnprintf(buf, sizeof(buf), pFmt, args);
	va_end(args);
	FontPrint(pX,pY, buf);
}

int GLES::FontGetPrintWidth(const char* pText)
{
	const std::string_view s(pText);

	// Get where the uvs will be written too.
	const int quadSize = 16 * mPixelFont.scale;
	const int squishHack = 3 * mPixelFont.scale;
	const int xStep = quadSize - squishHack;

	return (xStep * s.size());
}

int GLES::FontGetPrintfWidth(const char* pFmt,...)
{
	char buf[1024];	
	va_list args;
	va_start(args, pFmt);
	vsnprintf(buf, sizeof(buf), pFmt, args);
	va_end(args);
	return FontGetPrintWidth(buf);
}


// End of Pixel font.
//*******************************************

//*******************************************
// Free type rendering
#ifdef USE_FREETYPEFONTS
uint32_t GLES::FontLoad(const std::string& pFontName,int pPixelHeight)
{
	FT_Face loadedFace;
	if( FT_New_Face(mFreetype,pFontName.c_str(),0,&loadedFace) != 0 )
	{
		THROW_MEANINGFUL_EXCEPTION("Failed to load true type font " + pFontName);
	}

	const uint32_t fontID = mNextFontID++;
	mFreeTypeFonts[fontID] = std::make_unique<FreeTypeFont>(loadedFace,pPixelHeight);

	// Now we need to prepare the texture cache.
	auto& font = mFreeTypeFonts.at(fontID);
	font->BuildTexture(
		mMaximumAllowedGlyph,
		[this](int pWidth,int pHeight)
		{
			// Because the glyph rending to texture does not fill the whole texture the GL texture will not be created.
			// Do I have to make a big memory buffer, fill it with zero, then free the memory.
			auto zeroMemory = std::make_unique<uint8_t[]>(pWidth * pHeight);
			memset(zeroMemory.get(),0,pWidth * pHeight);

			return CreateTexture(pWidth,pHeight,zeroMemory.get(),TextureFormat::FORMAT_ALPHA);			
		},
		[this](uint32_t pTexture,int pX,int pY,int pWidth,int pHeight,const uint8_t* pPixels)
		{
			FillTexture(pTexture,pX,pY,pWidth,pHeight,pPixels,TextureFormat::FORMAT_ALPHA);
		}
	);

	VERBOSE_MESSAGE("Free type font loaded: " << pFontName << " with internal ID of " << fontID << " Using texture " << font->mTexture);

	return fontID;
}

void GLES::FontDelete(uint32_t pFont)
{
	mFreeTypeFonts.erase(pFont);
}

void GLES::FontSetColour(uint32_t pFont,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha)
{
	auto& font = mFreeTypeFonts.at(pFont);
	font->mColour.R = pRed;
	font->mColour.G = pGreen;
	font->mColour.B = pBlue;
	font->mColour.A = pAlpha;
}

void GLES::FontPrint(uint32_t pFont,int pX,int pY,const std::string_view& pText)
{
	auto& font = mFreeTypeFonts.at(pFont);

	mWorkBuffers->vertices2DShort.Restart();
	mWorkBuffers->uvShort.Restart();

	// Get where the uvs will be written too.
	for( auto c : pText )
	{
		if( c > 31 || c < 127 )
		{
			auto&g = font->mGlyphs.at(c-32);

			mWorkBuffers->vertices2DShort.BuildQuad(pX + g.x_off,pY + g.y_off,g.width,g.height);

			mWorkBuffers->uvShort.AddUVRect(
					g.uv[0].x,
					g.uv[0].y,
					g.uv[1].x,
					g.uv[1].y);

			pX += g.advance;
		}
		else
		{
			pX += font->mGlyphs[0].advance;
		}
	}

	assert(font->mTexture);
	EnableShader(mShaders.TextureAlphaOnly2D);
	mShaders.CurrentShader->SetTexture(font->mTexture);
	mShaders.CurrentShader->SetGlobalColour(font->mColour.R,font->mColour.G,font->mColour.B,font->mColour.A);

	// how many?
	const int numVerts = mWorkBuffers->vertices2DShort.Used();

	glVertexAttribPointer(
				(GLuint)StreamIndex::TEXCOORD,
				2,
				GL_SHORT,
				GL_TRUE,
				4,mWorkBuffers->uvShort.Data());

	VertexPtr(2,GL_SHORT,mWorkBuffers->vertices2DShort.Data());
	glDrawArrays(GL_TRIANGLES,0,numVerts);
	CHECK_OGL_ERRORS();
}

void GLES::FontPrintf(uint32_t pFont,int pX,int pY,const char* pFmt,...)
{
	char buf[1024];	
	va_list args;
	va_start(args, pFmt);
	vsnprintf(buf, sizeof(buf), pFmt, args);
	va_end(args);
	FontPrint(pFont,pX,pY, buf);
}

int GLES::FontGetPrintWidth(uint32_t pFont,const std::string_view& pText)
{
	auto& font = mFreeTypeFonts.at(pFont);

	// Get where the uvs will be written too.
	int x = 0;
	for( auto c : pText )
	{
		if( c > 31 || c < 127 )
		{
			x += font->mGlyphs.at(c-32).advance;
		}
		else
		{
			x += font->mGlyphs[0].advance;
		}
	}
	return x;
}

int GLES::FontGetPrintfWidth(uint32_t pFont,const char* pFmt,...)
{
	char buf[1024];	
	va_list args;
	va_start(args, pFmt);
	vsnprintf(buf, sizeof(buf), pFmt, args);
	va_end(args);
	return FontGetPrintWidth(pFont,buf);	
}

#endif
// End of free type font.
//*******************************************

void GLES::ProcessSystemEvents()
{
#ifdef PLATFORM_X11_GL
	mCTRL_C_Pressed = mPlatform->ProcessX11Events(mSystemEventHandler);
#endif

	// We don't bother to read the mouse if no pEventHandler has been registered. Would be a waste of time.
	if( mPointer.mDevice > 0 && mSystemEventHandler )
	{
		struct input_event ev;
		// Grab all messages and process befor going to next frame.
		while( read(mPointer.mDevice,&ev,sizeof(ev)) > 0 )
		{
			// EV_SYN is a seperator of events.
#ifdef VERBOSE_BUILD
			if( ev.type != EV_ABS && ev.type != EV_KEY && ev.type != EV_SYN )
			{// Anything I missed? 
				std::cout << std::hex << ev.type << " " << ev.code << " " << ev.value << "\n";
			}
#endif
			switch( ev.type )
			{
			case EV_KEY:
				switch (ev.code)
				{
				case BTN_TOUCH:
					SystemEventData data((ev.value != 0) ? SystemEventType::POINTER_DOWN : SystemEventType::POINTER_UP);
					data.mPointer.X = mPointer.mCurrent.x;
					data.mPointer.Y = mPointer.mCurrent.y;
					mSystemEventHandler(data);
					break;
				}
				break;

			case EV_ABS:
				switch (ev.code)
				{
				case ABS_X:
					mPointer.mCurrent.x = ev.value;
					break;

				case ABS_Y:
					mPointer.mCurrent.y = ev.value;
					break;
				}
				SystemEventData data(SystemEventType::POINTER_MOVE);
				data.mPointer.X = mPointer.mCurrent.x;
				data.mPointer.Y = mPointer.mCurrent.y;
				mSystemEventHandler(data);
				break;
			}
		}
	}

	// Finnally, did they ctrl + c ?
	if( mCTRL_C_Pressed )
	{
		VERBOSE_MESSAGE("CTRL trapped, quitting application");
		mCTRL_C_Pressed = false; // So we only send once.
		OnApplicationExitRequest();
	}
}

void GLES::SetRenderingDefaults()
{
	glViewport(0, 0, (GLsizei)mPhysical.Width, (GLsizei)mPhysical.Height);
	glDepthRangef(0.0f,1.0f);
	glPixelStorei(GL_UNPACK_ALIGNMENT,1);
	Begin2D();

	// Always cull, because why not. :) Make code paths simple.
	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CW);
	glCullFace(GL_BACK);

	// I have alpha blend on all the time. Makes life easy. No point in complicating the code for speed, going for simple implementation not fastest!
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);

	glEnableVertexAttribArray((int)StreamIndex::VERTEX);//Always on

	CHECK_OGL_ERRORS();
}

void GLES::BuildShaders()
{
	const char* ColourOnly2D_VS = R"(
		uniform mat4 u_proj_cam;
		uniform vec4 u_global_colour;
		attribute vec4 a_xyz;
		varying vec4 v_col;
		void main(void)
		{
			v_col = u_global_colour;
			gl_Position = u_proj_cam * a_xyz;
		}
	)";

	const char *ColourOnly2D_PS = R"(
		varying vec4 v_col;
		void main(void)
		{
			gl_FragColor = v_col;
		}
	)";

	mShaders.ColourOnly2D = std::make_unique<GLShader>("ColourOnly2D",ColourOnly2D_VS,ColourOnly2D_PS);

	const char* TextureColour2D_VS = R"(
		uniform mat4 u_proj_cam;
		uniform vec4 u_global_colour;
		attribute vec4 a_xyz;
		attribute vec2 a_uv0;
		varying vec4 v_col;
		varying vec2 v_tex0;
		void main(void)
		{
			v_col = u_global_colour;
			v_tex0 = a_uv0;
			gl_Position = u_proj_cam * a_xyz;
		}
	)";

	const char *TextureColour2D_PS = R"(
		varying vec4 v_col;
		varying vec2 v_tex0;
		uniform sampler2D u_tex0;
		void main(void)
		{
			gl_FragColor = v_col * texture2D(u_tex0,v_tex0);
		}
	)";

	mShaders.TextureColour2D = std::make_unique<GLShader>("TextureColour2D",TextureColour2D_VS,TextureColour2D_PS);

	const char* TextureAlphaOnly2D_VS = R"(
		uniform mat4 u_proj_cam;
		uniform vec4 u_global_colour;
		attribute vec4 a_xyz;
		attribute vec2 a_uv0;
		varying vec4 v_col;
		varying vec2 v_tex0;
		void main(void)
		{
			v_col = u_global_colour;
			v_tex0 = a_uv0;
			gl_Position = u_proj_cam * a_xyz;
		}
	)";

	const char *TextureAlphaOnly2D_PS = R"(
		varying vec4 v_col;
		varying vec2 v_tex0;
		uniform sampler2D u_tex0;
		void main(void)
		{
			gl_FragColor = vec4(v_col.rgb,texture2D(u_tex0,v_tex0).a);
		}
	)";

	mShaders.TextureAlphaOnly2D = std::make_unique<GLShader>("TextureAlphaOnly2D",TextureAlphaOnly2D_VS,TextureAlphaOnly2D_PS);
	
	const char* SpriteShader2D_VS = R"(
		uniform mat4 u_proj_cam;
		uniform mat4 u_trans;
		uniform vec4 u_global_colour;
		attribute vec4 a_xyz;
		attribute vec2 a_uv0;
		varying vec4 v_col;
		varying vec2 v_tex0;
		void main(void)
		{
			v_col = u_global_colour;
			v_tex0 = a_uv0;
			gl_Position = u_proj_cam * (u_trans * a_xyz);
		}
	)";

	const char *SpriteShader2D_PS = R"(
		varying vec4 v_col;
		varying vec2 v_tex0;
		uniform sampler2D u_tex0;
		void main(void)
		{
			gl_FragColor = v_col * texture2D(u_tex0,v_tex0);
		}
	)";

	mShaders.SpriteShader2D = std::make_unique<GLShader>("SpriteShader2D",SpriteShader2D_VS,SpriteShader2D_PS);

	const char* QuadBatchShader2D_VS = R"(
		uniform mat4 u_proj_cam;
		uniform vec4 u_global_colour;
		attribute vec4 a_xyz;
		attribute vec2 a_uv0;
		attribute vec4 a_trans;
		varying vec4 v_col;
		varying vec2 v_tex0;
		void main(void)
		{
			float scale = a_trans.w;
			float sCos = cos(a_trans.z * 0.00019175455);
			float sSin = sin(a_trans.z * 0.00019175455);

			mat4 trans;
			trans[0][0] = sCos * scale;
			trans[0][1] = sSin * scale;
			trans[0][2] = 0.0; 
			trans[0][3] = 0.0;

			trans[1][0] = -sSin * scale;
			trans[1][1] = sCos * scale;
			trans[1][2] = 0.0; 
			trans[1][3] = 0.0;

			trans[2][0] = 0.0;
			trans[2][1] = 0.0;
			trans[2][2] = scale;
			trans[2][3] = 0.0;

			trans[3][0] = a_trans.x;
			trans[3][1] = a_trans.y;
			trans[3][2] = 0.0;
			trans[3][3] = 1.0;

			v_col = u_global_colour;
			v_tex0 = a_uv0;
			gl_Position = u_proj_cam * (trans * a_xyz);
		}
	)";

	const char *QuadBatchShader2D_PS = R"(
		varying vec4 v_col;
		varying vec2 v_tex0;
		uniform sampler2D u_tex0;
		void main(void)
		{
			gl_FragColor = v_col * texture2D(u_tex0,v_tex0);
		}
	)";

	mShaders.QuadBatchShader2D = std::make_unique<GLShader>("QuadBatchShader2D",QuadBatchShader2D_VS,QuadBatchShader2D_PS);


	const char* ColourOnly3D_VS = R"(
		uniform mat4 u_proj_cam;
		uniform mat4 u_trans;
		uniform vec4 u_global_colour;		
		attribute vec4 a_xyz;
		attribute vec4 a_col;
		varying vec4 v_col;
		void main(void)
		{
			v_col = u_global_colour * a_col;
			gl_Position = u_proj_cam * (u_trans * a_xyz);
		}
	)";

	const char *ColourOnly3D_PS = R"(
		varying vec4 v_col;
		void main(void)
		{
			gl_FragColor = v_col;
		}
	)";

	mShaders.ColourOnly3D = std::make_unique<GLShader>("ColourOnly3D",ColourOnly3D_VS,ColourOnly3D_PS);	

	const char* TextureOnly3D_VS = R"(
		uniform mat4 u_proj_cam;
		uniform mat4 u_trans;
		uniform vec4 u_global_colour;		
		attribute vec4 a_xyz;
		attribute vec2 a_uv0;
		varying vec4 v_col;
		varying vec2 v_tex0;
		void main(void)
		{
			v_col = u_global_colour;
			v_tex0 = a_uv0;
			gl_Position = u_proj_cam * (u_trans * a_xyz);
		}
	)";

	const char *TextureOnly3D_PS = R"(
		varying vec4 v_col;
		varying vec2 v_tex0;
		uniform sampler2D u_tex0;
		void main(void)
		{
			gl_FragColor = v_col * texture2D(u_tex0,v_tex0);
		}
	)";

	mShaders.TextureOnly3D = std::make_unique<GLShader>("TextureOnly3D",TextureOnly3D_VS,TextureOnly3D_PS);	

}

void GLES::SelectAndEnableShader(uint32_t pTexture,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha)
{
	assert(mShaders.TextureAlphaOnly2D);
	assert(mShaders.TextureColour2D);
	assert(mShaders.ColourOnly2D);

	TinyShader aShader = mShaders.ColourOnly2D;
	if( pTexture > 0 )
	{
		if( mTextures.at(pTexture)->mFormat == TextureFormat::FORMAT_ALPHA )
		{
			aShader = mShaders.TextureAlphaOnly2D;
		}
		else
		{
			aShader = mShaders.TextureColour2D;
		}
	}

	EnableShader(aShader);
	mShaders.CurrentShader->SetGlobalColour(pRed,pGreen,pBlue,pAlpha);
	if( pTexture > 0 )
	{
		mShaders.CurrentShader->SetTexture(pTexture);
	}
}

void GLES::EnableShader(TinyShader pShader)
{
	assert( pShader );
	if( mShaders.CurrentShader != pShader )
	{
		mShaders.CurrentShader = pShader;
		pShader->Enable(mMatrices.projection);
		pShader->SetTransform(mMatrices.transform);
	}
}

void GLES::BuildDebugTexture()
{
	VERBOSE_MESSAGE("Creating mDiagnostics.texture");
	uint8_t pixels[16*16*4];
	uint8_t* dst = pixels;
	for( int y = 0 ; y < 16 ; y++ )
	{
		for( int x = 0 ; x < 16 ; x++ )
		{
			if( (x&1) == (y&1) )
			{
				dst[0] = 255;dst[1] = 0;dst[2] = 255;dst[3] = 255;
			}
			else
			{
				dst[0] = 0;dst[1] = 255;dst[2] = 0;dst[3] = 255;
			}
			dst+=4;
		}
	}
	// Put some dots in so I know which way is up and if it's flipped.
	pixels[(16*4) + (7*4) + 0] = 0xff;
	pixels[(16*4) + (7*4) + 1] = 0x0;
	pixels[(16*4) + (7*4) + 2] = 0x0;
	pixels[(16*4) + (8*4) + 0] = 0xff;
	pixels[(16*4) + (8*4) + 1] = 0x0;
	pixels[(16*4) + (8*4) + 2] = 0x0;

	pixels[(16*4*7) + (14*4) + 0] = 0x00;
	pixels[(16*4*7) + (14*4) + 1] = 0x0;
	pixels[(16*4*7) + (14*4) + 2] = 0xff;
	pixels[(16*4*8) + (14*4) + 0] = 0x00;
	pixels[(16*4*8) + (14*4) + 1] = 0x0;
	pixels[(16*4*8) + (14*4) + 2] = 0xff;

	mDiagnostics.texture = CreateTexture(16,16,pixels,tinygles::TextureFormat::FORMAT_RGBA);
}

void GLES::BuildPixelFontTexture()
{
	VERBOSE_MESSAGE("Creating pixel font texture");
	// This is alpha 4bits per pixel data. So we need to pad it out.
	uint8_t* pixels = mWorkBuffers->scratchRam.Restart(256*256);
	int n = 0;
	for( auto dword : mFont16x16Data )
	{
		for( int nibble = 0 ; nibble < 8 ; nibble++ )
		{
			const uint32_t shift = (7-nibble)*4;
			const uint32_t mask = (15<<shift);

			uint8_t a = (uint8_t)((dword&mask)>>shift);
			pixels[n++] = a<<4|a;
		}
	}

	mPixelFont.texture = CreateTexture(256,256,pixels,tinygles::TextureFormat::FORMAT_ALPHA,true);
}

void GLES::InitFreeTypeFont()
{
#ifdef USE_FREETYPEFONTS	
	if( FT_Init_FreeType(&mFreetype) == 0 )
	{
		VERBOSE_MESSAGE("Freetype font library created");
	}
	else
	{
		THROW_MEANINGFUL_EXCEPTION("Failed to init free type font library");
	}
#endif
}

void GLES::AllocateQuadBuffers()
{
	VERBOSE_MESSAGE("Creating quad buffers");
	// Fill quad index buffer.
	const size_t numIndices = mQuadBatch.IndicesPerQuad * mQuadBatch.MaxQuads;
	const size_t sizeofQuadIndexBuffer = sizeof(uint16_t) * numIndices;
	uint16_t* idx = (uint16_t*)mWorkBuffers->scratchRam.Restart(sizeofQuadIndexBuffer);
	const uint16_t* idx_end = idx + numIndices;
	uint16_t baseIndex = 0;
	for( size_t n = 0 ; n < mQuadBatch.MaxQuads ; n++, baseIndex += 4, idx += mQuadBatch.IndicesPerQuad )
	{
		assert( idx + mQuadBatch.IndicesPerQuad <= idx_end );

		idx[0] = 0 + baseIndex;
		idx[1] = 1 + baseIndex;
		idx[2] = 2 + baseIndex;
		idx[3] = 0 + baseIndex;
		idx[4] = 2 + baseIndex;
		idx[5] = 3 + baseIndex;
	}

	glGenBuffers(1,&mQuadBatch.IndicesBuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,mQuadBatch.IndicesBuffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeofQuadIndexBuffer,mWorkBuffers->scratchRam.Data(),GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
	CHECK_OGL_ERRORS();

	// Now build the verts for the quads, here we can use signed bytes.
	const size_t numberOfVectors = mQuadBatch.VerticesPerQuad * mQuadBatch.MaxQuads;
	const size_t sizeofQuadVertBuffer = sizeof(Vec2Db) * numberOfVectors;
	Vec2Db* v = (Vec2Db*)mWorkBuffers->scratchRam.Restart(sizeofQuadVertBuffer);
	const Vec2Db* v_end = v + numberOfVectors;
	for( size_t n = 0 ; n < mQuadBatch.MaxQuads ; n++, v += mQuadBatch.VerticesPerQuad )
	{
		assert( v + mQuadBatch.VerticesPerQuad <= v_end );

		v[0].x = -63;
		v[0].y = -63;

		v[1].x =  63;
		v[1].y = -63;

		v[2].x =  63;
		v[2].y =  63;

		v[3].x = -63;
		v[3].y =  63;
	}

	glGenBuffers(1,&mQuadBatch.VerticesBuffer);
	glBindBuffer(GL_ARRAY_BUFFER,mQuadBatch.VerticesBuffer);
	glBufferData(GL_ARRAY_BUFFER,sizeofQuadVertBuffer,mWorkBuffers->scratchRam.Data(),GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER,0);

	CHECK_OGL_ERRORS();
}

void GLES::VertexPtr(int pNum_coord, uint32_t pType,const void* pPointer)
{
	if(pNum_coord < 2 || pNum_coord > 3)
	{
		THROW_MEANINGFUL_EXCEPTION("VertexPtr passed invalid value for pNum_coord, must be 2 or 3 got " + std::to_string(pNum_coord));
	}

	glVertexAttribPointer(
				(GLuint)StreamIndex::VERTEX,
				pNum_coord,
				pType,
				pType == GL_BYTE,
				0,pPointer);
	CHECK_OGL_ERRORS();

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// Code to deal with CTRL + C
sighandler_t GLES::mUsersSignalAction = NULL;
bool GLES::mCTRL_C_Pressed = false;

void GLES::CtrlHandler(int SigNum)
{
	static int numTimesAskedToExit = 0;
	// Propergate to someone elses handler, if they felt they wanted to add one too.
	if( mUsersSignalAction != NULL )
	{
		mUsersSignalAction(SigNum);
	}

	if( numTimesAskedToExit > 2 )
	{
		std::cerr << "Asked to quit to many times, forcing exit in bad way\n";
		exit(1);
	}

	mCTRL_C_Pressed = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// GLES Shader definition
///////////////////////////////////////////////////////////////////////////////////////////////////////////
GLShader::GLShader(const std::string& pName,const char* pVertex, const char* pFragment) :
	mName(pName),
	mEnableStreamUV(strstr(pVertex," a_uv0;")),
	mEnableStreamTrans(strstr(pVertex," a_trans;")),
	mEnableStreamColour(strstr(pVertex," a_col;"))
{
	VERBOSE_SHADER_MESSAGE("Creating " << mName << " mEnableStreamUV " << mEnableStreamUV << " mEnableStreamTrans" << mEnableStreamTrans << " mEnableStreamColour" << mEnableStreamColour);

	mVertexShader = LoadShader(GL_VERTEX_SHADER,pVertex);

	mFragmentShader = LoadShader(GL_FRAGMENT_SHADER,pFragment);

	VERBOSE_SHADER_MESSAGE("vertex("<<mVertexShader<<") fragment("<<mFragmentShader<<")");

	mShader = glCreateProgram(); // create empty OpenGL Program
	CHECK_OGL_ERRORS();

	glAttachShader(mShader, mVertexShader); // add the vertex shader to program
	CHECK_OGL_ERRORS();

	glAttachShader(mShader, mFragmentShader); // add the fragment shader to program
	CHECK_OGL_ERRORS();

	//Set the input stream numbers.
	//Has to be done before linking.
	BindAttribLocation((int)StreamIndex::VERTEX, "a_xyz");
	BindAttribLocation((int)StreamIndex::TEXCOORD, "a_uv0");
	BindAttribLocation((int)StreamIndex::COLOUR, "a_col");
	BindAttribLocation((int)StreamIndex::TRANSFORM, "a_trans");

	glLinkProgram(mShader); // creates OpenGL program executables
	CHECK_OGL_ERRORS();

	GLint compiled;
	glGetProgramiv(mShader,GL_LINK_STATUS,&compiled);
	CHECK_OGL_ERRORS();
	if ( compiled == GL_FALSE )
	{	
		GLint infoLen = 0;
		glGetProgramiv ( mShader, GL_INFO_LOG_LENGTH, &infoLen );

		std::string error = "Failed to compile shader, infoLen " + std::to_string(infoLen) + "\n";
		if ( infoLen > 1 )
		{
			char* error_message = new char[infoLen];

			glGetProgramInfoLog(mShader,infoLen,&infoLen,error_message);

			error += error_message;

			delete []error_message;
		}
		glDeleteShader ( mShader );
		mShader = 0;
		THROW_MEANINGFUL_EXCEPTION(error);
	}

	VERBOSE_SHADER_MESSAGE("Shader " << mName << " Compiled ok");

	//Get the bits for the variables in the shader.
	mUniforms.proj_cam = GetUniformLocation("u_proj_cam");
	mUniforms.trans = GetUniformLocation("u_trans");
	mUniforms.global_colour = GetUniformLocation("u_global_colour");
	mUniforms.tex0 = GetUniformLocation("u_tex0");


	glUseProgram(0);
#ifdef VERBOSE_SHADER_BUILD
	gCurrentShaderName = "";
#endif
}

GLShader::~GLShader()
{
	VERBOSE_SHADER_MESSAGE("Deleting shader " << mName << " " << mShader);
	
	glDeleteShader(mVertexShader);
	CHECK_OGL_ERRORS();

	glDeleteShader(mFragmentShader);
	CHECK_OGL_ERRORS();

	glDeleteProgram(mShader);
	CHECK_OGL_ERRORS();
	mShader = 0;
}

int GLShader::GetUniformLocation(const char* pName)
{
	int location = glGetUniformLocation(mShader,pName);
	CHECK_OGL_ERRORS();

	if( location < 0 )
	{
		VERBOSE_SHADER_MESSAGE( mName << " Failed to find UniformLocation " << pName);
	}

	VERBOSE_SHADER_MESSAGE( mName << " GetUniformLocation(" << pName << ") == " << location);

	return location;

}

void GLShader::BindAttribLocation(int location,const char* pName)
{
	glBindAttribLocation(mShader, location,pName);
	CHECK_OGL_ERRORS();
	VERBOSE_SHADER_MESSAGE( mName << " AttribLocation("<< pName << "," << location << ")");
}

void GLShader::Enable(const float projInvcam[4][4])
{
#ifdef VERBOSE_SHADER_BUILD
	gCurrentShaderName = mName;
#endif

	assert(mShader);
    glUseProgram(mShader);
    CHECK_OGL_ERRORS();

    glUniformMatrix4fv(mUniforms.proj_cam, 1, false,(const float*)projInvcam);
    CHECK_OGL_ERRORS();

	if( mEnableStreamUV )
	{
		glEnableVertexAttribArray((int)StreamIndex::TEXCOORD);
	}
	else
	{
		glDisableVertexAttribArray((int)StreamIndex::TEXCOORD);
	}

	if( mEnableStreamTrans )
	{
		glEnableVertexAttribArray((int)StreamIndex::TRANSFORM);
	}
	else
	{
		glDisableVertexAttribArray((int)StreamIndex::TRANSFORM);
	}

	if( mEnableStreamColour )
	{
		glEnableVertexAttribArray((int)StreamIndex::COLOUR);
	}
	else
	{
		glDisableVertexAttribArray((int)StreamIndex::COLOUR);
	}
	


    CHECK_OGL_ERRORS();
}

void GLShader::SetTransform(float pTransform[4][4])
{
	if( mUniforms.trans >= 0 )
	{
		glUniformMatrix4fv(mUniforms.trans, 1, false,(const GLfloat*)pTransform);
		CHECK_OGL_ERRORS();
	}
}

void GLShader::SetGlobalColour(uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha)
{
	SetGlobalColour(
		ColourToFloat(pRed),
		ColourToFloat(pGreen),
		ColourToFloat(pBlue),
		ColourToFloat(pAlpha)
	);
	CHECK_OGL_ERRORS();
}

void GLShader::SetGlobalColour(float pRed,float pGreen,float pBlue,float pAlpha)
{
	glUniform4f(mUniforms.global_colour,pRed,pGreen,pBlue,pAlpha);
}

void GLShader::SetTexture(GLint pTexture)
{
	assert(pTexture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D,pTexture);
	glUniform1i(mUniforms.tex0,0);
	CHECK_OGL_ERRORS();
}

int GLShader::LoadShader(int type, const char* shaderCode)
{
	// create a vertex shader type (GLES20.GL_VERTEX_SHADER)
	// or a fragment shader type (GLES20.GL_FRAGMENT_SHADER)
	int shaderFrag = glCreateShader(type);

	// If we're GLES system we need to add "precision highp float"
#ifdef PLATFORM_GLES
	const std::string glesShaderCode= std::string("precision highp float; ") + shaderCode;
	shaderCode = glesShaderCode.c_str();
#endif

	// add the source code to the shader and compile it
	glShaderSource(shaderFrag,1,&shaderCode,NULL);
	glCompileShader(shaderFrag);
	CHECK_OGL_ERRORS();
	// Check the compile status
	GLint compiled;
	glGetShaderiv(shaderFrag,GL_COMPILE_STATUS,&compiled);
	if ( compiled == GL_FALSE )
	{
		GLint infoLen = 0;
		glGetShaderiv ( shaderFrag, GL_INFO_LOG_LENGTH, &infoLen );
		std::string error = "Failed to compile shader, infoLen " + std::to_string(infoLen) + "\n";

		if ( infoLen > 1 )
		{
			char* error_message = new char[infoLen];

			glGetShaderInfoLog(shaderFrag,infoLen,&infoLen,error_message);

			error += error_message;
		}
		glDeleteShader ( shaderFrag );
		THROW_MEANINGFUL_EXCEPTION(error);
	}
	CHECK_OGL_ERRORS();

	return shaderFrag;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// GLES Error implementation
#ifdef DEBUG_BUILD
void ReadOGLErrors(const char *pSource_file_name,int pLine_number)
{
	int gl_error_code = glGetError();
	if( gl_error_code == GL_NO_ERROR )
	{
		return;
	}

#ifdef VERBOSE_SHADER_BUILD
	if( gCurrentShaderName.size() )
	{
		VERBOSE_SHADER_MESSAGE("Current shader: " << gCurrentShaderName );
	}
	else
	{
		VERBOSE_SHADER_MESSAGE("No shader selected: " << gCurrentShaderName );
	}
#endif
	std:: cerr << "\n**********************\nline " << pLine_number << " file " << pSource_file_name << "\n";
	while(gl_error_code != GL_NO_ERROR)
	{
		std:: cerr << "GL error[%d]: :" << gl_error_code;
		switch(gl_error_code)
		{
		default:
			std:: cerr << "Unknown OGL error code\n";
			break;

		case GL_INVALID_ENUM:
			std:: cerr << "An unacceptable value is specified for an enumerated argument. The offending command is ignored, having no side effect other than to set the error flag.\n";
			break;

		case GL_INVALID_VALUE:
			std:: cerr << "A numeric argument is out of range. The offending command is ignored, having no side effect other than to set the error flag.\n";
			break;

		case GL_INVALID_OPERATION:
			std:: cerr << "The specified operation is not allowed in the current state. The offending command is ignored, having no side effect other than to set the error flag.\n";
			break;

		case GL_OUT_OF_MEMORY:
			std:: cerr << "There is not enough memory left to execute the command. The state of the GL is undefined, except for the state of the error flags, after this error is recorded.\n";
			break;
		}
		//Get next error.
		int last = gl_error_code;
		gl_error_code = glGetError();
		if( last == gl_error_code )
			break;
	}

	std:: cerr << "**********************\n";
}
#endif

#ifdef USE_FREETYPEFONTS
///////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief Optional freetype font library support. Is optional as the code is dependant on a library tha may not be avalibel for the host platform.
 * One note, I don't do localisation. ASCII here. If you need all the characters then maybe add yourself or use a commercial grade GL engine. :) localisation is a BIG job!
 * Rendering is done in the GL code, this class is more of just a container.
 */
FreeTypeFont::FreeTypeFont(FT_Face pFontFace,int pPixelHeight) :
	mFontName(pFontFace->family_name),
	mFace(pFontFace)
{
	if( FT_Set_Pixel_Sizes(mFace,0,pPixelHeight) == 0 )
	{
		VERBOSE_MESSAGE("Set pixel size " << pPixelHeight << " for true type font " << mFontName);
	}
	else
	{
		VERBOSE_MESSAGE("Failed to set pixel size " << pPixelHeight << " for true type font " << mFontName);
	}
}

FreeTypeFont::~FreeTypeFont()
{
	FT_Done_Face(mFace);	
}

bool FreeTypeFont::GetGlyph(char pChar,FreeTypeFont::Glyph& rGlyph,std::vector<uint8_t>& rPixels)
{
	assert(mFace);

	// Copied from original example source by Kevin Boone. http://kevinboone.me/fbtextdemo.html?i=1

	// Note that TT fonts have no built-in padding. 
	// That is, first,
	//  the top row of the bitmap is the top row of pixels to 
	//  draw. These rows usually won't be at the face bounding box. We need to
	//  work out the overall height of the character cell, and
	//  offset the drawing vertically by that amount. 
	//
	// Similar, there is no left padding. The first pixel in each row will not
	//  be drawn at the left margin of the bounding box, but in the centre of
	//  the screen width that will be occupied by the glyph.
	//
	//  We need to calculate the x and y offsets of the glyph, but we can't do
	//  this until we've loaded the glyph, because metrics
	//  won't be available.

	// Note that, by default, TT metrics are in 64'ths of a pixel, hence
	//  all the divide-by-64 operations below.

	// Get a FreeType glyph index for the character. If there is no
	//  glyph in the face for the character, this function returns
	//  zero.  
	FT_UInt gi = FT_Get_Char_Index (mFace, pChar);
	if( gi == 0 )
	{// Character not found, so default to space.
		VERBOSE_MESSAGE("Font: "<< mFontName << " Failed find glyph for character index " << (int)pChar);
		return false;
	}

	// Loading the glyph makes metrics data available
	if( FT_Load_Glyph (mFace, gi, FT_LOAD_DEFAULT ) != 0 )
	{
		VERBOSE_MESSAGE("Font: "<< mFontName << " Failed to load glyph for character index " << (int)pChar);
		return false;
	}

	// Rendering a loaded glyph creates the bitmap
	if( FT_Render_Glyph(mFace->glyph, FT_RENDER_MODE_NORMAL) != 0 )
	{
		VERBOSE_MESSAGE("Font: "<< mFontName << " Failed to render glyph for character index " << (int)pChar);
		return false;
	}

	assert(mFace->glyph);

	// Now we have the metrics, let's work out the x and y offset
	//  of the glyph from the specified x and y. Because there is
	//  no padding, we can't just draw the bitmap so that it's
	//  TL corner is at (x,y) -- we must insert the "missing" 
	//  padding by aligning the bitmap in the space available.

	// bbox.yMax is the height of a bounding box that will enclose
	//  any glyph in the face, starting from the glyph baseline.
	// Code changed, was casing it to render in the Y center of the font not on the base line. Will add it as an option in the future. Richard.
	int bbox_ymax = 0;//mFace->bbox.yMax / 64;

	// glyph_width is the pixel width of this specific glyph
	int glyph_width = mFace->glyph->metrics.width / 64;

	// So now we have (x_off,y_off), the location at which to
	//   start drawing the glyph bitmap.

	// Build the new glyph.
	rGlyph.width = mFace->glyph->bitmap.width;
	rGlyph.height = mFace->glyph->bitmap.rows;
	rGlyph.pitch = mFace->glyph->bitmap.pitch;

	// Advance is the amount of x spacing, in pixels, allocated
	//   to this glyph
	rGlyph.advance = mFace->glyph->metrics.horiAdvance / 64;


	// horiBearingX is the height of the top of the glyph from
	//   the baseline. So we work out the y offset -- the distance
	//   we must push down the glyph from the top of the bounding
	//   box -- from the height and the Y bearing.
	rGlyph.y_off = bbox_ymax - mFace->glyph->metrics.horiBearingY / 64;

	// Work out where to draw the left-most row of pixels --
	//   the x offset -- by halving the space between the 
	//   glyph width and the advance
	rGlyph.x_off = (rGlyph.advance - glyph_width) / 2;

	// It's an alpha only texture
	const size_t expectedSize = mFace->glyph->bitmap.rows * mFace->glyph->bitmap.pitch;
	// Some have no pixels, and so we just stop here.
	if(expectedSize == 0)
	{
		VERBOSE_MESSAGE("Font character " << pChar << " has no pixels " << mFace->glyph->bitmap.rows << " " << mFace->glyph->bitmap.pitch );
		return true;
	}

	assert(mFace->glyph->bitmap.buffer);

	if( mFace->glyph->bitmap.pitch == (int)mFace->glyph->bitmap.width )
	{// Quick path. Normally taken.
		rPixels.resize(expectedSize);
		memcpy(rPixels.data(),mFace->glyph->bitmap.buffer,expectedSize);
	}
	else
	{
		rPixels.reserve(expectedSize);
		const uint8_t* src = mFace->glyph->bitmap.buffer;
		for (int i = 0; i < (int)mFace->glyph->bitmap.rows; i++ , src += mFace->glyph->bitmap.pitch )
		{
			for (int j = 0; j < (int)mFace->glyph->bitmap.width; j++ )
			{
				rPixels.push_back(src[j]);
			}
		}
	}

	if( expectedSize != rPixels.size() )
	{
		THROW_MEANINGFUL_EXCEPTION("Font: " + mFontName + " Error, we read more pixels for free type font than expected for the glyph " + pChar );
	}

	return true;
}

void FreeTypeFont::BuildTexture(
			int pMaximumAllowedGlyph,
			std::function<uint32_t(int pWidth,int pHeight)> pCreateTexture,
			std::function<void(uint32_t pTexture,int pX,int pY,int pWidth,int pHeight,const uint8_t* pPixels)> pFillTexture)
{

	int maxX = 0,maxY = 0;

	std::array<std::vector<uint8_t>,96>glyphsPixels;
	for( int c = 0 ; c < 96 ; c++ )// Cheap and quick font ASCII renderer. I'm not geeting into unicode. It's a nightmare to make fast in GL on a resource constrained system!
	{
		auto& g = mGlyphs.at(c);
		auto& p = glyphsPixels.at(c);
		if( GetGlyph((char)(c+32),g,p) )
		{
			if( p.size() > 0 )
			{
				maxX = std::max(maxX,g.width);
				maxY = std::max(maxY,g.height);
			}
			else
			{
				VERBOSE_MESSAGE("Character " << c << " is empty, will just move the cursor " << g.advance << " pixels");
			}
		}
	}
	VERBOSE_MESSAGE("Font max glyph size requirement for cache is " << maxX << " " << maxY);
	if( maxX > pMaximumAllowedGlyph || maxY > pMaximumAllowedGlyph )
	{
		THROW_MEANINGFUL_EXCEPTION("Font: " + mFontName + " requires a very large texture as it's maximun size glyph is very big, maxX == " + std::to_string(maxX) + " maxY == " + std::to_string(maxY) + ". This creation has been halted. Please reduce size of font!");
	}

	auto nextPow2 = [](int v)
	{
		int pow2 = 1;
		while( pow2 < v )
		{
			pow2 <<= 1;
		}
		return pow2;
	};

	// Work out a texture size that will fit. Need 96 slots. 32 -> 127
	const int width = nextPow2(maxX * 12);
	const int height = nextPow2(maxY * 8);
	VERBOSE_MESSAGE("Texture size needed is << " << width << "x" << height);

	mTexture = pCreateTexture(width,height);
	assert(mTexture);

	// Now get filling. Could have a lot of wasted space, but I am not getting into complicated packing algos at load time. Take it offline. :)
	const int cellWidth = width / 12;
	const int cellHeight = height / 8;
	const int maxUV = 32767;
	int n = 0;
	for( int y = 0 ; y < 8 ; y++ )
	{
		for( int x = 0 ; x < 12 ; x++, n++ )
		{
			auto& g = mGlyphs.at(n);
			auto& p = glyphsPixels.at(n);
			const int cx = (x * cellWidth) + (cellWidth/2) - (g.width / 2);
			const int cy = (y * cellHeight) + (cellHeight/2) - (g.height / 2);
			if( p.size() > 0 )
			{
				pFillTexture(
					mTexture,
					cx,
					cy,
					g.width,
					g.height,
					p.data()
					);

				g.uv[0].x = (cx * maxUV) / width;
				g.uv[0].y = (cy * maxUV) / height;
				g.uv[1].x = ((cx + g.width) * maxUV)  / width;
				g.uv[1].y = ((cy + g.height) * maxUV) / height;
			}
			else
			{
				g.uv[0].x = 0;
				g.uv[0].y = 0;
				g.uv[1].x = 0;
				g.uv[1].y = 0;
			}
		}
	}	
}

#endif //#ifdef USE_FREETYPEFONTS


///////////////////////////////////////////////////////////////////////////////////////////////////////////
// EGL layer implementation
#ifdef PLATFORM_EGL
PlatformInterface::PlatformInterface()
{
	int File = open("/dev/fb0", O_RDWR);
	if(ioctl(File, FBIOGET_VSCREENINFO, &mScreenInfo) ) 
	{
		THROW_MEANINGFUL_EXCEPTION("Failed to open ioctl on /dev/fb0. Can not fetch screen mode!");
	}
	close(File);

	if( mScreenInfo.xres < 16 || mScreenInfo.yres < 16 )
	{
		THROW_MEANINGFUL_EXCEPTION("failed to find sensible screen mode from /dev/fb0");
	}
}

PlatformInterface::~PlatformInterface()
{
	VERBOSE_MESSAGE("Destroying context");
	eglDestroyContext(mDisplay, mContext);
    eglDestroySurface(mDisplay, mSurface);
    eglTerminate(mDisplay);
}

void PlatformInterface::SwapBuffers()
{
	assert(mDisplay);
	assert(mSurface);
	eglSwapBuffers(mDisplay,mSurface);
}

void PlatformInterface::InitialiseDisplay()
{
#ifdef BROADCOM_NATIVE_WINDOW
	bcm_host_init();
#endif

	VERBOSE_MESSAGE("Calling eglGetDisplay");
	mDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if( !mDisplay )
	{
		THROW_MEANINGFUL_EXCEPTION("Couldn\'t open the EGL default display");
	}

	//Now we have a display lets initialize it.
	EGLint majorVersion,minorVersion;
	if( !eglInitialize(mDisplay, &majorVersion, &minorVersion) )
	{
		THROW_MEANINGFUL_EXCEPTION("eglInitialize() failed");
	}
	CHECK_OGL_ERRORS();
	VERBOSE_MESSAGE("EGL version " << majorVersion << "." << minorVersion);
	eglBindAPI(EGL_OPENGL_ES_API);
	CHECK_OGL_ERRORS();

	FindEGLConfiguration();

	//We have our display and have chosen the config so now we are ready to create the rendering context.
	VERBOSE_MESSAGE("Creating context");
	EGLint ai32ContextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	mContext = eglCreateContext(mDisplay,mConfig,EGL_NO_CONTEXT,ai32ContextAttribs);
	if( !mContext )
	{
		THROW_MEANINGFUL_EXCEPTION("Failed to get a rendering context");
	}

// This is annoying but GLES is just broken on RPi 3,2,1 and always has been and always will be. RPi4 is now DRM.
#ifdef BROADCOM_NATIVE_WINDOW
	VC_RECT_T dst_rect;
	VC_RECT_T src_rect;

	dst_rect.x = 0;
	dst_rect.y = 0;
	dst_rect.width = GetWidth();
	dst_rect.height = GetHeight();

	src_rect.x = 0;
	src_rect.y = 0;
	src_rect.width = dst_rect.width << 16;
	src_rect.height = dst_rect.height << 16;        

	DISPMANX_DISPLAY_HANDLE_T dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
	DISPMANX_UPDATE_HANDLE_T dispman_update = vc_dispmanx_update_start( 0 );

	DISPMANX_ELEMENT_HANDLE_T dispman_element = vc_dispmanx_element_add(
			dispman_update,
			dispman_display,
			0,&dst_rect,
			0,&src_rect,
			DISPMANX_PROTECTION_NONE,
			nullptr,nullptr,
			DISPMANX_NO_ROTATE);

	mNativeWindow.element = dispman_element;
	mNativeWindow.width = GetWidth();
	mNativeWindow.height =  GetHeight();
	vc_dispmanx_update_submit_sync( dispman_update );
	mSurface = eglCreateWindowSurface(mDisplay,mConfig,&mNativeWindow,0);
#else
	mSurface = eglCreateWindowSurface(mDisplay,mConfig,mNativeWindow,0);
#endif //BROADCOM_NATIVE_WINDOW
	CHECK_OGL_ERRORS();

	eglMakeCurrent(mDisplay, mSurface, mSurface, mContext );
	CHECK_OGL_ERRORS();

	eglSwapInterval(mDisplay,1);
	glColorMask(EGL_TRUE,EGL_TRUE,EGL_TRUE,EGL_FALSE);// Have to mask out alpha as some systems (RPi show the terminal behind)
}

void PlatformInterface::FindEGLConfiguration()
{
	int depths_32_to_16[3] = {32,24,16};

	for( int c = 0 ; c < 3 ; c++ )
	{
		const EGLint attrib_list[] =
		{
			EGL_RED_SIZE,			8,
			EGL_GREEN_SIZE,			8,
			EGL_BLUE_SIZE,			8,
			EGL_ALPHA_SIZE,			8,
			EGL_DEPTH_SIZE,			depths_32_to_16[c],
			EGL_STENCIL_SIZE,		EGL_DONT_CARE,
			EGL_RENDERABLE_TYPE,	EGL_OPENGL_ES2_BIT,
			EGL_NONE,				EGL_NONE
		};

		EGLint numConfigs;
		if( !eglChooseConfig(mDisplay,attrib_list,&mConfig,1, &numConfigs) )
		{
			THROW_MEANINGFUL_EXCEPTION("Error: eglGetConfigs() failed");
		}

		if( numConfigs > 0 )
		{
			EGLint bufSize,r,g,b,a,z,s = 0;

			eglGetConfigAttrib(mDisplay,mConfig,EGL_BUFFER_SIZE,&bufSize);

			eglGetConfigAttrib(mDisplay,mConfig,EGL_RED_SIZE,&r);
			eglGetConfigAttrib(mDisplay,mConfig,EGL_GREEN_SIZE,&g);
			eglGetConfigAttrib(mDisplay,mConfig,EGL_BLUE_SIZE,&b);
			eglGetConfigAttrib(mDisplay,mConfig,EGL_ALPHA_SIZE,&a);

			eglGetConfigAttrib(mDisplay,mConfig,EGL_DEPTH_SIZE,&z);
			eglGetConfigAttrib(mDisplay,mConfig,EGL_STENCIL_SIZE,&s);

			CHECK_OGL_ERRORS();

			VERBOSE_MESSAGE("Config found:");
			VERBOSE_MESSAGE("\tFrame buffer size " << bufSize);
			VERBOSE_MESSAGE("\tRGBA " << r << "," << g << "," << b << "," << a);
			VERBOSE_MESSAGE("\tZBuffer " << z+s << "Z " << z << "S " << s);

			return;// All good :)
		}
	}
	THROW_MEANINGFUL_EXCEPTION("No matching EGL configs found");
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// Direct Render Manager layer implementation
// DRM Direct Render Manager hidden definition.
// Used for more model systems like RPi4 and proper GL / GLES implementations.
#ifdef PLATFORM_DIRECT_RENDER_MANAGER
PlatformInterface::PlatformInterface()
{
	if( drmAvailable() == 0 )
	{
		THROW_MEANINGFUL_EXCEPTION("Kernel DRM driver not loaded");
	}

	// Lets go searching for a connected direct render manager device.
	// Later I could add a param to allow user to specify this.
	drmDevicePtr devices[8] = { NULL };
	int num_devices = drmGetDevices2(0, devices, 8);
	if (num_devices < 0)
	{
		THROW_MEANINGFUL_EXCEPTION("drmGetDevices2 failed: " + std::string(strerror(-num_devices)) );
	}

	mDRMFile = -1;
	for( int n = 0 ; n < num_devices && mDRMFile < 0 ; n++ )
	{
		if( devices[n]->available_nodes&(1 << DRM_NODE_PRIMARY) )
		{
			// See if we can open it...
			VERBOSE_MESSAGE("Trying DRM device " << std::string(devices[n]->nodes[DRM_NODE_PRIMARY]));
			mDRMFile = open(devices[n]->nodes[DRM_NODE_PRIMARY], O_RDWR);
		}
	}
	drmFreeDevices(devices, num_devices);

	if( mDRMFile < 0 )
	{
		THROW_MEANINGFUL_EXCEPTION("DirectRenderManager: Failed to find and open direct rendering manager device" );
	}
	drmModeRes* resources = drmModeGetResources(mDRMFile);
	if( resources == nullptr )
	{
		THROW_MEANINGFUL_EXCEPTION("DirectRenderManager: Failed get mode resources");
	}

	drmModeConnector* connector = nullptr;
	for(int n = 0 ; n < resources->count_connectors && connector == nullptr ; n++ )
	{
		connector = drmModeGetConnector(mDRMFile, resources->connectors[n]);
		if( connector && connector->connection != DRM_MODE_CONNECTED )
		{// Not connected, check next one...
			drmModeFreeConnector(connector);
			connector = nullptr;
		}
	}
	if( connector == nullptr )
	{
		THROW_MEANINGFUL_EXCEPTION("DirectRenderManager: Failed get mode connector");
	}
	mConnector = connector;

	for( int i = 0 ; i < connector->count_modes && mModeInfo == nullptr ; i++ )
	{
		if( connector->modes[i].type & DRM_MODE_TYPE_PREFERRED )
		{// DRM really wants us to use this, this should be the best option for LCD displays.
			mModeInfo = &connector->modes[i];
			VERBOSE_MESSAGE("Preferred screen mode found");
		}
	}

	if( GetWidth() == 0 || GetHeight() == 0 )
	{
		THROW_MEANINGFUL_EXCEPTION("DirectRenderManager: Failed to find screen mode");
	}

	// Now grab the encoder, we need it for the CRTC ID. This is display connected to the conector.
	for( int n = 0 ; n < resources->count_encoders && mModeEncoder == nullptr ; n++ )
	{
		drmModeEncoder *encoder = drmModeGetEncoder(mDRMFile, resources->encoders[n]);
		if( encoder->encoder_id == connector->encoder_id )
		{
			mModeEncoder = encoder;
		}
		else
		{
			drmModeFreeEncoder(encoder);
		}
	}

	drmModeFreeResources(resources);	
}

PlatformInterface::~PlatformInterface()
{
	VERBOSE_MESSAGE("Destroying context");
	eglDestroyContext(mDisplay, mContext);
    eglDestroySurface(mDisplay, mSurface);
    eglTerminate(mDisplay);

	VERBOSE_MESSAGE("Cleaning up DRM");
	gbm_surface_destroy(mNativeWindow);
	gbm_device_destroy(mBufferManager);
	drmModeFreeEncoder(mModeEncoder);
	drmModeFreeConnector(mConnector);
	close(mDRMFile);
}

void PlatformInterface::InitialiseDisplay()
{
	VERBOSE_MESSAGE("Calling DRM InitialiseDisplay");

	mBufferManager = gbm_create_device(mDRMFile);
	mDisplay = eglGetDisplay(mBufferManager);
	if( !mDisplay )
	{
		THROW_MEANINGFUL_EXCEPTION("Couldn\'t open the EGL default display");
	}

	//Now we have a display lets initialize it.
	EGLint majorVersion,minorVersion;
	if( !eglInitialize(mDisplay, &majorVersion, &minorVersion) )
	{
		THROW_MEANINGFUL_EXCEPTION("eglInitialize() failed");
	}
	CHECK_OGL_ERRORS();
	VERBOSE_MESSAGE("EGL version " << majorVersion << "." << minorVersion);
	eglBindAPI(EGL_OPENGL_ES_API);
	CHECK_OGL_ERRORS();

	FindEGLConfiguration();

	//We have our display and have chosen the config so now we are ready to create the rendering context.
	VERBOSE_MESSAGE("Creating context");
	EGLint ai32ContextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	mContext = eglCreateContext(mDisplay,mConfig,EGL_NO_CONTEXT,ai32ContextAttribs);
	if( !mContext )
	{
		THROW_MEANINGFUL_EXCEPTION("Failed to get a rendering context");
	}

	mNativeWindow = gbm_surface_create(mBufferManager,GetWidth(), GetHeight(),mFOURCC_Format,GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	mSurface = eglCreateWindowSurface(mDisplay,mConfig,mNativeWindow,0);
	CHECK_OGL_ERRORS();

	eglMakeCurrent(mDisplay, mSurface, mSurface, mContext );
	CHECK_OGL_ERRORS();
}

void PlatformInterface::FindEGLConfiguration()
{
	int depths_32_to_16[3] = {32,24,16};

	for( int c = 0 ; c < 3 ; c++ )
	{
		const EGLint attrib_list[] =
		{
			EGL_RED_SIZE,			8,
			EGL_GREEN_SIZE,			8,
			EGL_BLUE_SIZE,			8,
			EGL_ALPHA_SIZE,			8,
			EGL_DEPTH_SIZE,			depths_32_to_16[c],
			EGL_STENCIL_SIZE,		EGL_DONT_CARE,
			EGL_RENDERABLE_TYPE,	EGL_OPENGL_ES2_BIT,
			EGL_NONE,				EGL_NONE
		};

		EGLint numConfigs;
		if( !eglChooseConfig(mDisplay,attrib_list,&mConfig,1, &numConfigs) )
		{
			THROW_MEANINGFUL_EXCEPTION("Error: eglGetConfigs() failed");
		}

		if( numConfigs > 0 )
		{
			EGLint bufSize,r,g,b,a,z,s = 0;

			eglGetConfigAttrib(mDisplay,mConfig,EGL_BUFFER_SIZE,&bufSize);

			eglGetConfigAttrib(mDisplay,mConfig,EGL_RED_SIZE,&r);
			eglGetConfigAttrib(mDisplay,mConfig,EGL_GREEN_SIZE,&g);
			eglGetConfigAttrib(mDisplay,mConfig,EGL_BLUE_SIZE,&b);
			eglGetConfigAttrib(mDisplay,mConfig,EGL_ALPHA_SIZE,&a);

			eglGetConfigAttrib(mDisplay,mConfig,EGL_DEPTH_SIZE,&z);
			eglGetConfigAttrib(mDisplay,mConfig,EGL_STENCIL_SIZE,&s);

			CHECK_OGL_ERRORS();

			// Get the format we need DRM buffers to match.
			if( r == 8 && g == 8 && b == 8 )
			{
				if( a == 8 )
				{
					mFOURCC_Format = DRM_FORMAT_ARGB8888;
				}
				else
				{
					mFOURCC_Format = DRM_FORMAT_RGB888;
				}
			}
			else
			{
				mFOURCC_Format = DRM_FORMAT_RGB565;
			}

			VERBOSE_MESSAGE("Config found:");
			VERBOSE_MESSAGE("\tFrame buffer size " << bufSize);
			VERBOSE_MESSAGE("\tRGBA " << r << "," << g << "," << b << "," << a);
			VERBOSE_MESSAGE("\tZBuffer " << z+s << "Z " << z << "S " << s);

			return;// All good :)
		}
	}
	THROW_MEANINGFUL_EXCEPTION("No matching EGL configs found");
}

static void drm_fb_destroy_callback(struct gbm_bo *bo, void *data)
{
	uint32_t* user_data = (uint32_t*)data;
	delete user_data;
}

void PlatformInterface::UpdateCurrentBuffer()
{
	assert(mNativeWindow);
	mCurrentFrontBufferObject = gbm_surface_lock_front_buffer(mNativeWindow);
	if( mCurrentFrontBufferObject == nullptr )
	{
		THROW_MEANINGFUL_EXCEPTION("Failed to lock front buffer from native window.");
	}

	uint32_t* user_data = (uint32_t*)gbm_bo_get_user_data(mCurrentFrontBufferObject);
	if( user_data == nullptr )
	{
		// Annoying JIT allocation. Should only happen twice.
		// Should look at removing the need for the libgbm

		const uint32_t handles[4] = {gbm_bo_get_handle(mCurrentFrontBufferObject).u32,0,0,0};
		const uint32_t strides[4] = {gbm_bo_get_stride(mCurrentFrontBufferObject),0,0,0};
		const uint32_t offsets[4] = {0,0,0,0};

		user_data = new uint32_t;
		int ret = drmModeAddFB2(mDRMFile, GetWidth(), GetHeight(), mFOURCC_Format,handles, strides, offsets, user_data, 0);
		if (ret)
		{
			THROW_MEANINGFUL_EXCEPTION("failed to create frame buffer " + std::string(strerror(ret)) + " " + std::string(strerror(errno)) );
		}
		gbm_bo_set_user_data(mCurrentFrontBufferObject,user_data, drm_fb_destroy_callback);
		VERBOSE_MESSAGE("JIT allocating drm frame buffer " << (*user_data));
	}
	mCurrentFrontBufferID = *user_data;
}

static void page_flip_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data)
{
	/* suppress 'unused parameter' warnings */
	(void)fd, (void)frame, (void)sec, (void)usec;
	*((bool*)data) = 0;	// Set flip flag to false
}

void PlatformInterface::SwapBuffers()
{
	eglSwapBuffers(mDisplay,mSurface);

	UpdateCurrentBuffer();

	if( mIsFirstFrame )
	{
		mIsFirstFrame = false;
		assert(mModeEncoder);
		assert(mConnector);
		assert(mModeInfo);

		int ret = drmModeSetCrtc(mDRMFile, mModeEncoder->crtc_id, mCurrentFrontBufferID, 0, 0,&mConnector->connector_id, 1, mModeInfo);
		if (ret)
		{
			THROW_MEANINGFUL_EXCEPTION("drmModeSetCrtc failed to set mode" + std::string(strerror(ret)) + " " + std::string(strerror(errno)) );
		}
	}

	// Using DRM_MODE_PAGE_FLIP_EVENT as some devices don't support DRM_MODE_PAGE_FLIP_ASYNC.
	bool waiting_for_flip = 1;
	int ret = drmModePageFlip(mDRMFile, mModeEncoder->crtc_id, mCurrentFrontBufferID,DRM_MODE_PAGE_FLIP_EVENT,&waiting_for_flip);
	if (ret)
	{
		THROW_MEANINGFUL_EXCEPTION("drmModePageFlip failed to queue page flip " + std::string(strerror(errno)) );
	}

	while (waiting_for_flip)
	{
		drmEventContext evctx =
		{
			.version = 2,
			.page_flip_handler = page_flip_handler,
		};

		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(0, &fds);
		FD_SET(mDRMFile, &fds);

		ret = select(mDRMFile + 1, &fds, NULL, NULL, NULL);
		if (ret < 0)
		{
			THROW_MEANINGFUL_EXCEPTION("PlatformInterface::SwapBuffer select on DRM file failed to queue page flip " + std::string(strerror(errno)));
		}

		drmHandleEvent(mDRMFile, &evctx);
	}

	gbm_surface_release_buffer(mNativeWindow,mCurrentFrontBufferObject);
}

#endif //#ifdef PLATFORM_DIRECT_RENDER_MANAGER

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// PLATFORM_X11_GL Implementation.
///////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef PLATFORM_X11_GL
/**
 * @brief The TinyGLES codebase is expected to be used for a system not running X11. But to aid development there is an option to 'emulate' a frame buffer with an X11 window.
 */
PlatformInterface::PlatformInterface():
	mXDisplay(NULL),
	mWindow(0),
	mWindowReady(false)
{

}

PlatformInterface::~PlatformInterface()
{
	VERBOSE_MESSAGE("Cleaning up GL");
	mWindowReady = false;

	glXMakeCurrent(mXDisplay, 0, 0 );
	glXDestroyContext(mXDisplay,mGLXContext);

	// Do this after we have set the message pump flag to false so the events generated will case XNextEvent to return.
	XFree(mVisualInfo);
	XFreeColormap(mXDisplay,mWindowAttributes.colormap);
	XDestroyWindow(mXDisplay,mWindow);
	XCloseDisplay(mXDisplay);
	mXDisplay = nullptr;
}

void PlatformInterface::InitialiseDisplay()
{
	VERBOSE_MESSAGE("Making X11 window for GLES emulation");

	mXDisplay = XOpenDisplay(NULL);
	if( mXDisplay == nullptr )
	{
		THROW_MEANINGFUL_EXCEPTION("Failed to open X display");
	}

	int glx_major, glx_minor;

	// FBConfigs were added in GLX version 1.3.
	if( glXQueryVersion(mXDisplay, &glx_major, &glx_minor) == GL_FALSE )
	{
		THROW_MEANINGFUL_EXCEPTION("Failed to fetch glx version information");
	}
	VERBOSE_MESSAGE("GLX version " << glx_major << "." << glx_minor);

	static int visual_attribs[] =
	{
		GLX_X_RENDERABLE    , True,
		GLX_DRAWABLE_TYPE   , GLX_WINDOW_BIT,
		GLX_RENDER_TYPE     , GLX_RGBA_BIT,
		GLX_X_VISUAL_TYPE   , GLX_TRUE_COLOR,
		GLX_RED_SIZE        , 8,
		GLX_GREEN_SIZE      , 8,
		GLX_BLUE_SIZE       , 8,
		GLX_ALPHA_SIZE      , 8,
		GLX_DEPTH_SIZE      , 24,
		GLX_STENCIL_SIZE    , 8,
		GLX_DOUBLEBUFFER    , True,
		//GLX_SAMPLE_BUFFERS  , 1,
		//GLX_SAMPLES         , 4,
		None
	};

	int numConfigs;
	GLXFBConfig* fbc = glXChooseFBConfig(mXDisplay, DefaultScreen(mXDisplay), visual_attribs, &numConfigs);
	if( fbc == nullptr )
	{
		THROW_MEANINGFUL_EXCEPTION("Failed to retrieve a framebuffer config");
	}

	VERBOSE_MESSAGE("Found " << numConfigs << " matching FB configs, picking first one");
	const GLXFBConfig bestFbc = fbc[0];
	XFree(fbc);

	mVisualInfo = glXGetVisualFromFBConfig( mXDisplay, bestFbc );
	VERBOSE_MESSAGE("Chosen visual ID = " << mVisualInfo->visualid );

	VERBOSE_MESSAGE("Creating colormap");
	mWindowAttributes.colormap = XCreateColormap(mXDisplay,RootWindow(mXDisplay,0),mVisualInfo->visual, AllocNone );	;
	mWindowAttributes.background_pixmap = None ;
	mWindowAttributes.border_pixel      = 0;
	mWindowAttributes.event_mask        = ExposureMask | KeyPressMask | StructureNotifyMask | PointerMotionMask | ButtonPressMask | ButtonReleaseMask ;

	mWindow = XCreateWindow(
					mXDisplay,
					RootWindow(mXDisplay, mVisualInfo->screen),
					10, 10,
					X11_EMULATION_WIDTH, X11_EMULATION_HEIGHT,
					0, mVisualInfo->depth, InputOutput, mVisualInfo->visual,
					CWBorderPixel|CWColormap|CWEventMask,
					&mWindowAttributes );
	if( !mWindow )
	{
		THROW_MEANINGFUL_EXCEPTION("Falid to create XWindow for our GL application");
	}

	XStoreName(mXDisplay, mWindow, "Tiny GLES");
	XMapWindow(mXDisplay, mWindow);

	mGLXContext = glXCreateNewContext( mXDisplay, bestFbc, GLX_RGBA_TYPE, 0, True );
	XSync(mXDisplay,False);

	VERBOSE_MESSAGE("Making context current");
	glXMakeCurrent(mXDisplay,mWindow,mGLXContext);

	// So I can exit cleanly if the user uses the close window button.
	mDeleteMessage = XInternAtom(mXDisplay, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(mXDisplay, mWindow, &mDeleteMessage, 1);

	// wait for the expose message.
  	timespec SleepTime = {0,1000000};
	while( !mWindowReady )
	{
		ProcessX11Events(nullptr);
		nanosleep(&SleepTime,NULL);
	}
}

bool PlatformInterface::ProcessX11Events(tinygles::GLES::SystemEventHandler pEventHandler)
{
	// The message pump had to be moved to the same thread as the rendering because otherwise it would fail after a little bit of time.
	// This is dispite what the documentation stated.
	if( mXDisplay == nullptr )
	{
		THROW_MEANINGFUL_EXCEPTION("The X11 display object is NULL!");
	}

	while( XPending(mXDisplay) )
	{
		XEvent e;
		XNextEvent(mXDisplay,&e);
		switch( e.type )
		{
		case Expose:
			mWindowReady = true;
			break;

		case ClientMessage:
			// All of this is to stop and error when we try to use the display but has been disconnected.
			// Snip from X11 docs.
			// 	Clients that choose not to include WM_DELETE_WINDOW in the WM_PROTOCOLS property
			// 	may be disconnected from the server if the user asks for one of the
			//  client's top-level windows to be deleted.
			// 
			// My note, could have been avoided if we just had something like XDisplayStillValid(my display)
			if (static_cast<Atom>(e.xclient.data.l[0]) == mDeleteMessage)
			{
				mWindowReady = false;
				return true;
			}
			break;

		case KeyPress:
			// exit on ESC key press
			if ( e.xkey.keycode == 0x09 )
			{
				mWindowReady = false;
				return true;
			}
			break;

		case MotionNotify:// Mouse movement
			if( pEventHandler )
			{
				SystemEventData data(SystemEventType::POINTER_MOVE);
				data.mPointer.X = e.xmotion.x;
				data.mPointer.Y = e.xmotion.y;
				pEventHandler(data);
			}
			break;

		case ButtonPress:
			if( pEventHandler )
			{
				SystemEventData data(SystemEventType::POINTER_DOWN);
				data.mPointer.X = e.xbutton.x;
				data.mPointer.Y = e.xbutton.y;
				pEventHandler(data);
			}
			break;

		case ButtonRelease:
			if( pEventHandler )
			{
				SystemEventData data(SystemEventType::POINTER_UP);
				data.mPointer.X = e.xbutton.x;
				data.mPointer.Y = e.xbutton.y;
				pEventHandler(data);
			}
			break;
		}
	}
	return false;
}

void PlatformInterface::SwapBuffers()
{
	assert( mWindowReady );
	if( mXDisplay == nullptr )
	{
		THROW_MEANINGFUL_EXCEPTION("The X11 display object is NULL!");
	}	
	glXSwapBuffers(mXDisplay,mWindow);
}

#endif //#ifdef PLATFORM_X11_GL

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pixel Font bits, packed image. Used to create a texture
///////////////////////////////////////////////////////////////////////////////////////////////////////////
const std::array<uint32_t,8192> GLES::mFont16x16Data =
{
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xffff,0xffff0000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xffff,0xffff0000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xffff,0xffff0000,0x0,0x0,0x0,0x0,0xf,0xffff0000,0xff,0xff000000,0x0,0x0,0x0,0xfff0000,0xfff,0xfff00000,
	0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xffff,0xffff0000,0x0,0x0,0x0,0x0,0x0,0xfff0000,0xff0,0xff00000,0x0,0x0,0xff,0xffff0000,0xfff,0xfff00000,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0xff,0xff000000,0xfff0,0xfff0000,0x0,0x0,0x0,0x0,0x0,0xffff0000,0xff0,0xff00000,0x0,0x0,0xff,0xfff0000,0xff,0xff000000,
	0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0xfff,0xfff00000,0xff00,0xff0000,0x0,0x0,0x0,0x0,0x0,0xffff0000,0xff,0xff000000,0x0,0x0,0xff,0xffff0000,0xff,0xff000000,0x0,0x0,0xf,0xffff0000,0xffff,0xf0000000,0xf,0xffff0000,0xffff,0xf0000000,0xf,0xf0000000,0xffff,0xffff0000,0xfff,0xfff00000,0xff00,0xff0000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0xff,0xff0000,0xfff,0xfff00000,
	0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x0,0x0,0xfff,0xfff00000,0xff00,0xff0000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xfff,0xfff00000,0x0,0x0,0xff,0xff0000,0xff,0xff000000,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x0,0x0,0xff,0xff000000,0xfff0,0xfff0000,0x0,0x0,0x0,0x0,0xff,0xf0000000,0xf,0xf0000000,0x0,0x0,0xff,0xff0000,0xff,0xff000000,
	0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xffff,0xffff0000,0x0,0x0,0x0,0x0,0xff0,0xff000000,0xf,0xf0000000,0x0,0x0,0xff,0xfff0000,0xfff,0xfff00000,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xffff,0xffff0000,0x0,0x0,0x0,0x0,0xff0,0xff000000,0xf,0xf0000000,0x0,0x0,0xfff,0xfff0000,0xfff,0xfff00000,
	0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xffff,0xffff0000,0x0,0x0,0x0,0x0,0xff,0xf0000000,0xf,0xf0000000,0x0,0x0,0xfff,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xffff,0xffff0000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xffff,0xffff0000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x0,0x0,0xf,0xf0000000,0x0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0xf,0xf0000000,0x0,0xff0000,0xff,0xff000000,0xff,0xff00000,0xff,0xfff00000,0xf,0xf0000000,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x0,0xfff0000,0xfff,0xfff00000,0xff,0xff00000,0xfff,0xfff00000,0xf,0xf0000000,0x0,0x0,0xf,0xf0000000,0xff,0xff000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0xf,0xf0000000,0x0,0xffff0000,0xf,0xf0000000,0xff,0xff00000,0xfff,0xfff00000,0xf,0xf0000000,0x0,0x0,0xf,0xf0000000,0xfff,0xfff00000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xf,0xffff0000,0xf,0xf0000000,0xff,0xff00000,0xfff,0xfff00000,0xf,0xf0000000,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0xf,0xf0000000,0xff,0xffff0000,0xf,0xf0000000,0xff,0xff00000,0xfff,0xfff00000,0xf,0xf0000000,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0xff000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xffff,0xffff0000,0xfff,0xffff0000,0xf,0xf0000000,0xff,0xff00000,0xfff,0xfff00000,0xffff,0xffff0000,0xffff,0xffff0000,0xffff,0xf0000000,0xf,0xf0000000,0xf,0xffff0000,0x0,0xff00000,0xff,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0xf,0xf0000000,0xff,0xffff0000,0xf,0xf0000000,0xff,0xff00000,0xff,0xfff00000,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xfff,0xffff0000,0xfff,0xffff0000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xf,0xffff0000,0xf,0xf0000000,0xff,0xff00000,0xf,0xfff00000,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0xff00000,0xff,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0xf,0xf0000000,0x0,0xffff0000,0xf,0xf0000000,0x0,0x0,0xf,0xfff00000,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0xff000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x0,0xfff0000,0xfff,0xfff00000,0x0,0x0,0xf,0xfff00000,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0xf,0xf0000000,0x0,0xff0000,0xff,0xff000000,0xff,0xff00000,0xf,0xfff00000,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x0,0x0,0xf,0xf0000000,0xff,0xff00000,0xf,0xfff00000,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xff0,0xff000000,0xe,0xe00000,0x7ef,0xd7000000,0x6f60,0x9400000,0x2bf,0xfb200000,0xf,0xf0000000,0x0,0x4d000000,0xd,0x40000000,0x0,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1d000000,
	0x0,0x0,0xf,0xf0000000,0xff0,0xff000000,0x3b,0x3b00000,0x6fff,0xff700000,0xe1e0,0xd000000,0xdf2,0x2fd00000,0xf,0xf0000000,0x0,0xd9000000,0x9,0xd0000000,0xba,0xd8a00000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x68000000,0x0,0x0,0xf,0xf0000000,0xee0,0xee000000,0x78,0x7800000,0xef3f,0x4fd00000,0xf0f0,0x67000000,0xef3,0x2fe00000,0xe,0xe0000000,0x3,0xf5000000,0x5,0xf3000000,0xb,0xfb000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xb3000000,
	0x0,0x0,0xd,0xd0000000,0xbb0,0xbb000000,0xfff,0xfff00000,0xdf3f,0x0,0xe1e0,0xd1000000,0x5fe,0xef500000,0xb,0xb0000000,0x9,0xf3000000,0x3,0xf9000000,0xa9,0x38a00000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xe0000000,0x0,0x0,0xb,0xb0000000,0x0,0x0,0xd0,0xd000000,0x4eff,0x81000000,0x6f63,0xa0000000,0x4ef,0xf3000000,0x0,0x0,0xc,0xf0000000,0x0,0xfc000000,0x0,0x0,0xffff,0xffff0000,0x0,0x0,0x0,0x0,0x0,0x0,0x5,0x90000000,
	0x0,0x0,0xb,0xb0000000,0x0,0x0,0x1d0,0x1d000000,0x16f,0xef500000,0x9,0x46f60000,0x5fed,0xf9071000,0x0,0x0,0xf,0xf0000000,0x0,0xff000000,0x0,0x0,0xffff,0xffff0000,0x0,0x0,0xf,0xfff00000,0x0,0x0,0x9,0x50000000,0x0,0x0,0x7,0x70000000,0x0,0x0,0xfff,0xfff00000,0xf,0xff00000,0xd,0xe1e0000,0xef31,0xefcf3000,0x0,0x0,0xf,0xf0000000,0x0,0xff000000,0x0,0x0,0xf,0xf0000000,0x0,0x0,0xf,0xfff00000,0x0,0x0,0xe,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x770,0x77000000,0xdf4f,0x2fe00000,0x67,0xf0f0000,0xff00,0x5ffe0000,0x0,0x0,0xf,0xf0000000,0x0,0xff000000,0x0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x3b,0x0,0x0,0x0,0xf,0xf0000000,0x0,0x0,0xb30,0xb3000000,0x7fff,0xff500000,0xd1,0xe1e0000,0x8f50,0x5ffe3000,0x0,0x0,0xb,0xf0000000,0x0,0xfb000000,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0xf,0xf0000000,0x86,0x0,
	0x0,0x0,0xf,0xf0000000,0x0,0x0,0xe00,0xe0000000,0x7ef,0xb2000000,0x3a0,0x6f60000,0x7df,0xfb6c9000,0x0,0x0,0x8,0xf3000000,0x3,0xf8000000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x0,0x0,0xf,0xf0000000,0xd1,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x2,0xf5000000,0x5,0xf2000000,0x0,0x0,0x0,0x0,0x2,0xd0000000,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xd8000000,0x8,0xc0000000,0x0,0x0,0x0,0x0,0xb,0x30000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x3d000000,0xd,0x30000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x5df,0xd5000000,0xa,0xf0000000,0x4df,0xe9000000,0x7df,0xe9000000,0x1,0xef000000,0xfff,0xfff00000,0x2bf,0xf8000000,0xffff,0xfff00000,0x9ef,0xe9000000,0x7ef,0xc4000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x4cf,0xfc400000,
	0x3fff,0xff300000,0x2bf,0xf0000000,0x4fff,0xff900000,0x7fff,0xffb00000,0xc,0xff000000,0x3fff,0xfff00000,0x2eff,0xff800000,0xffff,0xff900000,0x9fff,0xff900000,0x8fff,0xff300000,0x0,0x0,0x0,0x0,0x0,0x3b00000,0x0,0x0,0xb300,0x0,0x5fff,0xfff50000,0xaf90,0x9fa00000,0xfcf,0xf0000000,0xbf70,0x4ff00000,0xbf40,0x3ff00000,0x7d,0xff000000,0x6f70,0x0,0x9f90,0x3fb00000,0x0,0x7e000000,0xff30,0x3ff00000,0xff20,0x3fa00000,0xf,0xf0000000,0xf,0xf0000000,0x2,0xbff00000,0xffff,0xfff00000,0xffb2,0x0,0xef60,0x3fe0000,
	0xef00,0xfe00000,0xb0f,0xf0000000,0xbf00,0xfd00000,0x0,0x3fa00000,0x3f3,0xff000000,0x9fdf,0xe7000000,0xdf00,0x0,0x1,0xf7000000,0xdf30,0x3fd00000,0xff30,0x3fe00000,0xf,0xf0000000,0xf,0xf0000000,0x19f,0xf9300000,0xffff,0xfff00000,0x39ff,0x91000000,0x0,0x7fd0000,0xff00,0xff00000,0xf,0xf0000000,0x0,0x9f500000,0xd,0xfd000000,0xd80,0xff000000,0xbfff,0xff700000,0xfe9f,0xe8000000,0x9,0xf0000000,0x2dff,0xfd200000,0x8fff,0xfff00000,0x0,0x0,0x0,0x0,0x9fe7,0x10000000,0x0,0x0,0x18,0xef900000,0x0,0xcfc20000,
	0xff00,0xff00000,0xf,0xf0000000,0x9,0xf7000000,0x0,0x4fa00000,0x9c00,0xff000000,0xbe30,0x6fd00000,0xffff,0xff800000,0xe,0xa0000000,0x3eff,0xfe300000,0x8ef,0x9ef00000,0x0,0x0,0x0,0x0,0x9fe9,0x10000000,0xffff,0xfff00000,0x18,0xef900000,0xb,0xf9000000,0xef00,0xfe00000,0xf,0xf0000000,0xbf,0x60000000,0xbe00,0xff00000,0xffff,0xfff00000,0x0,0xff00000,0xef30,0x3ff00000,0x5f,0x60000000,0xdf30,0x3fd00000,0x0,0xfd00000,0x0,0x0,0x0,0x0,0x19f,0xf9300000,0xffff,0xfff00000,0x39ff,0x91000000,0xf,0xf0000000,
	0xaf90,0x9fa00000,0xf,0xf0000000,0xcf5,0x0,0xaf90,0x5fe00000,0xffff,0xfff00000,0xdf40,0x5fd00000,0xaf30,0x2ff00000,0xaf,0x30000000,0xff30,0x3ff00000,0xbf30,0x9f900000,0x0,0x0,0x0,0x0,0x1,0xbff00000,0x0,0x0,0xffb1,0x0,0x0,0x0,0x4fff,0xff300000,0xf,0xf0000000,0x9fff,0xfff00000,0x3fff,0xff700000,0x0,0xff000000,0x7fff,0xff400000,0x3fff,0xff800000,0xdf,0x0,0x9fff,0xff900000,0x8fff,0xfe200000,0xf,0xf0000000,0xf,0xf0000000,0x0,0x3b00000,0x0,0x0,0xb300,0x0,0xf,0xf0000000,
	0x5df,0xd5000000,0xf,0xf0000000,0xefff,0xfff00000,0x4df,0xd6000000,0x0,0xff000000,0x7ef,0xd4000000,0x4cf,0xe7000000,0xff,0x0,0x7df,0xe8000000,0x9ff,0xa2000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x2,0xd0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xb,0x30000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x16bf,0xffc81000,0xf,0xfe000000,0xffff,0xffa00000,0x9e,0xfc400000,0xffff,0xfa200000,0xfff,0xffff0000,0xfff,0xffff0000,0x7df,0xfc400000,0xff00,0xff0000,0xf,0xf0000000,0x0,0xff00000,0xff00,0x1ee2000,0xff0,0x0,0xfff50,0x5fff00,0xff50,0xff0000,0x9ef,0xe9000000,
	0x5d830,0x28d500,0x4f,0xff400000,0xffff,0xfffb0000,0xcff,0xfff40000,0xffff,0xffe10000,0xfff,0xffff0000,0xfff,0xffff0000,0xcfff,0xfff40000,0xff00,0xff0000,0xf,0xf0000000,0x0,0xff00000,0xff00,0xce20000,0xff0,0x0,0xfffa0,0xafff00,0xffd0,0xff0000,0xcfff,0xffc00000,0x5c11af,0xd5ff5e30,0x9f,0xbf900000,0xff00,0x2ff0000,0x7fc1,0xafd0000,0xff00,0x1bf90000,0xff0,0x0,0xff0,0x0,0x9fe30,0x1cfd0000,0xff00,0xff0000,0xf,0xf0000000,0x0,0xff00000,0xff00,0xce200000,0xff0,0x0,0xffbf0,0xfbff00,0xfff7,0xff0000,0x7fc20,0x2cf70000,
	0x1e10cff,0xfeff05a0,0xef,0x1fe00000,0xff00,0x3fb0000,0xcf20,0x1b30000,0xff00,0x1fd0000,0xff0,0x0,0xff0,0x0,0xdf300,0x1b30000,0xff00,0xff0000,0xf,0xf0000000,0x0,0xff00000,0xff0c,0xf4000000,0xff0,0x0,0xff5f5,0x5f5ff00,0xffde,0x10ff0000,0xdf300,0x3fd0000,0x9707fc0,0x3ffb00f0,0x4fa,0xaf30000,0xffff,0xffb10000,0xff00,0x0,0xff00,0xff0000,0xfff,0xffff0000,0xfff,0xfff00000,0xff000,0x0,0xffff,0xffff0000,0xf,0xf0000000,0x0,0xff00000,0xffcf,0xf9000000,0xff0,0x0,0xff0fa,0xaf0ff00,0xff4f,0xa0ff0000,0xff000,0xff0000,
	0xd20cf20,0xff800f0,0x8f5,0x5f80000,0xffff,0xfff80000,0xff00,0x0,0xff00,0xff0000,0xfff,0xffff0000,0xfff,0xfff00000,0xff000,0xffff0000,0xffff,0xffff0000,0xf,0xf0000000,0x0,0xff00000,0xffe2,0xdf100000,0xff0,0x0,0xff0af,0x1fa0ff00,0xff0b,0xf3ff0000,0xff000,0xff0000,0xf00ff00,0x3ff502c0,0xdff,0xfffd0000,0xff00,0x3ff0000,0xdf20,0x1b30000,0xff00,0x1fd0000,0xff0,0x0,0xff0,0x0,0xdf400,0xffff0000,0xff00,0xff0000,0xf,0xf0000000,0xff00,0xff00000,0xff50,0x5f900000,0xff0,0x0,0xff05f,0xbf50ff00,0xff01,0xfcff0000,0xdf400,0x4fd0000,
	0xf00ff30,0xcff20b40,0x2fff,0xffff2000,0xff00,0x3ff0000,0x8fc1,0xafd0000,0xff00,0x9f90000,0xff0,0x0,0xff0,0x0,0x9fe30,0x3ff0000,0xff00,0xff0000,0xf,0xf0000000,0xdf60,0x4fe00000,0xff00,0xdf30000,0xff0,0x0,0xff00f,0xff00ff00,0xff00,0x8fff0000,0x7fc30,0x3cf70000,0xb409fff,0xfff09900,0x8f60,0x6f8000,0xffff,0xfff90000,0xeff,0xfff40000,0xffff,0xffe10000,0xfff,0xffff0000,0xff0,0x0,0xcfff,0xffe60000,0xff00,0xff0000,0xf,0xf0000000,0x9fff,0xff800000,0xff00,0x4fb0000,0xfff,0xffff0000,0xff00a,0xfa00ff00,0xff00,0xeff0000,0xcfff,0xffc00000,
	0x5b00bfb,0x5efb4000,0xdf20,0x2fd000,0xffff,0xff900000,0x9f,0xfc400000,0xffff,0xfa200000,0xfff,0xffff0000,0xff0,0x0,0x7df,0xe9000000,0xff00,0xff0000,0xf,0xf0000000,0x9ef,0xd7000000,0xff00,0xbf3000,0xfff,0xffff0000,0xff005,0xf500ff00,0xff00,0x6ff0000,0x9ef,0xe9000000,0xc90000,0x5a0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0xbd630,0x15ba00,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x39cf,0xffc93000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xfff,0xffa00000,0x9ef,0xe9000000,0xffff,0xffa00000,0x9e,0xfe800000,0xffff,0xffff0000,0xff00,0xff0000,0xcf20,0x2fc000,0xdf100e,0xfe001fd0,0x3fc0,0xcf3000,0x2fc00,0xcf2000,0xffff,0xffff0000,0xf,0xfff00000,0xd1,0x0,0xff,0xff000000,0xd,0xe0000000,0x0,0x0,
	0xfff,0xfffa0000,0xcfff,0xffc00000,0xffff,0xfffa0000,0x9ff,0xfff80000,0xffff,0xffff0000,0xff00,0xff0000,0x6f80,0x8f6000,0xaf402f,0xff204fa0,0x9f6,0x6f90000,0x8f60,0x6f80000,0xffff,0xfff90000,0xf,0xfff00000,0x86,0x0,0xff,0xff000000,0x5f,0xf5000000,0x0,0x0,0xff0,0x3ff0000,0x7fc30,0x2cf70000,0xff00,0x2ff0000,0xff2,0x4fe0000,0xf,0xf0000000,0xff00,0xff0000,0x1fd0,0xdf1000,0x7f806f,0xff608f70,0xee,0x1ee00000,0xee0,0xee00000,0x0,0x3fc00000,0xf,0xf0000000,0x3b,0x0,0x0,0xff000000,0xdc,0xcd000000,0x0,0x0,
	0xff0,0x3ff0000,0xdf300,0x3fc0000,0xff00,0x3fd0000,0xef6,0x0,0xf,0xf0000000,0xff00,0xff0000,0xcf3,0x3fc0000,0x3fb0af,0x7fa0bf30,0x6f,0xff600000,0x5f9,0x9f500000,0x0,0xee200000,0xf,0xf0000000,0xe,0x0,0x0,0xff000000,0x3f6,0x6f400000,0x0,0x0,0xfff,0xfff90000,0xff000,0xff0000,0xffff,0xfff70000,0x9ff,0xff800000,0xf,0xf0000000,0xff00,0xff0000,0x5f9,0x9f50000,0xff0ef,0xfe0ff00,0xc,0xfc000000,0xbf,0xfb000000,0xa,0xf5000000,0xf,0xf0000000,0x9,0x50000000,0x0,0xff000000,0xbf1,0x1fb00000,0x0,0x0,
	0xfff,0xff900000,0xff000,0xff0000,0xffff,0xfd500000,0x8d,0xfff80000,0xf,0xf0000000,0xff00,0xff0000,0xfe,0xef00000,0xcf5fb,0xcf4fc00,0xc,0xfc000000,0x2f,0xf2000000,0x6f,0x90000000,0xf,0xf0000000,0x5,0x90000000,0x0,0xff000000,0x0,0x0,0x0,0x0,0xff0,0x0,0xdf30a,0x66fe0000,0xff02,0xef800000,0x0,0x26ff0000,0xf,0xf0000000,0xff00,0xff0000,0xaf,0x9fa00000,0x9fdf8,0x8fbf900,0x6f,0xff600000,0xf,0xf0000000,0x2fc,0x0,0xf,0xf0000000,0x0,0xe0000000,0x0,0xff000000,0x0,0x0,0x0,0x0,
	0xff0,0x0,0x8fc15,0xfff90000,0xff00,0x4ff50000,0xef4,0x2ff0000,0xf,0xf0000000,0xcf90,0x9fa0000,0x5f,0xff500000,0x5fff4,0x4fff600,0xee,0x1ee00000,0xf,0xf0000000,0xde2,0x0,0xf,0xf0000000,0x0,0xb3000000,0x0,0xff000000,0x0,0x0,0x0,0x0,0xff0,0x0,0xefff,0xffe10000,0xff00,0xafe0000,0x7ff,0xfff90000,0xf,0xf0000000,0x7fff,0xfff40000,0xe,0xfe000000,0x2fff0,0xfff200,0x9f6,0x6f90000,0xf,0xf0000000,0x9fff,0xffff0000,0xf,0xf0000000,0x0,0x68000000,0x0,0xff000000,0x0,0x0,0x0,0x0,
	0xff0,0x0,0x9ef,0xc7d30000,0xff00,0x1efa000,0x7e,0xfe900000,0xf,0xf0000000,0x6df,0xfd700000,0x9,0xf9000000,0xefc0,0xdff000,0x3fc0,0xcf3000,0xf,0xf0000000,0xffff,0xffff0000,0xf,0xf0000000,0x0,0x1d000000,0x0,0xff000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x5a0000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0xff000000,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xfff00000,0x0,0x0,0xff,0xff000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xfff00000,0x0,0x0,0xff,0xff000000,0x0,0x0,0xffff,0xffff0000,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x9f,0x30000000,0x0,0x0,0xff0,0x0,0x0,0x0,0x0,0xff0000,0x0,0x0,0x5,0xefc00000,0x0,0x0,0xff0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xff0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,
	0x9,0xb0000000,0x0,0x0,0xff0,0x0,0x0,0x0,0x0,0xff0000,0x0,0x0,0xf,0xf0000000,0x0,0x0,0xff0,0x0,0x0,0x0,0x0,0x0,0xff0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x2bf,0xfc200000,0xff5,0xee600000,0x5e,0xf9000000,0x6e,0xe5ff0000,0x5d,0xfd400000,0xff,0xfff00000,0x6e,0xd2ff0000,0xff2,0xdfb20000,0xf,0xf0000000,0xf,0xf0000000,0xff0,0xce20000,0xf,0xf0000000,0xff7ed,0x28fd2000,0xff4,0xdfc20000,0x4d,0xfd400000,
	0x0,0x0,0xcff,0xffd00000,0xfff,0xfff50000,0x4ff,0xff700000,0x5ff,0xffff0000,0x4ff,0xfff30000,0xff,0xfff00000,0x4ff,0xfeff0000,0xffe,0xfffb0000,0xf,0xf0000000,0xf,0xf0000000,0xff0,0xcf300000,0xf,0xf0000000,0xfffff,0xcfffd000,0xfff,0xfffc0000,0x4ff,0xfff40000,0x0,0x0,0x0,0x2ff00000,0xff8,0x8fc0000,0xcf3,0x2fd00000,0xcf8,0x8ff0000,0xcf3,0x3fa0000,0xf,0xf0000000,0xcf8,0x9ff0000,0xff9,0x4ff0000,0xf,0xf0000000,0xf,0xf0000000,0xffa,0xf5000000,0xf,0xf0000000,0xff61f,0xf62ff000,0xffb,0x4ff0000,0xcf9,0x9fc0000,
	0x0,0x0,0x38,0xcff00000,0xff1,0x1ff0000,0xff0,0x0,0xff1,0x1ff0000,0xfff,0xfffe0000,0xf,0xf0000000,0xff1,0x1ff0000,0xff0,0xff0000,0xf,0xf0000000,0xf,0xf0000000,0xfff,0xf3000000,0xf,0xf0000000,0xff00f,0xf00ff000,0xff1,0xff0000,0xff0,0xff0000,0x0,0x0,0x5f8,0x2ff00000,0xff0,0x1ff0000,0xff0,0x0,0xff0,0x1ff0000,0xfff,0xffff0000,0xf,0xf0000000,0xff1,0x1ff0000,0xff0,0xff0000,0xf,0xf0000000,0xf,0xf0000000,0xffd,0xfb000000,0xf,0xf0000000,0xff00f,0xf00ff000,0xff0,0xff0000,0xff0,0x1ff0000,
	0x0,0x0,0xef2,0x4ff00000,0xff7,0x8fc0000,0xcf4,0x2fe00000,0xcf9,0x9ff0000,0xcf3,0x0,0xf,0xf0000000,0xcf8,0x9ff0000,0xff0,0xff0000,0xf,0xf0000000,0xf,0xf0000000,0xff0,0xbf300000,0xf,0xf0000000,0xff00f,0xf00ff000,0xff0,0xff0000,0xcf9,0x9fc0000,0x0,0x0,0xdff,0xfef00000,0xfff,0xfff40000,0x4ff,0xff900000,0x4ff,0xffff0000,0x4ff,0xfffb0000,0xf,0xf0000000,0x7ff,0xfdff0000,0xff0,0xff0000,0xf,0xf0000000,0xf,0xf0000000,0xff0,0x3fb00000,0xf,0xf0000000,0xff00f,0xf00ff000,0xff0,0xff0000,0x4ff,0xfff30000,
	0x0,0x0,0x2df,0xb2f90000,0xff6,0xee500000,0x5e,0xf9000000,0x5e,0xe6ff0000,0x6d,0xfe900000,0xf,0xf0000000,0x6e,0xd2ff0000,0xff0,0xff0000,0xf,0xf0000000,0xf,0xf0000000,0xff0,0xbf30000,0xf,0xf0000000,0xff00f,0xf00ff000,0xff0,0xff0000,0x4d,0xfd400000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xff2,0x4fe0000,0x0,0x0,0x0,0x0,0x1f,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xbff,0xfff90000,0x0,0x0,0x0,0x0,0x9ff,0xd0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xaf,0xfe900000,0x0,0x0,0x0,0x0,0xcfd,0x30000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xfff,0xffff0000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x3,0xb0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x2,0xaff00000,0xf,0x0,0xff,0xb2000000,0x0,0x0,0xff0,0xff0000,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xc,0xfff00000,0xf,0x0,0xff,0xfc000000,0x0,0x0,0xff0,0xff0000,0xff6,0xee500000,0x6e,0xe5ff0000,0xff,0x2ed00000,0x2cf,0xfb200000,0xff,0xfff00000,0xff0,0xff0000,0x4fa0,0xaf4000,0xdf206f,0x701fd000,0x2fb0,0xbf20000,0xdf10,0x1fd00000,0xfff,0xff000000,0xf,0xf1000000,0xf,0x0,0x1,0xff000000,0x0,0x0,0xff0,0xff0000,
	0xfff,0xfff40000,0x4ff,0xffff0000,0xff,0xffa00000,0xdff,0xffc00000,0xff,0xfff00000,0xff0,0xff0000,0xdf0,0xfe0000,0x8f60af,0xa06f8000,0x8f4,0x5f700000,0x9f50,0x5f900000,0xfff,0xff000000,0xf,0xf0000000,0xf,0x0,0x0,0xff000000,0x7ee9,0x20800000,0xff0,0xff0000,0xff9,0x9fc0000,0xcf9,0x8ff0000,0xff,0x90100000,0xff1,0x0,0xf,0xf0000000,0xff0,0xff0000,0x8f6,0x6f80000,0x2fb0df,0xe0af2000,0xdd,0xdc000000,0x5fa0,0xaf500000,0x4,0xf9000000,0xf,0xf0000000,0xf,0x0,0x0,0xff000000,0xffff,0xfff00000,0xff0,0xff0000,
	0xff1,0x1ff0000,0xff1,0x1ff0000,0xff,0x10000000,0x8ff,0xc7000000,0xf,0xf0000000,0xff0,0xff0000,0x1fc,0xcf10000,0xdf1fb,0xf1fd0000,0x4f,0xf3000000,0x1fe0,0xef000000,0xe,0xc0000000,0x3f,0xa0000000,0xf,0x0,0x0,0xaf300000,0x7017,0xee600000,0xff0,0xff0000,0xff1,0x1ff0000,0xff0,0x1ff0000,0xff,0x0,0x7b,0xdfb00000,0xf,0xf0000000,0xff0,0xff0000,0xbf,0x5fc00000,0x9f9f3,0xf9f90000,0x6f,0xf6000000,0xcf5,0xfc000000,0xbe,0x20000000,0xfd,0x10000000,0xf,0x0,0x0,0x1df00000,0x0,0x0,0xff0,0xff0000,
	0xffa,0x8fc0000,0xcf9,0xaff0000,0xff,0x0,0x0,0xff00000,0xf,0xf0000000,0xff4,0x9ff0000,0x5f,0xff500000,0x4ffe0,0xdff40000,0xee,0xee000000,0x8fd,0xf8000000,0x7f5,0x0,0x3f,0xa0000000,0xf,0x0,0x0,0xaf300000,0x0,0x0,0xff0,0xff0000,0xfff,0xfff40000,0x5ff,0xffff0000,0xff,0x0,0xcff,0xffb00000,0xe,0xffa00000,0xcff,0xffff0000,0xe,0xfe000000,0xefa0,0xafe00000,0x9f5,0x6f900000,0x4ff,0xf4000000,0xfff,0xff000000,0xf,0xf0000000,0xf,0x0,0x0,0xff000000,0x0,0x0,0xff0,0xff0000,
	0xff5,0xee500000,0x7e,0xe5ff0000,0xff,0x0,0x1af,0xfb200000,0x4,0xefb00000,0x2cf,0xd5ff0000,0x9,0xf9000000,0xaf70,0x6fa00000,0x3fc0,0xcf30000,0xff,0xf0000000,0xfff,0xff000000,0xf,0xf0000000,0xf,0x0,0x0,0xff000000,0x0,0x0,0xfff,0xffff0000,0xff0,0x0,0x0,0xff0000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xcf,0xa0000000,0x0,0x0,0xf,0xf1000000,0xf,0x0,0x1,0xff000000,0x0,0x0,0x0,0x0,
	0xff0,0x0,0x0,0xff0000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xcf,0x50000000,0x0,0x0,0xc,0xfff00000,0xf,0x0,0xff,0xfc000000,0x0,0x0,0x0,0x0,0xff0,0x0,0x0,0xff0000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xfe9,0x0,0x0,0x0,0x2,0xaff00000,0xf,0x0,0xff,0xa2000000,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xb,0x7b000000,0x0,0x0,0x0,0x0,0x0,0x0,0xb,0x7b000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x3,0xf4000000,0x0,0x0,0x0,0x0,0x0,0x0,0x3,0xf4000000,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x2a,0xffb00000,0xff,0xff000000,0x0,0x0,0x0,0x5ffb0000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0x1,0xf2000000,0x6f600a,0x40000000,0x9e,0xfe800000,0x0,0x0,0x4dfd5f,0xffffff00,0xff,0xff000000,0xffff,0xffff0000,0xff,0xff000000,
	0x2ef,0xff900000,0xff,0x0,0x0,0x0,0x0,0xfff30000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xb,0x7b000000,0xe1e01d,0x0,0x9ff,0xfff80000,0x0,0x0,0x2ffffff,0xffffff00,0xff,0x0,0xffff,0xfff90000,0xff,0x0,0xaf6,0x0,0xff,0x0,0x0,0x0,0x4,0xfc000000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xffff,0xffff0000,0x0,0x0,0xf0f077,0x0,0xff2,0x4fe0000,0x0,0x0,0x9fa00af,0xf0000000,0xff,0x0,0x0,0x3fc00000,0xff,0x0,
	0xffff,0xf9000000,0xff,0x0,0x0,0x0,0xff,0xfff00000,0x0,0x0,0x0,0x0,0xfff,0xfff00000,0xffff,0xffff0000,0x0,0x0,0xe1e0d1,0x0,0xef6,0x0,0x0,0x0,0xdf1001f,0xf0000000,0xff,0x0,0x0,0xee200000,0xff,0x0,0xff0,0x0,0xff,0x0,0x0,0x0,0xff,0xfff00000,0x0,0x0,0x0,0x0,0xfff,0xfff00000,0xf,0xf0000000,0x0,0x0,0x6f64a0,0x0,0x9ff,0xff800000,0x0,0x4d000000,0xff0000f,0xffffff00,0xff,0x0,0xa,0xf5000000,0xff,0x0,
	0xffff,0xf9000000,0xff,0x0,0x0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0xa46,0xf606f600,0x8d,0xfff80000,0x0,0xe9000000,0xff0000f,0xffffff00,0xff,0x0,0x6f,0x90000000,0xff,0x0,0xdf1,0x0,0xff,0x0,0x0,0x0,0x2f,0xc0000000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x1d0e,0x1e0e1e00,0x0,0x26ff0000,0xa,0xf5000000,0xdf2002f,0xf0000000,0xff,0x0,0x2fc,0x0,0xff,0x0,
	0x7fc,0x10600000,0xff,0x0,0x0,0x0,0x6f,0x90000000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x770f,0xf0f0f00,0xef4,0x2ff0000,0xa,0xf5000000,0x9fa00af,0xf0000000,0xff,0x0,0xde2,0x0,0xff,0x0,0xcf,0xfff00000,0xff,0x0,0xf,0xf0000000,0xaf,0x50000000,0xff0,0xff000000,0xff000f,0xf000ff00,0xf,0xf0000000,0xffff,0xffff0000,0x0,0x0,0xd10e,0x1e0e1e00,0x7ff,0xfff90000,0x0,0xe9000000,0x1ffffff,0xffffff00,0xff,0x0,0x9fff,0xffff0000,0xff,0x0,
	0x9,0xffa00000,0xff,0xff000000,0xf,0xf0000000,0xdf,0x10000000,0xff0,0xff000000,0xff000f,0xf000ff00,0xf,0xf0000000,0xffff,0xffff0000,0x0,0x0,0x4a006,0xf606f600,0x7e,0xfe900000,0x0,0x4d000000,0x4cfd5f,0xffffff00,0xff,0xff000000,0xffff,0xffff0000,0xff,0xff000000,0x0,0x0,0x0,0x0,0x2,0xd0000000,0xfe,0x0,0x2d0,0x2d000000,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0xb,0x30000000,0x3ffa,0x0,0xb30,0xb3000000,0x0,0x0,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xbfe2,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf00000,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xb,0x7b000000,0x0,0x0,0x0,0x0,0x0,0x0,0xb7,0xb0000000,0x0,0x0,0xff,0xff000000,0x3,0xb0000000,0xf,0xf0000000,0x3b0,0x3b000000,0xff0,0xff000000,0x0,0x0,0x0,0x0,0x0,0x0,0x9f,0x80f00000,0xfffff0f,0xd000ef00,0x3,0xf4000000,0x0,0x0,0x0,0x0,0xff,0xff000000,0x3f,0x40000000,0x2fc0,0xcf200,
	0xff,0x0,0xd,0x20000000,0xf,0xf0000000,0xd20,0xd2000000,0xff0,0xff000000,0x0,0x0,0x0,0x0,0x0,0x0,0xf1,0x8f800000,0xf000f,0xd505df00,0x0,0x0,0x0,0x0,0x0,0x0,0xff,0x0,0x0,0x0,0x8f6,0x6f8000,0xff,0x0,0xf,0xf0000000,0x2,0xd0000000,0xff0,0xff000000,0x2d0,0x2d000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf000f,0x6d0d6f00,0x2cf,0xfb200000,0x0,0x0,0x4dfc,0x25ef8000,0xff,0x0,0xfff,0xff000000,0xee,0xee0000,
	0xff,0x0,0xf,0xf0000000,0xb,0x30000000,0xff0,0xff000000,0xb30,0xb3000000,0x5e,0xe5000000,0x0,0x0,0x0,0x0,0x0,0x0,0xf000f,0xfbf0f00,0xdff,0xffc00000,0x0,0x0,0x4ffff,0xeefff700,0xff,0x0,0xfff,0xff000000,0x5f,0x99f50000,0xff,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xef,0xfe000000,0x0,0x0,0x0,0x0,0x0,0x0,0xf000f,0x9f90f00,0xff1,0x0,0xd,0x40000000,0xcf707,0xff22fd00,0xff,0x0,0x4,0xf9000000,0xb,0xffb00000,
	0xff,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xef,0xfe000000,0xffff,0xffff0000,0xfffffff,0xfffffff0,0x0,0x0,0xf000f,0x2f20f00,0x8ff,0xc7000000,0x9,0xe0000000,0xff000,0xffffff00,0xff,0x0,0xe,0xc0000000,0x2,0xff200000,0xff,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x5e,0xe5000000,0xffff,0xffff0000,0xfffffff,0xfffffff0,0x0,0x0,0x0,0x0,0x7b,0xdfb00000,0x5,0xfa000000,0xff000,0xff000000,0xff,0x0,0xbe,0x20000000,0x0,0xff000000,
	0xff,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xff00000,0x5,0xfa000000,0xcf607,0xff63fe00,0xff,0x0,0x7f5,0x0,0x0,0xff000000,0xff,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xcff,0xffb00000,0x9,0xe0000000,0x4ffff,0xeffff700,0xff,0x0,0xfff,0xff000000,0x0,0xff000000,
	0xff,0xff000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1af,0xfb200000,0xd,0x40000000,0x4dfc,0x25df8000,0xff,0xff000000,0xfff,0xff000000,0x0,0xff000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xffff,0xffff0000,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x2c000000,0xaf,0xf9000000,0x0,0x0,0xbf30,0x3fb0000,0xf,0x0,0x2df,0xd2000000,0xf,0xf00000,0x4bf,0xfb400000,0x5e,0xf8000000,0x0,0x0,0x0,0x0,0x0,0x0,0x4bf,0xfb400000,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x96000000,0xbff,0xff900000,0x900,0x900000,0x1fb0,0xbf10000,0xf,0x0,0xdf1,0xfd000000,0x0,0x0,0x9c40,0x4c90000,0xd2,0x1f000000,0x0,0x0,0x0,0x0,0x0,0x0,0x9c40,0x4c90000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x5df,0xf4000000,0xff2,0x2fb00000,0x9fff,0xfff90000,0x8f3,0x3f800000,0xf,0x0,0xff2,0x0,0x0,0x0,0x4c02d,0xe40c4000,0x6d,0xbf000000,0x0,0x0,0xffff,0xfff00000,0x0,0x0,0x4c0ff,0xf30c4000,0x0,0x0,
	0x0,0x0,0xf,0xf0000000,0x4fff,0xff400000,0xdf1,0x0,0xf90,0x9f00000,0xeb,0xbe000000,0xf,0x0,0xafe,0x50000000,0x0,0x0,0xb40a6,0x3d04b000,0xf1,0x2f000000,0x0,0x0,0xffff,0xfff00000,0x0,0x0,0xb40f0,0x2e04b000,0x0,0x0,0x0,0x0,0x0,0x0,0xcf75,0xbfd00000,0xffff,0xff000000,0xf00,0xf00000,0xffff,0xffff0000,0xf,0x0,0x4fcf,0xf9000000,0x0,0x0,0xf00f0,0xf000,0x4d,0xdd000000,0x4d0,0x4d00000,0x0,0xff00000,0x0,0x0,0xf00f0,0x2d00f000,0x0,0x0,
	0x0,0x0,0x7,0x70000000,0xff0a,0x40000000,0xffff,0xff000000,0xf00,0xf00000,0xf,0xf0000000,0xf,0x0,0xdf05,0xff800000,0x0,0x0,0xf00f0,0xf000,0x0,0x0,0xe80,0xe800000,0x0,0xff00000,0xf,0xfff00000,0xf00ff,0xe400f000,0x0,0x0,0x0,0x0,0xa,0xa0000000,0xff0e,0x0,0xff,0x0,0xf90,0x9f00000,0xffff,0xffff0000,0x0,0x0,0xff20,0x3ff00000,0x0,0x0,0xb40b4,0x3d04b000,0x0,0x0,0xaf30,0xaf300000,0x0,0xff00000,0xf,0xfff00000,0xb40f0,0x8c04b000,0x0,0x0,
	0x0,0x0,0xb,0xb0000000,0xcf99,0x4fd00000,0x8f8,0x0,0x9fff,0xfff90000,0xf,0xf0000000,0x0,0x0,0xbfe7,0x1fd00000,0x0,0x0,0x4c02d,0xe40c4000,0x0,0x0,0xaf40,0xaf400000,0x0,0x0,0x0,0x0,0x4c0f0,0x2f0c4000,0x0,0x0,0x0,0x0,0xb,0xb0000000,0x4fff,0xff600000,0xafff,0xfff90000,0x900,0x900000,0xf,0xf0000000,0xf,0x0,0xaff,0xfc200000,0x0,0x0,0x9c40,0x4c90000,0x0,0x0,0xe80,0xe800000,0x0,0x0,0x0,0x0,0x9c40,0x4c90000,0x0,0x0,
	0x0,0x0,0xf,0xf0000000,0x5ff,0xd6000000,0x4801,0x9ee60000,0x0,0x0,0xf,0xf0000000,0xf,0x0,0x5e,0xfb000000,0x0,0x0,0x4bf,0xfb400000,0x0,0x0,0x4d0,0x4d00000,0x0,0x0,0x0,0x0,0x4bf,0xfb400000,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x2a0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0x0,0x0,0xff000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0xf,0xf0000000,0x860,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0x0,0xdf22,0xfd000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0xd10,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0x0,0x2aff,0xb2000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x5e,0xe5000000,0xf,0xf0000000,0x5e,0xf7000000,0x5e,0xe5000000,0x3,0xf9000000,0x0,0x0,0xbff,0xfff00000,0x0,0x0,0x0,0x0,0x3,0xe0000000,0x4,0xee400000,0x0,0x0,0x3e0000,0x8700000,0x3e0000,0x4a00000,0x5ee500,0x1d00000,0x0,0x0,
	0xe2,0x2e000000,0xf,0xf0000000,0xe2,0xf000000,0xc2,0x1f000000,0xb,0x90000000,0x0,0x0,0xafff,0xf000000,0x0,0x0,0x0,0x0,0xb,0xf0000000,0xd,0x23d00000,0x0,0x0,0xbf0000,0x3c000000,0xbf0000,0xd000000,0xc21f00,0xa300000,0x0,0x0,0xe2,0x2e000000,0xffff,0xffff0000,0x0,0xb5000000,0x0,0xfa000000,0x0,0x0,0xff00,0xff00000,0xffff,0xf000000,0x0,0x0,0x0,0x0,0x0,0xf0000000,0xf,0xf00000,0x0,0x0,0xf0000,0xc1000000,0xf0000,0xa3000000,0xfa00,0x3b000000,0xf,0xf0000000,
	0x5e,0xe5000000,0xffff,0xffff0000,0x2c,0x30000000,0xd2,0x1f000000,0x0,0x0,0xff00,0xff00000,0xffff,0xf000000,0x0,0x0,0x0,0x0,0x0,0xf0000000,0xd,0x23d00000,0x0,0x0,0xf0008,0x60000000,0xf0006,0x80000000,0xd21f00,0xc1000000,0xf,0xf0000000,0x0,0x0,0xf,0xf0000000,0xdf,0xff000000,0x5e,0xe6000000,0x0,0x0,0xff00,0xff00000,0x9fff,0xf000000,0xf,0xf0000000,0x0,0x0,0x0,0xf0000000,0x4,0xee400000,0xd40,0xd400000,0xf003b,0x0,0xf002b,0x0,0x5ee605,0x80000000,0x0,0x0,
	0x0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0xff00,0xff00000,0x9ef,0xf000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x8e0,0x8e00000,0xc1,0x3f0000,0xc2,0x5ef7000,0xd,0x3f0000,0xf,0xf0000000,0x0,0x0,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0xff00,0xff00000,0xf,0xf000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x4fa,0x4fa0000,0x860,0xcf0000,0x770,0xe20f000,0x76,0xcf0000,0x8f,0xd0000000,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xff70,0x7ff00000,0xf,0xf000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x3fa,0x3fa0000,0x3b00,0x94f0000,0x3b00,0xb5000,0x1d0,0x94f0000,0x8fe,0x30000000,0x0,0x0,0xffff,0xffff0000,0x0,0x0,0x0,0x0,0x0,0x0,0xffff,0xfff00000,0xf,0xf000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x8e0,0x8e00000,0xc100,0xffff000,0xc100,0x2910000,0xa30,0xffff000,0x7fd2,0x0,
	0x0,0x0,0xffff,0xffff0000,0x0,0x0,0x0,0x0,0x0,0x0,0xff7f,0x7ff00000,0xf,0xf000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xd40,0xd400000,0x84000,0xf0000,0x95000,0xdfff000,0x3b00,0xf0000,0xef10,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xff00,0x0,0xf,0xf000000,0x0,0x0,0xc,0xf8000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xef60,0x6fd0000,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xff00,0x0,0xf,0xf000000,0x0,0x0,0x0,0x2f000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x5fff,0xfff50000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xff00,0x0,0xf,0xf000000,0x0,0x0,0xff,0xd5000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x4cf,0xfc400000,
	0x9f,0x30000000,0x3,0xf9000000,0x1f,0x20000000,0x9f8,0xf000000,0x0,0x0,0x5e,0xe5000000,0x0,0x0,0x0,0x0,0x9,0xf3000000,0x0,0x3f900000,0x1,0xf2000000,0x0,0x0,0x9f,0x30000000,0x3,0xf9000000,0x1,0xf2000000,0x0,0x0,0x9,0xb0000000,0xb,0x90000000,0xb7,0xb0000000,0xf18,0xf8000000,0xf0,0xf000000,0xe2,0x2e000000,0x0,0x0,0x0,0x0,0x0,0x9b000000,0x0,0xb9000000,0xb,0x7b000000,0xf,0xf00000,0x9,0xb0000000,0xb,0x90000000,0xb,0x7b000000,0xf0,0xf000000,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xe2,0x2e000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xff,0xe0000000,0xff,0xe0000000,0xff,0xe0000000,0xff,0xe0000000,0xff,0xe0000000,0xff,0xf5000000,0x3fff,0xffffff00,0x9e,0xfc400000,0xfff,0xffff0000,0xfff,0xffff0000,0xfff,0xffff0000,0xfff,0xffff0000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,
	0x4ff,0xf4000000,0x4ff,0xf4000000,0x4ff,0xf4000000,0x4ff,0xf4000000,0x4ff,0xf4000000,0x4ff,0xf4000000,0xbf3f,0xffffff00,0xcff,0xfff40000,0xfff,0xffff0000,0xfff,0xffff0000,0xfff,0xffff0000,0xfff,0xffff0000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x9fb,0xf9000000,0x9fb,0xf9000000,0x9fb,0xf9000000,0x9fb,0xf9000000,0x9fb,0xf9000000,0x9fb,0xf9000000,0x3fb0f,0xf0000000,0x7fc1,0xafd0000,0xff0,0x0,0xff0,0x0,0xff0,0x0,0xff0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,
	0xef1,0xfe000000,0xef1,0xfe000000,0xef1,0xfe000000,0xef1,0xfe000000,0xef1,0xfe000000,0xef1,0xfe000000,0xbf30f,0xf0000000,0xcf20,0x1b30000,0xff0,0x0,0xff0,0x0,0xff0,0x0,0xff0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x4fa0,0xaf300000,0x4fa0,0xaf300000,0x4fa0,0xaf300000,0x4fa0,0xaf300000,0x4fa0,0xaf300000,0x4fa0,0xaf300000,0x3fb00f,0xffffff00,0xff00,0x0,0xfff,0xffff0000,0xfff,0xffff0000,0xfff,0xffff0000,0xfff,0xffff0000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,
	0x8f50,0x5f800000,0x8f50,0x5f800000,0x8f50,0x5f800000,0x8f50,0x5f800000,0x8f50,0x5f800000,0x8f50,0x5f800000,0xbf300f,0xffffff00,0xff00,0x0,0xfff,0xffff0000,0xfff,0xffff0000,0xfff,0xffff0000,0xfff,0xffff0000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xdfff,0xffd00000,0xdfff,0xffd00000,0xdfff,0xffd00000,0xdfff,0xffd00000,0xdfff,0xffd00000,0xdfff,0xffd00000,0x3ffffff,0xf0000000,0xdf20,0x1b30000,0xff0,0x0,0xff0,0x0,0xff0,0x0,0xff0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,
	0x2ffff,0xfff20000,0x2ffff,0xfff20000,0x2ffff,0xfff20000,0x2ffff,0xfff20000,0x2ffff,0xfff20000,0x2ffff,0xfff20000,0xbffffff,0xf0000000,0x8fc1,0xafd0000,0xff0,0x0,0xff0,0x0,0xff0,0x0,0xff0,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x8f600,0x6f80000,0x8f600,0x6f80000,0x8f600,0x6f80000,0x8f600,0x6f80000,0x8f600,0x6f80000,0x8f600,0x6f80000,0x3fb0000f,0xffffff00,0xeff,0xfff40000,0xfff,0xffff0000,0xfff,0xffff0000,0xfff,0xffff0000,0xfff,0xffff0000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,
	0xdf200,0x2fd0000,0xdf200,0x2fd0000,0xdf200,0x2fd0000,0xdf200,0x2fd0000,0xdf200,0x2fd0000,0xdf200,0x2fd0000,0xbf30000f,0xffffff00,0x9f,0xfc400000,0xfff,0xffff0000,0xfff,0xffff0000,0xfff,0xffff0000,0xfff,0xffff0000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xcf,0x80000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x2,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xffd,0x50000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x9f,0x80f00000,0x9f,0x30000000,0x3f,0x90000000,0x1f,0x20000000,0x9f8,0xf000000,0x0,0x0,0x0,0x0,0x0,0x0,0x9f,0x30000000,0x3,0xf9000000,0x1,0xf2000000,0x0,0x0,0x0,0x3f900000,0x0,0x0,0x0,0x0,0x0,0x0,0xf1,0x8f800000,0x9,0xb0000000,0xb9,0x0,0xb7,0xb0000000,0xf18,0xf8000000,0xf0,0xf000000,0x0,0x0,0x0,0x0,0x9,0xb0000000,0xb,0x90000000,0xb,0x7b000000,0xf0,0xf000000,0x0,0xb9000000,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x30000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xffff,0xfa200000,0xff50,0xff0000,0x9ef,0xe9000000,0x9ef,0xe9000000,0x9ef,0xe9000000,0x9ef,0xe9000000,0x9ef,0xe9000000,0x0,0x0,0x9df,0xfa7f5000,0xff00,0xff0000,0xff00,0xff0000,0xff00,0xff0000,0xff00,0xff0000,0x2fc0,0xcf200,0xff0,0x0,0x2cf,0xd5000000,
	0xffff,0xffe10000,0xffd0,0xff0000,0xcfff,0xffc00000,0xcfff,0xffc00000,0xcfff,0xffc00000,0xcfff,0xffc00000,0xcfff,0xffc00000,0x0,0x0,0xcfff,0xfffa0000,0xff00,0xff0000,0xff00,0xff0000,0xff00,0xff0000,0xff00,0xff0000,0x8f6,0x6f8000,0xff0,0x0,0xaff,0xfe000000,0xff00,0x1cf90000,0xfff7,0xff0000,0x7fc20,0x2cf70000,0x7fc20,0x2cf70000,0x7fc20,0x2cf70000,0x7fc20,0x2cf70000,0x7fc20,0x2cf70000,0x1900,0xb000000,0x7fc10,0x4ffa0000,0xff00,0xff0000,0xff00,0xff0000,0xff00,0xff0000,0xff00,0xff0000,0xee,0xee0000,0xfff,0xffb00000,0xff1,0xfe000000,
	0xff00,0x2fd0000,0xffde,0x10ff0000,0xdf300,0x3fd0000,0xdf300,0x3fd0000,0xdf300,0x3fd0000,0xdf300,0x3fd0000,0xdf300,0x3fd0000,0x2ec1,0xce200000,0xdf302,0xedfe0000,0xff00,0xff0000,0xff00,0xff0000,0xff00,0xff0000,0xff00,0xff0000,0x5f,0x99f50000,0xfff,0xfffa0000,0xff6,0xf7000000,0xfffff,0xff0000,0xff4f,0xa0ff0000,0xff000,0xff0000,0xff000,0xff0000,0xff000,0xff0000,0xff000,0xff0000,0xff000,0xff0000,0x2ef,0xe2000000,0xff00e,0xc0ff0000,0xff00,0xff0000,0xff00,0xff0000,0xff00,0xff0000,0xff00,0xff0000,0xb,0xffb00000,0xff0,0x2ff0000,0xffe,0xf0000000,
	0xff00,0xff0000,0xff0b,0xf3ff0000,0xff000,0xff0000,0xff000,0xff0000,0xff000,0xff0000,0xff000,0xff0000,0xff000,0xff0000,0x2ef,0xe2000000,0xff0ce,0xff0000,0xff00,0xff0000,0xff00,0xff0000,0xff00,0xff0000,0xff00,0xff0000,0x2,0xff200000,0xff0,0x2ff0000,0xff9,0xfc000000,0xff00,0x1fd0000,0xff01,0xfcff0000,0xdf400,0x4fd0000,0xdf400,0x4fd0000,0xdf400,0x4fd0000,0xdf400,0x4fd0000,0xdf400,0x4fd0000,0x2ee5,0xee200000,0xefde2,0x3fd0000,0xff00,0xff0000,0xff00,0xff0000,0xff00,0xff0000,0xff00,0xff0000,0x0,0xff000000,0xfff,0xfff90000,0xff0,0x9f800000,
	0xff00,0xaf90000,0xff00,0x8fff0000,0x7fc30,0x3cf70000,0x7fc30,0x3cf70000,0x7fc30,0x3cf70000,0x7fc30,0x3cf70000,0x7fc30,0x3cf70000,0x2d20,0x2d200000,0xbff60,0x1cf70000,0xcf90,0x9fa0000,0xcf90,0x9fa0000,0xcf90,0x9fa0000,0xcf90,0x9fa0000,0x0,0xff000000,0xfff,0xff900000,0xff0,0xff00000,0xffff,0xffe10000,0xff00,0xeff0000,0xcfff,0xffc00000,0xcfff,0xffc00000,0xcfff,0xffc00000,0xcfff,0xffc00000,0xcfff,0xffc00000,0x0,0x0,0xaffff,0xffc00000,0x7fff,0xfff40000,0x7fff,0xfff40000,0x7fff,0xfff40000,0x7fff,0xfff40000,0x0,0xff000000,0xff0,0x0,0xff5,0xffd00000,
	0xffff,0xfa200000,0xff00,0x6ff0000,0x9ef,0xe9000000,0x9ef,0xe9000000,0x9ef,0xe9000000,0x9ef,0xe9000000,0x9ef,0xe9000000,0x0,0x0,0x5f8bff,0xd8000000,0x6df,0xfd700000,0x6df,0xfd700000,0x6df,0xfd700000,0x6df,0xfd700000,0x0,0xff000000,0xff0,0x0,0xff2,0xee300000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x30000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x5e,0xe5000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xe2,0x2e000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x9f,0x30000000,0x3,0xf9000000,0x1,0xf2000000,0x9f,0x80f00000,0x0,0x0,0xe2,0x2e000000,0x0,0x0,0x0,0x0,0x9,0xf3000000,0x3,0xf9000000,0x1,0xf2000000,0x0,0x0,0x9f,0x30000000,0x3,0xf9000000,0x1,0xf2000000,0x0,0x0,0x9,0xb0000000,0xb,0x90000000,0xb,0x7b000000,0xf1,0x8f800000,0xf0,0xf000000,0x5e,0xe5000000,0x0,0x0,0x0,0x0,0x0,0x9b000000,0xb,0x90000000,0xb,0x7b000000,0xf,0xf00000,0x9,0xb0000000,0xb,0x90000000,0xb,0x7b000000,0xf0,0xf000000,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x2bf,0xfc200000,0x2bf,0xfc200000,0x2bf,0xfc200000,0x2bf,0xfc200000,0x2bf,0xfc200000,0x2bf,0xfc200000,0x9ffb,0x3dfd7000,0x5e,0xf9000000,0x5d,0xfd400000,0x5d,0xfd400000,0x5d,0xfd400000,0x5d,0xfd400000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,
	0xcff,0xffd00000,0xcff,0xffd00000,0xcff,0xffd00000,0xcff,0xffd00000,0xcff,0xffd00000,0xcff,0xffd00000,0x8ffff,0xfffff700,0x4ff,0xff700000,0x4ff,0xfff30000,0x4ff,0xfff30000,0x4ff,0xfff30000,0x4ff,0xfff30000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0x2ff00000,0x0,0x2ff00000,0x0,0x2ff00000,0x0,0x2ff00000,0x0,0x2ff00000,0x0,0x2ff00000,0xe402f,0xf403fd00,0xcf3,0x2fd00000,0xcf3,0x3fa0000,0xcf3,0x3fa0000,0xcf3,0x3fa0000,0xcf3,0x3fa0000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,
	0x38,0xcff00000,0x38,0xcff00000,0x38,0xcff00000,0x38,0xcff00000,0x38,0xcff00000,0x38,0xcff00000,0x48cf,0xffffff00,0xff0,0x0,0xfff,0xfffe0000,0xfff,0xfffe0000,0xfff,0xfffe0000,0xfff,0xfffe0000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x5f8,0x2ff00000,0x5f8,0x2ff00000,0x5f8,0x2ff00000,0x5f8,0x2ff00000,0x5f8,0x2ff00000,0x5f8,0x2ff00000,0x6f71f,0xf0000000,0xff0,0x0,0xfff,0xffff0000,0xfff,0xffff0000,0xfff,0xffff0000,0xfff,0xffff0000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,
	0xef2,0x4ff00000,0xef2,0x4ff00000,0xef2,0x4ff00000,0xef2,0x4ff00000,0xef2,0x4ff00000,0xef2,0x4ff00000,0xef24f,0xf905fc00,0xcf4,0x2fe00000,0xcf3,0x0,0xcf3,0x0,0xcf3,0x0,0xcf3,0x0,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xdff,0xfef00000,0xdff,0xfef00000,0xdff,0xfef00000,0xdff,0xfef00000,0xdff,0xfef00000,0xdff,0xfef00000,0xdfffe,0xfffff300,0x4ff,0xff900000,0x4ff,0xfffb0000,0x4ff,0xfffb0000,0x4ff,0xfffb0000,0x4ff,0xfffb0000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,
	0x2df,0xb2f90000,0x2df,0xb2f90000,0x2df,0xb2f90000,0x2df,0xb2f90000,0x2df,0xb2f90000,0x2df,0xb2f90000,0x3dfa1,0x3cfd5000,0x5e,0xf9000000,0x6d,0xfe900000,0x6d,0xfe900000,0x6d,0xfe900000,0x6d,0xfe900000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0xf,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xc,0xf8000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x2f000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xff,0xd5000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
	0x0,0x0,0x9f,0x80f00000,0x9,0xf3000000,0x3,0xf9000000,0x1,0xf2000000,0x9f,0x80f00000,0x0,0x0,0x0,0x0,0x0,0x0,0x9,0xf3000000,0x3,0xf9000000,0x1,0xf2000000,0x0,0x0,0x3,0xf9000000,0x0,0x0,0x0,0x0,0xc,0xe7600000,0xf1,0x8f800000,0x0,0x9b000000,0xb,0x90000000,0xb,0x7b000000,0xf1,0x8f800000,0xf,0xf00000,0x0,0x0,0x0,0x0,0x0,0x9b000000,0xb,0x90000000,0xb,0x7b000000,0xf,0xf00000,0xb,0x90000000,0xff0,0x0,0xf0,0xf000000,
	0x6,0xfe000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xf,0xf0000000,0x0,0x300000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xff0,0x0,0x0,0x0,0x37,0x2db00000,0xff4,0xdfc20000,0x4d,0xfd400000,0x4d,0xfd400000,0x4d,0xfd400000,0x4d,0xfd400000,0x4d,0xfd400000,0xf,0xf0000000,0x4d,0xfaf50000,0xff0,0xff0000,0xff0,0xff0000,0xff0,0xff0000,0xff0,0xff0000,0xdf10,0x1fd00000,0xff5,0xee500000,0xdf10,0x1fd00000,
	0x7e,0xfcf50000,0xfff,0xfffc0000,0x4ff,0xfff40000,0x4ff,0xfff40000,0x4ff,0xfff40000,0x4ff,0xfff40000,0x4ff,0xfff40000,0x0,0x0,0x4ff,0xffe00000,0xff0,0xff0000,0xff0,0xff0000,0xff0,0xff0000,0xff0,0xff0000,0x9f50,0x5f900000,0xfff,0xfff40000,0x9f50,0x5f900000,0x7ff,0xfffa0000,0xffb,0x4ff0000,0xcf9,0x9fc0000,0xcf9,0x9fc0000,0xcf9,0x9fc0000,0xcf9,0x9fc0000,0xcf9,0x9fc0000,0xffff,0xffff0000,0xbf6,0x5ff70000,0xff0,0xff0000,0xff0,0xff0000,0xff0,0xff0000,0xff0,0xff0000,0x5fa0,0xaf500000,0xff8,0x9fc0000,0x5fa0,0xaf500000,
	0xdf5,0x6ff0000,0xff1,0xff0000,0xff0,0xff0000,0xff0,0xff0000,0xff0,0xff0000,0xff0,0xff0000,0xff0,0xff0000,0xffff,0xffff0000,0xff0,0xe9fe0000,0xff0,0xff0000,0xff0,0xff0000,0xff0,0xff0000,0xff0,0xff0000,0x1fe0,0xef000000,0xff1,0xff0000,0x1fe0,0xef000000,0xff0,0xff0000,0xff0,0xff0000,0xff0,0x1ff0000,0xff0,0x1ff0000,0xff0,0x1ff0000,0xff0,0x1ff0000,0xff0,0x1ff0000,0x0,0x0,0xff9,0xe0ff0000,0xff0,0xff0000,0xff0,0xff0000,0xff0,0xff0000,0xff0,0xff0000,0xcf5,0xfc000000,0xff0,0xff0000,0xcf5,0xfc000000,
	0xdf5,0x5fc0000,0xff0,0xff0000,0xcf9,0x9fc0000,0xcf9,0x9fc0000,0xcf9,0x9fc0000,0xcf9,0x9fc0000,0xcf9,0x9fc0000,0xf,0xf0000000,0xcff,0x56fc0000,0xff4,0x9ff0000,0xff4,0x9ff0000,0xff4,0x9ff0000,0xff4,0x9ff0000,0x8fd,0xf8000000,0xff7,0x9fc0000,0x8fd,0xf8000000,0x6ff,0xfff40000,0xff0,0xff0000,0x4ff,0xfff30000,0x4ff,0xfff30000,0x4ff,0xfff30000,0x4ff,0xfff30000,0x4ff,0xfff30000,0xf,0xf0000000,0x7ff,0xfff40000,0xcff,0xffff0000,0xcff,0xffff0000,0xcff,0xffff0000,0xcff,0xffff0000,0x4ff,0xf4000000,0xfff,0xfff40000,0x4ff,0xf4000000,
	0x6d,0xfd500000,0xff0,0xff0000,0x4d,0xfd400000,0x4d,0xfd400000,0x4d,0xfd400000,0x4d,0xfd400000,0x4d,0xfd400000,0x0,0x0,0x5ff,0xfd500000,0x2cf,0xd5ff0000,0x2cf,0xd5ff0000,0x2cf,0xd5ff0000,0x2cf,0xd5ff0000,0xff,0xf0000000,0xff5,0xee500000,0xff,0xf0000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x30,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xcf,0xa0000000,0xff0,0x0,0xcf,0xa0000000,
	0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xcf,0x50000000,0xff0,0x0,0xcf,0x50000000,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xfe9,0x0,0xff0,0x0,0xfe9,0x0
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////
};//namespace tinygles
