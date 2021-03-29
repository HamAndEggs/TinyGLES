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

#include <signal.h>

#include "GLES2/gl2.h" //sudo apt install libgles2-mesa-dev
#include "EGL/egl.h"

#ifdef PLATFORM_BROADCOM_GLES
// All included from /opt/vc/include
	#include "bcm_host.h"
#endif


namespace tinygles{	// Using a namespace to try to prevent name clashes as my class name is kind of obvious. :)
///////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief The different type of events that the application can respond to.
 * See setSystemEventHandler function in GLES class.
 * I would like to add more custom events too like network status events. Time will tell.
 * All event processing is done at the end of the frame in the main thread.
 */
enum SystemEventType
{
	// Generic system events, like ctrl + c
	SYSTEM_EVENT_EXIT_REQUEST,	//!< User closed the window or pressed ctrl + c

	// Generic display mouse or touch events.
	SYSTEM_EVENT_POINTER_MOVE,
	SYSTEM_EVENT_POINTER_DOWN,
	SYSTEM_EVENT_POINTER_UP
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

// Forward decleration of internal type.
struct GLShader;

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

struct Vec2D
{
	float x,y;
};

struct Vec3D
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
	void Circle(int pCenterX,int pCenterY,int pRadius,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha,int pNumPoints,bool pFilled);
	inline void DrawCircle(int pCenterX,int pCenterY,int pRadius,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha = 255,int pNumPoints = 0){Circle(pCenterX,pCenterY,pRadius,pRed,pGreen,pBlue,pAlpha,pNumPoints,false);}
	inline void FillCircle(int pCenterX,int pCenterY,int pRadius,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha = 255,int pNumPoints = 0){Circle(pCenterX,pCenterY,pRadius,pRed,pGreen,pBlue,pAlpha,pNumPoints,true);}

	/**
	 * @brief Draws a rectangle with the passed in RGB values either filled or not.
	 */
	void Rectangle(int pFromX,int pFromY,int pToX,int pToY,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha,bool pFilled,uint32_t pTexture);
	inline void DrawRectangle(int pFromX,int pFromY,int pToX,int pToY,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha = 255,uint32_t pTexture = 0){Rectangle(pFromX,pFromY,pToX,pToY,pRed,pGreen,pBlue,pAlpha,false,pTexture);}
	inline void FillRectangle(int pFromX,int pFromY,int pToX,int pToY,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha = 255,uint32_t pTexture = 0){Rectangle(pFromX,pFromY,pToX,pToY,pRed,pGreen,pBlue,pAlpha,true,pTexture);}

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
	 */
	uint32_t CreateTexture(int pWidth,int pHeight,const uint8_t* pPixels,bool pHasAlpha = false,bool pGenerateMipmaps = false,bool pFiltered = false);
	inline uint32_t CreateTextureRGB(int pWidth,int pHeight,const uint8_t* pPixels){return CreateTexture(pWidth,pHeight,pPixels,false,false,false);}
	inline uint32_t CreateTextureRGBA(int pWidth,int pHeight,const uint8_t* pPixels){return CreateTexture(pWidth,pHeight,pPixels,true,false,false);}

	/**
	 * @brief Delete the texture, will throw an exception is texture not found.
	 * All textures are deleted when the GLES context is torn down so you only need to use this if you need to reclaim some memory.
	 */
	void DeleteTexture(uint32_t pTexture);

private:
	enum struct StreamIndex
	{
		VERTEX				= 0,		//!< Vertex positional data.
		TEXCOORD			= 1,		//!< Texture coordinate information.
		COLOUR				= 2,		//!< Colour type is in the format RGBA.
	};


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

	void VertexPtr(GLint pNum_coord, GLenum pType, GLsizei pStride,const void* pPointer);
	void TexCoordPtr(GLint pNum_coord, GLenum pType, GLsizei pStride,const void* pPointer);
	void ColourPtr(GLint pNum_coord, GLsizei pStride,const uint8_t* pPointer);
	void SetUserSpaceStreamPtr(StreamIndex pStream,GLint pNum_coord, GLenum pType, GLsizei pStride,const void* pPointer);

	const bool mVerbose;

	int mWidth = 0;
	int mHeight = 0;

	EGLDisplay mDisplay = nullptr;				//!<GL display
	EGLSurface mSurface = nullptr;				//!<GL rendering surface
	EGLContext mContext = nullptr;				//!<GL rendering context
	EGLConfig mConfig = nullptr;				//!<Configuration of the display.
    EGLint mMajorVersion = 0;					//!<Major version number of GLES we are running on.
	EGLint mMinorVersion = 0;					//!<Minor version number of GLES we are running on.

#ifdef PLATFORM_BROADCOM_GLES
    EGL_DISPMANX_WINDOW_T mNativeWindow;		//!<The RPi window object needed to create the render surface.
#else
	EGLNativeWindowType mNativeWindow;
#endif

	std::array<Vec2D,128> m2DWorkSpace;

	SystemEventHandler mSystemEventHandler = nullptr; //!< Where all events that we are intrested in are routed.
	bool mKeepGoing = true; //!< Set to false by the application requesting to exit or the user doing ctrl + c.

	/**
	 * @brief To make code path simpler, we always texture everything, even with white.
	 * If you want uber speed go use a different GLES engine or change this one.
	 * Our goal is minimal code paths.
	 */
	uint32_t mTextureWhite; 
	std::set<uint32_t> mTextures; //!< Our textures

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
		std::unique_ptr<GLShader> ColourOnly;
		std::unique_ptr<GLShader> TextureColour;
	}mShaders;

	struct
	{
		Matrix4x4 projection;
	}mMatrices;

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
