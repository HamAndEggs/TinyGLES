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

namespace tinygles{	// Using a namespace to try to prevent name clashes as my class name is kind of obvious. :)
///////////////////////////////////////////////////////////////////////////////////////////////////////////

GLES* GLES::Open(bool pVerbose)
{
	
}

GLES::GLES()
{
	FrameBuffer::mKeepGoing = true;

	// Lets hook ctrl + c.
	mUsersSignalAction = signal(SIGINT,CtrlHandler);

	const char* MouseDeviceName = "/dev/input/event0";
	mPointer.mDevice = open(MouseDeviceName,O_RDONLY|O_NONBLOCK);
	if( pVerbose )
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
		{
			std::clog << "Failed to open mouse device " << MouseDeviceName << "\n";
		}
	}
}

GLES::~GLES()
{

}

bool GLES::BeginFrame()
{

}

void GLES::EndFrame()
{

}

void GLES::OnApplicationExitRequest()
{
	GLES::mKeepGoing = false;
	if( GLES::mSystemEventHandler )
	{
		SystemEventData data(SYSTEM_EVENT_EXIT_REQUEST);
		GLES::mSystemEventHandler(data);
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
}

sighandler_t GLES::mUsersSignalAction = NULL;
GLES::SystemEventHandler GLES::mSystemEventHandler = nullptr;
bool GLES::mKeepGoing = false;
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

	OnApplicationExitRequest();
	std::cout << '\n'; // without this the command prompt may be at the end of the ^C char.
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
};//namespace tinygles
