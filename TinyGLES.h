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

#include <vector>
#include <string>
#include <functional>
#include <

#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>

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
	 * @brief Creates and opens a GLES object.
	 * If the OS does not support the frame buffer driver or there is some other error,
	 * this function will return NULL.
	 * For simplicity all drawing is done at eight bits per channel to an off screen bufffer.
	 * This makes the code very simple as the colour space conversion is only done when the
	 * offscreen buffer is copied to the display.
	 * 
	 * @param pVerbose get debugging information as the object is created.
	 * @return GLES* 
	 */
	static GLES* Open(bool pVerbose = false);

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
	 * @brief Sets the flag for the main loop to false and fires the SYSTEM_EVENT_EXIT_REQUEST
	 * You would typically call this from a UI button to quit the app.
	 */
	static void OnApplicationExitRequest();

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

private:

	/**
	 * @brief Construct a new Frame Buffer object
	 */
	GLES(int pFile,uint8_t* pDisplayBuffer,struct fb_fix_screeninfo pFixInfo,struct fb_var_screeninfo pScreenInfo,bool pVerbose);

	/**
	 * @brief Check for system events that the application my want.
	 */
	void ProcessSystemEvents();

	/**
	 * @brief Handle ctrl + c event.
	 * 
	 * @param SigNum 
	 */
	static void CtrlHandler(int SigNum);

	const int mWidth,mHeight;

	const bool mVerbose;
	bool mReportedPresentSpeed = false; //!< Used for verbose mode, will tell you the present screen route taken when on using linux frame buffer device.

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

	// I'm not a fan of these statics. I will try to avoid them.
	// The problem is that the OS ignels don't allow me to pass user data.
	// I don't want an internal pointer to self either.
	static SystemEventHandler mSystemEventHandler; //!< Where all events that we are intrested in are routed.
	static bool mKeepGoing; //!< Set to false by the ctrl + c handler.

	/**
	 * @brief I trap ctrl + c. Because someone may also do this I record their handler and call it when mine is.
	 * You do not need to handle ctrl + c if you use the member function GetKeepGoing to keep your rendering look going.
	 */
	static sighandler_t mUsersSignalAction;
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////
};//namespace tinygles
	
#endif //TINY_GLES_H
