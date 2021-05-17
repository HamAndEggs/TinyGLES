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
#include <array>
#include <string_view>
#include <stdexcept>
#include <cstring>

#include <signal.h>
#include <assert.h>

/**
 * @brief The TinyGLES is targeting systems without a desktop, but sometimes we want to develop on a system with it.
 * This define allows that. But is expected to be used ONLY for development.
 * To set window draw size define X11_EMULATION_WIDTH and X11_EMULATION_HEIGHT in your build settings.
 * These below are here for default behaviour.
 * Doing this saves on params that are not needed 99% of the time. Only has an affect when building for X11 and GLES emulation.
 */
#ifdef PLATFORM_X11_GL
	#ifndef X11_EMULATION_WIDTH
		#define X11_EMULATION_WIDTH 1024
	#endif

	#ifndef X11_EMULATION_HEIGHT
		#define X11_EMULATION_HEIGHT 600
	#endif
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

constexpr float ColourToFloat(uint8_t pColour)
{
	return (float)pColour / 255.0f;
}

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

/**
 * @brief The texture formats that I expose and support. Don't want to get too silly here, these are more than enough.
 */
enum struct TextureFormat
{
	FORMAT_RGBA,
	FORMAT_RGB,
	FORMAT_ALPHA
};

struct SpriteBatchTransform
{
	// We have to have four because of how we draw quads.
	// This is because GLES 2.0 does not have a quad primitive or have instance rendering.
	// When I support more higher API's I can adjust how this works to get full speed on these systems.
	// For now, just make it work.
	inline void SetTransform(float pX,float pY,float pRotation,float pScale)
	{
		for( int n = 0 ; n < 4 ; n++ )
		{
			trans[n].x = pX;
			trans[n].y = pY;
			trans[n].r = pRotation;
			trans[n].s = pScale;
		}
	}

	struct
	{
		float x = 0.0f;
		float y = 0.0f;
		float r = 0.0f;	//!< Rotation in radians
		float s = 1.0f; //!< Scale.
	}trans[4];
};

// Forward decleration of internal types.
typedef std::shared_ptr<struct GLShader> TinyShader;
struct FreeTypeFont;
struct GLTexture;			//!< Because we can't query the values used to create a gl texture we have to store them. horrid API GLES 2.0
struct NinePatch;			//!< Internal data used to draw the nine patch objects.
struct WorkBuffers;			//!< Internal work buffers for building temporay render data.
struct PlatformInterface;	//!< Abstraction of the rendering platform we use to get the work done.
struct Sprite;				//!< The sprite object. Defined in the source code, only need a forward definition here.
struct SpriteBatch;			//!< The sprite batch object. Defined in the source code, only need a forward definition here.

///////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Temperary data on where and how a nine patch was drawn so that content can be drawn on top of it.
 */
struct NinePatchDrawInfo
{
	struct
	{
		struct
		{
			int x,y;
		}top,bottom;
	}outerRectangle,innerRectangle; // outerRectangle is it's total rendered area. innerRectangle is where you may safely draw over the nine patch. 
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////
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
	 * @brief Clears the screen using the passed texture, scaling it so it fills the screen.
	 * Takes a quick route and only clears the Zbuffer, not the RGBA buffer as that is about to be writtin to with our texture.
	 */
	void Clear(uint32_t pTexture);

	/**
	 * @brief Set the Frustum for 2D rending. This is the default mode, you only need to call it if you're mixed 3D with 2D.
	 */
	void SetFrustum2D();

	/**
	 * @brief Set the view frustum for 3D rendering
	 */
	void SetFrustum3D(float pFov, float pAspect, float pNear, float pFar);

	void SetTransform(float transform[4][4]);
	void SetTransform(float x,float y,float z);
	void SetTransformIdentity();

	void SetTransform2D(float pX,float pY,float pRotation,float pScale);

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

	/**
	 * @brief Splats the texture on the screen at it's native size, no scaling etc.
	 * Handy for when you just want to draw a texture to the display as is.
	 */
	void Blit(uint32_t pTexture,int pX,int pY,uint8_t pRed = 255,uint8_t pGreen = 255,uint8_t pBlue = 255,uint8_t pAlpha = 255);

//*******************************************
// Sprite functions

	/**
	 * @brief Create a sprite object, size and center is not related to the same size of the texture. pCX and pCY defines the center of rotation and position.
	 * The size of the sprite, in pixels.
	 * The center of the sprite, in pixels, from the top left of the quad that renders it.	 Uses a float as it allows sub pixel centers. Which can be handy.
	 * pTex* params defines the rectangle of the texture that is mapped to the sprite.
	 * @return uint32_t The handle of the sprite object.
	 */
	uint32_t SpriteCreate(uint32_t pTexture,float pWidth,float pHeight,float pCX,float pCY,int pTexFromX,int pTexFromY,int pTexToX,int pTexToY);

	/**
	 * @brief Util function, same as above but also uses the texture as the size of the sprite and centers at the center of the sprite.
	 */
	uint32_t SpriteCreate(uint32_t pTexture,float pWidth,float pHeight,float pCX,float pCY);

	/**
	 * @brief Util function, same as above but sets the UV's to use all of the texture.
	 */
	uint32_t SpriteCreate(uint32_t pTexture);

	/**
	 * @brief deletes the sprite
	 */
	void SpriteDelete(uint32_t pSprite);

	/**
	 * @brief Draws the sprite using the current transform to position, scale and rotate it. Witch is the whole point.
	 */
	void SpriteDraw(uint32_t pSprite);

	/**
	 * @brief Sets the center position of an existing sprite
	 */
	void SpriteSetCenter(uint32_t pSprite,float pCX,float pCY);

	/**
	 * @brief creates a list of the same sprite configuration that will allow many to be drawn in one function draw for much faster speed.
	 * The transform, rotation and scale is uploaded in a separate data stream. Poor mans instancing as it's not available in the lower end systems we support.
	 * Texture has to be the same for all, no way of making that different per sprite.
	 */
	uint32_t SpriteBatchCreate(uint32_t pTexture,int pCount,float pWidth,float pHeight,float pCX,float pCY,int pTexFromX,int pTexFromY,int pTexToX,int pTexToY);

	/**
	 * @brief Util function, same as above but also uses the texture as the size of the sprite and centers at the center of the sprite.
	 */
	uint32_t SpriteBatchCreate(uint32_t pTexture,int pCount,float pWidth,float pHeight,float pCX,float pCY);

	/**
	 * @brief Util function, same as above but sets the UV's to use all of the texture and sprite size to size of texture.
	 */
	uint32_t SpriteBatchCreate(uint32_t pTexture,int pCount);

	/**
	 * @brief Deletes the sprite batch.
	 */
	void SpriteBatchDelete(uint32_t pSpriteBatch);

	/**
	 * @brief Draws all the sprites, expects that all their transforms will have been set.
	 */
	void SpriteBatchDraw(uint32_t pSpriteBatch);

	/**
	 * @brief Draws the sprites within the range specified, expects that their transforms will have been set.
	 */
	void SpriteBatchDraw(uint32_t pSpriteBatch,size_t pFromIndex,size_t pToIndex);

	/**
	 * @brief Sets the transform for the sprite at index for the sprite batch.
	 */
	void SpriteBatchSetTransform(uint32_t pSpriteBatch,int pIndex,float pX,float pY,float pRotation,float pScale);

	/**
	 * @brief Because calling the above could be expensive for a lot of sprites this function allows you to do it more directly.
	 */
	std::vector<SpriteBatchTransform>& SpriteBatchGetTransform(uint32_t pSpriteBatch);

//*******************************************
// Texture functions
	/**
	 * @brief Create a Texture object with the size passed in and a given name. 
	 * pPixels is either RGB format 24bit or RGBA 32bit format is pHasAlpha is true.
	 * pPixels can be null if you're going to use FillTexture later to set the image data.
	 * But there is a GL gotcha with passing null, if you don't write to ALL the pixels the texture will not work. So if you're texture is always black you may not have filled it all.
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
	 * @brief Gets the width of the texture. Not recommended that this is called 1000's of times in a frame as it has to search a std::map for the object.
	 */
	int GetTextureWidth(uint32_t pTexture)const;


	/**
	 * @brief Gets the height of the texture. Not recommended that this is called 1000's of times in a frame as it has to search a std::map for the object.
	 */
	int GetTextureHeight(uint32_t pTexture)const;

	/**
	 * @brief Get the diagnostics texture for use to help with finding issues.
	 */
	uint32_t GetDiagnosticsTexture()const{return mDiagnostics.texture;}

	/**
	 * @brief Get the Pixel Font Texture object
	 */
	uint32_t GetPixelFontTexture()const{return mPixelFont.texture;}

//*******************************************
// 9 Patch rendering for buttons. Unity calls them 9-slicing. I'm using the Android specification as that is where most of the UI resources are.

	/**
	 * @brief Create a Nine Patch texture with embedded information about scale and fill area.
	 * Nine patch uses transparency to define areas that are and are not scaled as well as areas that are safe to be drawn on.
	 * The guides are straight, 1-pixel black lines drawn on the edge of your image that define the scaling and fill of your image.
	 * The returned as a normal texture with above rendering functions. The outer pixels are removed.
	 * When used with DrawNinePatch then will do correct scaling. If handle passed is not a nine patch object code will throw an exception.
	 * Format of pixels has to be RGBA. The outer edge pixels must be either 0x00 (no alpha) or 0xff (full alpha). If not will throw an excpetion.
	 */
	uint32_t CreateNinePatch(int pWidth,int pHeight,const uint8_t* pPixels,bool pFiltered = false);

	/**
	 * @brief Delete the nine patch, will throw an exception is texture not found.
	 */
	void DeleteNinePatch(uint32_t pNinePatch);

	/**
	 * @brief 
	 * @return const NinePatchDrawInfo& Don't hold onto this, will go away / change. Returned to help with rending of content in the fillable area of the nine patch.
	 */
	const NinePatchDrawInfo& DrawNinePatch(uint32_t pNinePatch,int pX,int pY,float pXScale,float pYScale);

//*******************************************
// Pixel font, low res, mainly for debugging.
	void FontPrint(int pX,int pY,const char* pText);
	void FontPrintf(int pX,int pY,const char* pFmt,...);

	int FontGetPrintWidth(const char* pText);
	int FontGetPrintfWidth(const char* pFmt,...);

	void FontSetScale(int pScale){assert(pScale>0);mPixelFont.scale = pScale;}
	void FontSetColour(uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha = 255){mPixelFont.R = pRed;mPixelFont.G = pGreen;mPixelFont.B = pBlue;mPixelFont.A = pAlpha;}

//*******************************************
// Free type rendering
#ifdef USE_FREETYPEFONTS

	uint32_t FontLoad(const std::string& pFontName,int pPixelHeight = 40,bool pVerbose = false);
	void FontDelete(uint32_t pFont);

	void FontPrint(uint32_t pFont,int pX,int pY,const std::string_view& pText);
	void FontPrintf(uint32_t pFont,int pX,int pY,const char* pFmt,...);

	int FontGetPrintWidth(uint32_t pFont,const std::string_view& pText);
	int FontGetPrintfWidth(uint32_t pFont,const char* pFmt,...);

	void FontSetColour(uint32_t pFont,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha = 255);
	void FontSetMaximumAllowedGlyph(int pMaxSize){mMaximumAllowedGlyph = pMaxSize;} // The default size is 128 per character. Any bigger will throw an exception, this allows you to go bigger, but kiss good by to vram. Really should do something else instead!

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
	 * @brief If the shader is already active, only it's vars are updated. Else it it is enabled. Depending on platform you want to minimise the changing of the shader used.
	 */
	void EnableShader(TinyShader pShader);

	void BuildDebugTexture();
	void BuildPixelFontTexture();
	void InitFreeTypeFont();
	void AllocateQuadBuffers();

	void VertexPtr(int pNum_coord, uint32_t pType,const void* pPointer);
	void TexCoordPtr(int pNum_coord, uint32_t pType,const void* pPointer);
	void ColourPtr(int pNum_coord,const uint8_t* pPointer);
	void SetUserSpaceStreamPtr(uint32_t pStream,int pNum_coord, uint32_t pType,const void* pPointer);

	const bool mVerbose;
	bool mKeepGoing = true;								//!< Set to false by the application requesting to exit or the user doing ctrl + c.

	int mWidth = 0;
	int mHeight = 0;

	std::unique_ptr<PlatformInterface> mPlatform;				//!< This is all the data needed to drive the rendering platform that this code sits on and used to render with.
	std::unique_ptr<WorkBuffers> mWorkBuffers;					//!< Handy set of internal work buffers used when rendering so we don't blow the stack or thrash the heap. Easy speed up.
	SystemEventHandler mSystemEventHandler = nullptr;			//!< Where all events that we are intrested in are routed.
	std::map<uint32_t,std::unique_ptr<GLTexture>> mTextures; 	//!< Our textures. I reuse the GL texture index (handle) for my own. A handy value and works well.
	std::map<uint32_t,std::unique_ptr<NinePatch>> mNinePatchs;	//!< Our nine patch data, image data is also into the textures map. I reuse the GL texture index (handle) for my own. A handy value and works well.
	NinePatchDrawInfo mNinePatchDrawInfo;						//!< Temporary buffer used to pass back rending information to the caller of the DrawNinePatch so they can draw in the safe area.

	std::map<uint32_t,std::unique_ptr<Sprite>> mSprites;		//!< Our sprites. Allows for easier rending with more functionality without functions that have a thousand paramiters.
	uint32_t mNextSpriteIndex = 1;								//!< The next sprite index to use when a sprite is allocated.

	std::map<uint32_t,std::unique_ptr<SpriteBatch>> mSpriteSpriteBatchs;	//!< Our sprite batches. Allows for easier rending with more functionality without functions that have a thousand paramiters.
	uint32_t mNextSpriteBatchIndex = 1;										//!< The next sprite batch index to use when a sprite batch is allocated.

	static const size_t mNumQuads = 2000;
	static const size_t mIndicesPerQuad = 6;
	uint32_t mQuadIndices = -1;	//!< Turns a list of a set of four quads into two triangles for rendering so they can be sepirate. (used in sprites)

	/**
	 * @brief Some data used for diagnostics/
	 */
	struct
	{
		uint32_t texture = 0; //!< A handy texture used in debugging. 16x16 check board.
		uint32_t frameNumber = 0; //!< What frame we're on. incremented in BeginFrame() So first frame will be 1
	}mDiagnostics;
	

	static const std::array<uint32_t,8192> mFont16x16Data;	//!< used to rebuild font texture.
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
		TinyShader SpriteShader;
		TinyShader SpriteBatchShader;

		TinyShader CurrentShader;
	}mShaders;

	struct
	{
		float projection[4][4];
		float transform[4][4];
	}mMatrices;

#ifdef USE_FREETYPEFONTS
	uint32_t mNextFontID = 1;
	int mMaximumAllowedGlyph = 128;
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
