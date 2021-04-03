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

#ifndef TINY_GLES_H
#define TINY_GLES_H

#include <functional>
#include <memory>
#include <cmath>
#include <set>
#include <vector>
#include <map>
#include <string_view>

#include <signal.h>
#include <assert.h>

/**
 * @brief The TinyGLES is targeting systems without a desktop, but sometimes we want to develop on a system with it.
 * This define allows that. But is expected to be used ONLY for development.
 * To set window draw size define X11_EMULATION_WIDTH and X11_EMULATION_HEIGHT in your build settings.
 * These below are here for default behaviour.
 * Doing this saves on params that are not needed 99% of the time.
 */
#ifdef PLATFORM_X11_GL
	#define GL_GLEXT_PROTOTYPES
	#include <X11/Xlib.h>
	#include <X11/Xutil.h>
	#include <GL/gl.h>
	#include <GL/glext.h>
	#include <GL/glx.h>
	#include <GL/glu.h>

	#ifndef X11_EMULATION_WIDTH
		#define X11_EMULATION_WIDTH 1024
	#endif

	#ifndef X11_EMULATION_HEIGHT
		#define X11_EMULATION_HEIGHT 600
	#endif
	/**
	 * @brief Emulate GLES with GL and X11, some defines to make the implementation cleaner, this is for development, I hate it adds loads of #ifdef's this should stop that.
	 */
	#define eglSwapBuffers(DISPLAY__,SURFACE__)			(mNativeWindow->RedrawWindow())
	#define eglDestroyContext(DISPLAY__, CONTEXT__)
	#define eglDestroySurface(DISPLAY__, SURFACE__)
	#define eglTerminate(DISPLAY__)
	#define eglSwapInterval(DISPLAY__,INTERVAL__)
	#define glColorMask(RED__,GREEN__,BLUE__,ALPHA__)

#endif

#ifdef BROADCOM_NATIVE_WINDOW // All included from /opt/vc/include
	#include "bcm_host.h"
#endif

#ifdef PLATFORM_GLES
	#include "GLES2/gl2.h"
	#include "EGL/egl.h"
#endif

/**
 * @brief The define USE_FREETYPEFONTS allows users of this lib to disable freetype support to reduce code size and dependencies.
 * Make sure you have freetype dev installed. sudo apt install libfreetype6-dev
 * Also add /usr/include/freetype2 to your build paths. The include macros need this.
 */
#ifdef USE_FREETYPEFONTS
	#include <freetype2/ft2build.h> //sudo apt install libfreetype6-dev
	#include FT_FREETYPE_H
#endif

namespace tinygles{	// Using a namespace to try to prevent name clashes as my class name is kind of obvious. :)
///////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief The different type of events that the application can respond to.
 * See setSystemEventHandler function in GLES class.
 * I would like to add more custom events too like network status events. Time will tell.
 * All event processing is done at the end of the frame in the main thread.
 */
enum struct SystemEventType
{
	// Generic system events, like ctrl + c
	EXIT_REQUEST,	//!< User closed the window or pressed ctrl + c

	// Generic display mouse or touch events.
	POINTER_MOVE,
	POINTER_DOWN,
	POINTER_UP
};

/**
 * @brief The texture formats that I expose and support. Don't want to get too silly here, these are more than enough.
 */
enum struct TextureFormat
{
	FORMAT_RGBA,
	FORMAT_RGB,
	FORMAT_ALPHA
};

enum struct StreamIndex
{
	VERTEX				= 0,		//!< Vertex positional data.
	TEXCOORD			= 1,		//!< Texture coordinate information.
	COLOUR				= 2,		//!< Colour type is in the format RGBA.
};


/**
 * @brief The data relating to a system event.
 * I've implemented some very basic events. Not going to over do it. Just passes on some common ones.
 * If you need to track the last know state of something then you'll have to do that. If I try it may not be how you expect it to work.
 * I just say when something changes.
 */
struct SystemEventData
{
	const SystemEventType mType;

	struct
	{
		int X = 0;
		int Y = 0;
	}mPointer;

	SystemEventData(SystemEventType pType) : mType(pType){}
};

// Forward decleration of internal types.
typedef std::shared_ptr<struct GLShader> TinyShader;
struct X11GLEmulation;
struct FreeTypeFont;

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// vertex buffer utility
///////////////////////////////////////////////////////////////////////////////////////////////////////////
template<typename _VertexType> struct VertexBuffer
{
	inline void Reset()
	{
		mVerts.resize(0);// I'm taking it on faith that this does nothing but set an int to zero. 
	}

	/**
	 * @brief Writes six vertices to the buffer.
	 */
	inline void BuildQuad(int pX,int pY,int pWidth,int pHeight)
	{
		mVerts.emplace_back(pX,pY);
		mVerts.emplace_back(pX + pWidth,pY);
		mVerts.emplace_back(pX + pWidth,pY + pHeight);

		mVerts.emplace_back(pX,pY);
		mVerts.emplace_back(pX + pWidth,pY + pHeight);
		mVerts.emplace_back(pX,pY + pHeight);
	}

	inline void AddUVRect(int U0,int V0,int U1,int V1)
	{
		mVerts.emplace_back(U0,V0);
		mVerts.emplace_back(U1,V0);
		mVerts.emplace_back(U1,V1);

		mVerts.emplace_back(U0,V0);
		mVerts.emplace_back(U1,V1);
		mVerts.emplace_back(U0,V1);
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

	const size_t size()const{return mVerts.size();}
	const _VertexType* data()const{return mVerts.data();}

private:
	std::vector<_VertexType> mVerts;
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////

constexpr float GetPI()
{
	return std::acos(-1);
}

constexpr float GetRadian()
{
	return 2.0f * GetPI();
}

constexpr float DegreeToRadian(float pDegree)
{
	return pDegree * (GetPI()/180.0f);
}

struct Vec2Df
{
	float x,y;
};

struct Vec2Ds
{
	Vec2Ds() = default;
	Vec2Ds(int16_t pX,int16_t pY):x(pX),y(pY){};
	int16_t x,y;
};

struct Vec3Df
{
	float x,y,z;
};


struct Matrix4x4
{
	float m[4][4];
};

/**
 * @brief Represents the linux frame buffer display.
 * Is able to deal with and abstract out the various pixel formats. 
 * For a simple 2D rendering system that's built for portablity that is an easy speed up.
 * Tiny2D goal is portablity and small code base. Not and epic SIMD / NEON / GL / DX / Volcan monster. :)
 */
class GLES
{
public:

	typedef std::function<void(const SystemEventData& pEvent)> SystemEventHandler;

	/**
	 * @brief Creates and opens a GLES object. Throws an excpetion if it fails.
	 */
	GLES(bool pVerbose);

	/**
	 * @brief Clean up code. You must delete your object on exit!
	 */
	~GLES();

	/**
	 * @brief Get the setting for Verbose debug output.
	 * 
	 * @return true 
	 * @return false 
	 */
	bool GetVerbose()const{return mVerbose;}

	/**
		@brief Returns the width of the frame buffer.
	*/
	int GetWidth()const{return mWidth;}

	/**
		@brief Returns the height of the frame buffer.
	*/
	int GetHeight()const{return mHeight;}

	/**
	 * @brief Get the Display Aspect Ratio
	 */
	float GetDisplayAspectRatio()const{return (float)mWidth/(float)mHeight;}

	/**
	 * @brief Marks the start of the frame, normally called in the while of a loop to create the render loop.
	 * @return true All is ok, so keep running.
	 * @return false Either the user requested an exit with ctrl+c or there was an error.
	 */
	bool BeginFrame();

	/**
	 * @brief Called at the end of the rendering phase. Normally last part line in the while loop.
	 */
	void EndFrame();

	/**
	 * @brief Clears the screen to the colour passed.
	 */
	void Clear(uint8_t pRed,uint8_t pGreen,uint8_t pBlue);

	/**
	 * @brief Set the Frustum for 2D rending. This is the default mode, you only need to call it if you're mixed 3D with 2D.
	 */
	void SetFrustum2D();

	/**
	 * @brief Set the view frustum for 3D rendering
	 */
	void SetFrustum3D(float pFov, float pAspect, float pNear, float pFar);

	/**
	 * @brief Sets the flag for the main loop to false and fires the SYSTEM_EVENT_EXIT_REQUEST
	 * You would typically call this from a UI button to quit the app.
	 */
	void OnApplicationExitRequest();

	/**
	 * @brief Gets the handler that is used to send event information to the application from the system.
	 */
	SystemEventHandler& GetSystemEventHandler(){return mSystemEventHandler;}

	/**
	 * @brief Sets the handler for system events
	 * 
	 * @param pOnMouseMove 
	 */
	void SetSystemEventHandler(SystemEventHandler pEventHandler){mSystemEventHandler = pEventHandler;}

//*******************************************
// Primitive draw commands.
	/**
	 * @brief Draws an arbitrary line.
	 */
	void Line(int pFromX,int pFromY,int pToX,int pToY,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha = 255);

	/**
	 * @brief Draws a circle using the pNumPoints to guide how many to use. I have set it to a nice default if < 1 -> 3 + (std::sqrt(pRadius)*3)
	 */
	void Circle(int pCenterX,int pCenterY,int pRadius,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha,size_t pNumPoints,bool pFilled);
	inline void DrawCircle(int pCenterX,int pCenterY,int pRadius,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha = 255,size_t pNumPoints = 0){Circle(pCenterX,pCenterY,pRadius,pRed,pGreen,pBlue,pAlpha,pNumPoints,false);}
	inline void FillCircle(int pCenterX,int pCenterY,int pRadius,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha = 255,size_t pNumPoints = 0){Circle(pCenterX,pCenterY,pRadius,pRed,pGreen,pBlue,pAlpha,pNumPoints,true);}

	/**
	 * @brief Draws a rectangle with the passed in RGB values either filled or not.
	 */
	void Rectangle(int pFromX,int pFromY,int pToX,int pToY,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha,bool pFilled,uint32_t pTexture);
	inline void DrawRectangle(int pFromX,int pFromY,int pToX,int pToY,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha = 255){Rectangle(pFromX,pFromY,pToX,pToY,pRed,pGreen,pBlue,pAlpha,false,0);}
	inline void FillRectangle(int pFromX,int pFromY,int pToX,int pToY,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha = 255,uint32_t pTexture = 0){Rectangle(pFromX,pFromY,pToX,pToY,pRed,pGreen,pBlue,pAlpha,true,pTexture);}
	inline void FillRectangle(int pFromX,int pFromY,int pToX,int pToY,uint32_t pTexture = 0){Rectangle(pFromX,pFromY,pToX,pToY,255,255,255,255,true,pTexture);}

	/**
	 * @brief Draws a rectangle with rounder corners in the passed in RGB values either filled or not.
	 */
	void RoundedRectangle(int pFromX,int pFromY,int pToX,int pToY,int pRadius,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha,bool pFilled);
	inline void DrawRoundedRectangle(int pFromX,int pFromY,int pToX,int pToY,int pRadius,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha = 255){RoundedRectangle(pFromX,pFromY,pToX,pToY,pRadius,pRed,pGreen,pBlue,pAlpha,false);}
	inline void FillRoundedRectangle(int pFromX,int pFromY,int pToX,int pToY,int pRadius,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha = 255){RoundedRectangle(pFromX,pFromY,pToX,pToY,pRadius,pRed,pGreen,pBlue,pAlpha,true);}

//*******************************************
// Texture commands.
	/**
	 * @brief Create a Texture object with the size passed in and a given name. 
	 * pPixels is either RGB format 24bit or RGBA 32bit format is pHasAlpha is true.
	 * pPixels can be null if you're going to use FillTexture later to set the image data.
	 */
	uint32_t CreateTexture(int pWidth,int pHeight,const uint8_t* pPixels,TextureFormat pFormat,bool pFiltered = false,bool pGenerateMipmaps = false);

	/**
	 * @brief Fill a sub rectangle, or the whole texture. Pixels is expected to be a continuous image data. So it's size is WidthxHeight of the region being updated.
	 * Pixels must be in the format that the texture was originally created with.
	 */
	void FillTexture(uint32_t pTexture,int pX,int pY,int pWidth,int pHeight,const uint8_t* pPixels,TextureFormat pFormat = TextureFormat::FORMAT_RGB,bool pGenerateMips = false);

	/**
	 * @brief Delete the texture, will throw an exception is texture not found.
	 * All textures are deleted when the GLES context is torn down so you only need to use this if you need to reclaim some memory.
	 */
	void DeleteTexture(uint32_t pTexture);

	/**
	 * @brief Get the diagnostics texture for use to help with finding issues.
	 */
	uint32_t GetDiagnosticsTexture()const{return mDiagnostics.texture;}

	/**
	 * @brief Get the Pixel Font Texture object
	 */
	uint32_t GetPixelFontTexture()const{return mPixelFont.texture;}

//*******************************************
// Pixel font, low res, mainly for debugging.
	void FontPrint(int pX,int pY,const char* pText);
	void FontPrintf(int pX,int pY,const char* pFmt,...);
	void FontSetScale(int pScale){assert(pScale>0);mPixelFont.scale = pScale;}
	void FontSetColour(uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha = 255){mPixelFont.R = pRed;mPixelFont.G = pGreen;mPixelFont.B = pBlue;mPixelFont.A = pAlpha;}

//*******************************************
// Free type rendering
#ifdef USE_FREETYPEFONTS

	uint32_t FontLoad(const std::string& pFontName,int pPixelHeight = 40,bool pVerbose = false);
	void FontDelete(uint32_t pFont);

	void FontSetColour(uint32_t pFont,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha = 255);

	void FontPrint(uint32_t pFont,int pX,int pY,const std::string_view& pText);
	void FontPrintf(uint32_t pFont,int pX,int pY,const char* pFmt,...);

#endif
//*******************************************

private:

	/**
	 * @brief Check for system events that the application my want.
	 */
	void ProcessSystemEvents();

	/**
	 * @brief Gets the display of the screen, done like this as using GLES / EGL seems to have many different ways of doing it. A bit annoying.
	 * 
	 */
	void FetchDisplayMode();

	/**
	 * @brief Gets the ball rolling by finding the initialsizeing the display.
	 */
	void InitialiseDisplay();

	/**
	 * @brief Looks for the best configuration format for the display.
	 * Throws an exception if one could not be found.
	 */
	void FindGLESConfiguration();

	/**
	 * @brief Create the rendering context
	 */
	void CreateRenderingContext();

	/**
	 * @brief Sets some common rendering states for a nice starting point.
	 */
	void SetRenderingDefaults();

	/**
	 * @brief Build the shaders that we need for basic rendering. If you need more copy the code and go multiply :)
	 */
	void BuildShaders();

	/**
	 * @brief Based on the input data it will select and enable the correct shader to use. If the shader is already enabled then will just updated it's variables.
	 * 
	 */
	void SelectAndEnableShader(uint32_t pTexture,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha);

	/**
	 * @brief If the shader is already active, only it's vars are updated. Else it it is enabled. Depending on platform you want to minimise the chaning of the shader used.
	 */
	void EnableShader(TinyShader pShader,uint32_t pTexture,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha);

	void BuildDebugTexture();
	void BuildPixelFontTexture();
	void InitFreeTypeFont();

	void VertexPtr(GLint pNum_coord, GLenum pType, GLsizei pStride,const void* pPointer);
	void TexCoordPtr(GLint pNum_coord, GLenum pType, GLsizei pStride,const void* pPointer);
	void ColourPtr(GLint pNum_coord, GLsizei pStride,const uint8_t* pPointer);
	void SetUserSpaceStreamPtr(StreamIndex pStream,GLint pNum_coord, GLenum pType, GLsizei pStride,const void* pPointer);

	const bool mVerbose;

	int mWidth = 0;
	int mHeight = 0;

#ifdef PLATFORM_GLES
	EGLDisplay mDisplay = nullptr;				//!<GL display
	EGLSurface mSurface = nullptr;				//!<GL rendering surface
	EGLContext mContext = nullptr;				//!<GL rendering context
	EGLConfig mConfig = nullptr;				//!<Configuration of the display.
    EGLint mMajorVersion = 0;					//!<Major version number of GLES we are running on.
	EGLint mMinorVersion = 0;					//!<Minor version number of GLES we are running on.
	#ifdef BROADCOM_NATIVE_WINDOW
		EGL_DISPMANX_WINDOW_T mNativeWindow;		//!<The RPi window object needed to create the render surface.
	#else 
		EGLNativeWindowType mNativeWindow;
	#endif
#endif

#ifdef PLATFORM_X11_GL
	X11GLEmulation* mNativeWindow;
#endif

	struct
	{
		std::array<Vec2Df,128> vec2Df;
		VertexBuffer<Vec2Ds> vec2Ds;
		VertexBuffer<Vec2Ds> uvShort;
		std::vector<uint8_t> zeroPixels; // Used to initialise a texture when user does not yet have all the pixels data. Please read comment in CreateTexture.
	}mWorkBuffers;

	SystemEventHandler mSystemEventHandler = nullptr; //!< Where all events that we are intrested in are routed.
	bool mKeepGoing = true; //!< Set to false by the application requesting to exit or the user doing ctrl + c.

	/**
	 * @brief Some data used for diagnostics/
	 */
	struct
	{
		uint32_t texture = 0; //!< A handy texture used in debugging. 16x16 check board.
		uint32_t frameNumber = 0; //!< What frame we're on. incremented in BeginFrame() So first frame will be 1
	}mDiagnostics;
	

	/**
	 * @brief This is a pain in the arse, because we can't query the values used to create a gl texture we have to store them. horrid API GLES 2.0
	 */
	struct GLTexture
	{
		uint32_t mHandle;
		TextureFormat mFormat;
		int mWidth;
		int mHeight;
	};
	std::map<uint32_t,GLTexture> mTextures; //!< Our textures

	static const std::vector<uint32_t> mFont16x16Data;	//!< used to rebuild font texture.
	struct
	{
		uint32_t texture = 0; //!< The texture used for rendering the pixel font.
		uint8_t R = 255,G = 255,B = 255,A = 255;
		int scale = 1;
	}mPixelFont;

	/**
	 * @brief Information about the mouse driver
	 */
	struct
	{
		int mDevice = 0; //!< File handle to /dev/input/mice

		/**
		 * @brief Maintains the current known values. Because we get many messages.
		 */
		struct
		{
			int x = 0;
			int y = 0;
		}mCurrent;
	}mPointer;

	struct
	{
		TinyShader ColourOnly;
		TinyShader TextureColour;
		TinyShader TextureAlphaOnly;

		TinyShader CurrentShader;
	}mShaders;

	struct
	{
		Matrix4x4 projection;
	}mMatrices;

#ifdef USE_FREETYPEFONTS
	uint32_t mNextFontID = 1;
	std::map<uint32_t,std::unique_ptr<FreeTypeFont>> mFreeTypeFonts;

	FT_Library mFreetype = nullptr;	

#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// Code to deal with CTRL + C
	// I'm not a fan of these statics. I will try to avoid them.
	// The problem is that the OS ignels don't allow me to pass user data.
	// I don't want an internal pointer to self either.
	/**
	 * @brief Handle ctrl + c event.
	 * 
	 * @param SigNum 
	 */
	static void CtrlHandler(int SigNum);

	/**
	 * @brief I trap ctrl + c. Because someone may also do this I record their handler and call it when mine is.
	 * You do not need to handle ctrl + c if you use the member function GetKeepGoing to keep your rendering look going.
	 */
	static sighandler_t mUsersSignalAction;
	static bool mCTRL_C_Pressed;
///////////////////////////////////////////////////////////////////////////////////////////////////////////

};


///////////////////////////////////////////////////////////////////////////////////////////////////////////
};//namespace tinygles
	
#endif //TINY_GLES_H
