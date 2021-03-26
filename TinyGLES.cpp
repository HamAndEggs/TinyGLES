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
#include <stdexcept>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <linux/input.h>

namespace tinygles{	// Using a namespace to try to prevent name clashes as my class name is kind of obvious. :)
///////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef DEBUG_BUILD
	#define CHECK_OGL_ERRORS()	ReadOGLErrors(__FILE__,__LINE__)
#else
	#define CHECK_OGL_ERRORS()
#endif

#define VERBOSE_MESSAGE(THE_MESSAGE__)	{if(mVerbose){std::clog << THE_MESSAGE__ << "\n";}}

GLES::GLES(bool pVerbose) :
	mVerbose(pVerbose)
{

	// Lets hook ctrl + c.
	mUsersSignalAction = signal(SIGINT,CtrlHandler);

	const char* MouseDeviceName = "/dev/input/event0";
	mPointer.mDevice = open(MouseDeviceName,O_RDONLY|O_NONBLOCK); // May fail, this is ok. They may not have one.
	if( mVerbose )
	{
		if(  mPointer.mDevice >  0 )
		{
			char name[256] = "Unknown";
			if( ioctl(mPointer.mDevice, EVIOCGNAME(sizeof(name)), name) == 0 )
			{
				std::clog << "Reading mouse from: handle = " << mPointer.mDevice << " name = " << name << "\n";
			}
			else
			{
				std::clog << "Open mouse device" << MouseDeviceName << "\n" ;
			}
		}
		else
		{// Not an error, may not have one connected. Depends on the usecase.
			std::clog << "Failed to open mouse device " << MouseDeviceName << "\n";
		}
	}

	InitialiseDisplay();
	FindGLESConfiguration();
	CreateRenderingContext();
	SetRenderingDefaults();
	CHECK_OGL_ERRORS();
}

GLES::~GLES()
{

}

bool GLES::BeginFrame()
{
	return GLES::mKeepGoing;
}

void GLES::EndFrame()
{
	ProcessSystemEvents();
}

void GLES::OnApplicationExitRequest()
{
	if( mVerbose )
	{
		std::clog << "Exit request from user, quitting application\n";
	}

	mKeepGoing = false;
	if( mSystemEventHandler != nullptr )
	{
		SystemEventData data(SYSTEM_EVENT_EXIT_REQUEST);
		mSystemEventHandler(data);
	}
}

void GLES::ProcessSystemEvents()
{
	// We don't bother to read the mouse if no pEventHandler has been registered. Would be a waste of time.
	if( mPointer.mDevice > 0 && mSystemEventHandler )
	{
		struct input_event ev;
		// Grab all messages and process befor going to next frame.
		while( read(mPointer.mDevice,&ev,sizeof(ev)) > 0 )
		{
			// EV_SYN is a seperator of events.
			if( mVerbose && ev.type != EV_ABS && ev.type != EV_KEY && ev.type != EV_SYN )
			{// Anything I missed? 
				std::cout << std::hex << ev.type << " " << ev.code << " " << ev.value << "\n";
			}

			switch( ev.type )
			{
			case EV_KEY:
				switch (ev.code)
				{
				case BTN_TOUCH:
					SystemEventData data((ev.value != 0) ? SYSTEM_EVENT_POINTER_DOWN : SYSTEM_EVENT_POINTER_UP);
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
				SystemEventData data(SYSTEM_EVENT_POINTER_MOVE);
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
		if( mVerbose )
		{
			std::clog << "CTRL trapped, quitting application\n";
		}

		mCTRL_C_Pressed = false; // So we only send once.
		OnApplicationExitRequest();
	}
}

void GLES::InitialiseDisplay()
{
	mDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if( !mDisplay )
	{
		throw std::runtime_error("Couldn\'t open the EGL default display");
	}

	//Now we have a display lets initialize it.
	if( !eglInitialize(mDisplay, &mMajorVersion, &mMinorVersion) )
	{
		throw std::runtime_error("eglInitialize() failed");
	}
	CHECK_OGL_ERRORS();

	VERBOSE_MESSAGE("GLES version " << mMajorVersion << "." << mMinorVersion);
}

void GLES::FindGLESConfiguration()
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
			EGL_RENDERABLE_TYPE,	EGL_OPENGL_ES2_BIT,
			EGL_NONE,				EGL_NONE
		};

		EGLint numConfigs;
		if( !eglChooseConfig(mDisplay,attrib_list,&mConfig,1, &numConfigs) )
		{
			throw std::runtime_error("Error: eglGetConfigs() failed");
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

	throw std::runtime_error("No matching EGL configs found");
}

void GLES::CreateRenderingContext()
{
	eglBindAPI(EGL_OPENGL_ES_API);

	//We have our display and have chosen the config so now we are ready to create the rendering context.
	EGLint ai32ContextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	mContext = eglCreateContext(mDisplay,mConfig,EGL_NO_CONTEXT,ai32ContextAttribs);
	if( !mContext )
	{
		throw std::runtime_error("Failed to get a rendering context");
	}

	mSurface = eglCreateWindowSurface(mDisplay,mConfig,0,0);

	if( mSurface == EGL_NO_SURFACE )
	{
		throw std::runtime_error("Failed to create display surfaces");
	}

	CHECK_OGL_ERRORS();
	eglMakeCurrent( mDisplay, mSurface, mSurface, mContext );
	eglQuerySurface(mDisplay, mSurface,EGL_WIDTH,  &mWidth);
	eglQuerySurface(mDisplay, mSurface,EGL_HEIGHT, &mHeight);
	CHECK_OGL_ERRORS();

	VERBOSE_MESSAGE("Display resolution is " << mWidth << "x" << mHeight );
}

void GLES::SetRenderingDefaults()
{
//	glColorMask(EGL_TRUE,EGL_TRUE,EGL_TRUE,EGL_FALSE);
	eglSwapInterval(mDisplay,1);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDepthMask(true);

	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CW);
	glCullFace(GL_BACK);

	glPixelStorei(GL_UNPACK_ALIGNMENT,1);
//	SetBlendMode(BLENDMODE_OFF);

	glDepthRangef(0.0f,1.0f);

	CHECK_OGL_ERRORS();
}

#ifdef DEBUG_BUILD
void GLES::ReadOGLErrors(const char *pSource_file_name,int pLine_number)
{
	int gl_error_code = glGetError();
	if( gl_error_code == GL_NO_ERROR )
	{
		return;
	}

//	GLOBAL_ASSERT( gl_error_code == GL_NO_ERROR );

	printf("\n**********************\nline %d file %s\n",pLine_number,pSource_file_name);
	while(gl_error_code != GL_NO_ERROR)
	{
		printf("GL error[%d]: :",gl_error_code);
		switch(gl_error_code)
		{
		default:
			printf("Unknown OGL error code\n");
			break;

		case GL_INVALID_ENUM:
			printf("An unacceptable value is specified for an enumerated argument. The offending command is ignored, having no side effect other than to set the error flag.\n");
			break;

		case GL_INVALID_VALUE:
			printf("A numeric argument is out of range. The offending command is ignored, having no side effect other than to set the error flag.\n");
			break;

		case GL_INVALID_OPERATION:
			printf("The specified operation is not allowed in the current state. The offending command is ignored, having no side effect other than to set the error flag.\n");
			break;

		case GL_OUT_OF_MEMORY:
			printf("There is not enough memory left to execute the command. The state of the GL is undefined, except for the state of the error flags, after this error is recorded.\n");
			break;
		}
		//Get next error.
		int last = gl_error_code;
		gl_error_code = glGetError();
		if( last == gl_error_code )
			break;
	}

	printf("**********************\n");
}
#endif


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
	std::cout << '\n'; // without this the command prompt may be at the end of the ^C char.
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
};//namespace tinygles
