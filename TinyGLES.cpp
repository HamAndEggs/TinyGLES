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

#include <math.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <linux/input.h>
#include <linux/fb.h>
#include <linux/videodev2.h>

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

#define VERBOSE_MESSAGE(THE_MESSAGE__)	{if(mVerbose){std::clog << THE_MESSAGE__ << "\n";}}

#define ATTRIB_POS 0
#define ATTRIB_COLOUR 1
#define ATTRIB_UV0 2

struct Vec3D
{
	float x,y,z;
};

struct Quad3D
{
	Vec3D v[4];

	const float* Data()const{return &v[0].x;}
};

constexpr float ColourToFloat(uint8_t pColour)
{
	return (float)pColour / 255.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// GLES Shader definition
///////////////////////////////////////////////////////////////////////////////////////////////////////////
struct GLShader
{
	GLShader(const std::string& pName,const char* pVertex, const char* pFragment,bool pVerbose);
	~GLShader();

	int GetUniformLocation(const char* pName);
	void BindAttribLocation(int location,const char* pName);
	void Enable(const Matrix4x4& projInvcam);
	void SetTransform(Matrix4x4& transform);
	void SetTransform(float x,float y,float z);
	void SetTransformIdentity();
	void SetGlobalColour(uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha);
	void SetGlobalColour(float red,float green,float blue,float alpha);
	void SetTexture(int index,GLint texture);


private:
	const std::string mName;	//!< Mainly to help debugging.
	const bool mVerbose;
	GLint mShader;
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
// GLES Implementation
///////////////////////////////////////////////////////////////////////////////////////////////////////////
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

	FetchDisplayMode();
	InitialiseDisplay();
	FindGLESConfiguration();
	CreateRenderingContext();
	SetRenderingDefaults();
	BuildShaders();

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
	eglSwapBuffers(mDisplay,mSurface);

	ProcessSystemEvents();
}

void GLES::Clear(uint8_t pRed,uint8_t pGreen,uint8_t pBlue)
{
	glClearColor((float)pRed / 255.0f,(float)pGreen / 255.0f,(float)pBlue / 255.0f,1.0f);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	CHECK_OGL_ERRORS();
}

void GLES::SetFrustum2D()
{
	VERBOSE_MESSAGE("SetFrustum2D " << mWidth << " " << mHeight);

	mMatrices.projection.m[0][0] = 2.0f / (float)mWidth;
	mMatrices.projection.m[0][1] = 0;
	mMatrices.projection.m[0][2] = 0;
	mMatrices.projection.m[0][3] = 0;

	mMatrices.projection.m[1][0] = 0;
	mMatrices.projection.m[1][1] = -2.0f / (float)mHeight;
	mMatrices.projection.m[1][2] = 0;
	mMatrices.projection.m[1][3] = 0;
		  	
	mMatrices.projection.m[2][0] = 0;
	mMatrices.projection.m[2][1] = 0;
	mMatrices.projection.m[2][2] = 0;
	mMatrices.projection.m[2][3] = 0;
		  	
	mMatrices.projection.m[3][0] = -1;
	mMatrices.projection.m[3][1] = 1;
	mMatrices.projection.m[3][2] = 0;
	mMatrices.projection.m[3][3] = 1;
}

void GLES::SetFrustum3D(float pFov, float pAspect, float pNear, float pFar)
{
	VERBOSE_MESSAGE("SetFrustum3D " << pFov << " " << pAspect << " " << pNear << " " << pFar);
	
	float cotangent = 1.0f / tanf(DegreeToRadian(pFov));
	float q = pFar / (pFar - pNear);

	mMatrices.projection.m[0][0] = cotangent;
	mMatrices.projection.m[0][1] = 0.0f;
	mMatrices.projection.m[0][2] = 0.0f;
	mMatrices.projection.m[0][3] = 0.0f;

	mMatrices.projection.m[1][0] = 0.0f;
	mMatrices.projection.m[1][1] = pAspect * cotangent;
	mMatrices.projection.m[1][2] = 0.0f;
	mMatrices.projection.m[1][3] = 0.0f;

	mMatrices.projection.m[2][0] = 0.0f;
	mMatrices.projection.m[2][1] = 0.0f;
	mMatrices.projection.m[2][2] = q;
	mMatrices.projection.m[2][3] = 1.0f;

	mMatrices.projection.m[3][0] = 0.0f;
	mMatrices.projection.m[3][1] = 0.0f;
	mMatrices.projection.m[3][2] = -q * pNear;
	mMatrices.projection.m[3][3] = 0.0f;


/*	RMError result = GI::SetFrustum(pFov,pAspect,pNear,pFar);
	if(result != RMERROR_OK)
		return result;

	shaders->Set_Projection(pFov,pAspect,pNear,pFar);

	return m_last_error;*/
}


void GLES::OnApplicationExitRequest()
{
	VERBOSE_MESSAGE("Exit request from user, quitting application");
	mKeepGoing = false;
	if( mSystemEventHandler != nullptr )
	{
		SystemEventData data(SYSTEM_EVENT_EXIT_REQUEST);
		mSystemEventHandler(data);
	}
}
	float RandF()
	{
		return ((float)rand()) / (float)RAND_MAX;
	}

//*******************************************
// Primitive draw commands.
void GLES::FillRectangle(int pFromX,int pFromY,int pToX,int pToY,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha)
{
	float quad[4][2];

	quad[0][0] = pFromX;
	quad[0][1] = pFromY;

	quad[1][0] = pToX;
	quad[1][1] = pFromY;

	quad[2][0] = pToX;
	quad[2][1] = pToY;

	quad[3][0] = pFromX;
	quad[3][1] = pToY;

	mShaders.ColourOnly->Enable(mMatrices.projection);
	mShaders.ColourOnly->SetTransformIdentity();
	mShaders.ColourOnly->SetGlobalColour(pRed,pGreen,pBlue,255);

	VertexPtr(2,GL_FLOAT,8,quad);
	return DrawArray(GL_TRIANGLE_FAN,0,4);
}

//*******************************************
// End of primitive draw commands.

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
		VERBOSE_MESSAGE("CTRL trapped, quitting application");
		mCTRL_C_Pressed = false; // So we only send once.
		OnApplicationExitRequest();
	}
}

void GLES::FetchDisplayMode()
{
	struct fb_var_screeninfo vinfo;
	{
		int File = open("/dev/fb0", O_RDWR);
		if(ioctl(File, FBIOGET_VSCREENINFO, &vinfo) ) 
		{
			throw std::runtime_error("failed to open ioctl");
		}
		close(File);
	}

	mWidth = vinfo.xres;
	mHeight = vinfo.yres;

	if( mWidth < 16 || mHeight < 16 )
	{
		throw std::runtime_error("failed to find sensible screen mode from /dev/fb0");
	}

	VERBOSE_MESSAGE("Display resolution is " << mWidth << "x" << mHeight );		
}

void GLES::InitialiseDisplay()
{
#ifdef PLATFORM_BROADCOM_GLES
	bcm_host_init();
#endif

	VERBOSE_MESSAGE("Calling eglGetDisplay");
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
	eglBindAPI(EGL_OPENGL_ES_API);
	CHECK_OGL_ERRORS();
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
			EGL_STENCIL_SIZE,		EGL_DONT_CARE,
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
	//We have our display and have chosen the config so now we are ready to create the rendering context.
	VERBOSE_MESSAGE("Creating context");
	EGLint ai32ContextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	mContext = eglCreateContext(mDisplay,mConfig,EGL_NO_CONTEXT,ai32ContextAttribs);
	if( !mContext )
	{
		throw std::runtime_error("Failed to get a rendering context");
	}

#ifdef PLATFORM_BROADCOM_GLES
	VC_RECT_T dst_rect;
	VC_RECT_T src_rect;

	dst_rect.x = 0;
	dst_rect.y = 0;
	dst_rect.width = mWidth;
	dst_rect.height = mHeight;

	src_rect.x = 0;
	src_rect.y = 0;
	src_rect.width = mWidth << 16;
	src_rect.height = mHeight << 16;        

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
	mNativeWindow.width = mWidth;
	mNativeWindow.height = mHeight;
	vc_dispmanx_update_submit_sync( dispman_update );
#endif //PLATFORM_BROADCOM_GLES

	mSurface = eglCreateWindowSurface(mDisplay,mConfig,&mNativeWindow,0);

	CHECK_OGL_ERRORS();
	eglMakeCurrent(mDisplay, mSurface, mSurface, mContext );
	eglQuerySurface(mDisplay, mSurface,EGL_WIDTH,  &mWidth);
	eglQuerySurface(mDisplay, mSurface,EGL_HEIGHT, &mHeight);
	CHECK_OGL_ERRORS();

	VERBOSE_MESSAGE("Display resolution is " << mWidth << "x" << mHeight );
}

void GLES::SetRenderingDefaults()
{
	eglSwapInterval(mDisplay,1);
	glViewport(0, 0, (GLsizei)mWidth, (GLsizei)mHeight);
	glDepthRangef(0.0f,1.0f);
	glPixelStorei(GL_UNPACK_ALIGNMENT,1);
	SetFrustum2D();

//	glColorMask(EGL_TRUE,EGL_TRUE,EGL_TRUE,EGL_FALSE);

	glDisable(GL_DEPTH_TEST);
	glDepthFunc(GL_ALWAYS);
//	glDepthMask(false);

	glDisable(GL_CULL_FACE);
//	glFrontFace(GL_CW);
//	glCullFace(GL_BACK);

	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
//	SetBlendMode(BLENDMODE_OFF);

	glEnableVertexAttribArray((int)StreamIndex::VERTEX);//Always on

	CHECK_OGL_ERRORS();
}

void GLES::BuildShaders()
{
	const char* ColourOnly_VS = R"(
		uniform mat4 u_proj_cam;
		uniform mat4 u_trans;
		uniform vec4 u_global_colour;
		attribute vec4 a_xyz;
		varying vec4 v_col;
		void main(void)
		{
			v_col = u_global_colour;
			gl_Position = u_proj_cam * (u_trans * a_xyz);
		}
	)";

	const char *ColourOnly_PS = R"(
		varying vec4 v_col;
		void main(void)
		{
			gl_FragColor = v_col;
		}
	)";

	mShaders.ColourOnly = std::make_unique<GLShader>("ColourOnly",ColourOnly_VS,ColourOnly_PS,mVerbose);
}

void GLES::VertexPtr(GLint pNum_coord, GLenum pType, GLsizei pStride,const void* pPointer)
{
	if(pNum_coord < 2 || pNum_coord > 3)
	{
		throw std::runtime_error("VertexPtr passed invalid value for pNum_coord, must be 2 or 3 got " + std::to_string(pNum_coord));
	}

	return SetUserSpaceStreamPtr(StreamIndex::VERTEX,pNum_coord,pType,pStride,pPointer);
}

void GLES::TexCoordPtr(GLint pNum_coord, GLenum pType, GLsizei pStride,const void* pPointer)
{
	return SetUserSpaceStreamPtr(StreamIndex::TEXCOORD,pNum_coord,pType,pStride,pPointer);
}

void GLES::ColourPtr(GLint pNum_coord, GLsizei pStride,const uint8_t* pPointer)
{
	if(pNum_coord < 3 || pNum_coord > 4)
	{
		throw std::runtime_error("ColourPtr passed invalid value for pNum_coord, must be 3 or 4 got " + std::to_string(pNum_coord));
	}

	return SetUserSpaceStreamPtr(StreamIndex::COLOUR,pNum_coord,GL_BYTE,pStride,pPointer);
}

void GLES::SetUserSpaceStreamPtr(StreamIndex pStream,GLint pNum_coord, GLenum pType, GLsizei pStride,const void* pPointer)
{
	if( pStride == 0 )
	{
		throw std::runtime_error("SetUserSpaceStreamPtr passed invalid value for pStride, must be > 0 ");
	}

	if( pStream != StreamIndex::VERTEX && pStream != StreamIndex::TEXCOORD && pStream != StreamIndex::COLOUR  )
	{
		throw std::runtime_error("SetUserSpaceStreamPtr passed invlaid stream index " + std::to_string((int)pStream));
	}

	//Make sure no bad code has left one of these bound.
	glBindBuffer(GL_ARRAY_BUFFER,0);

	glVertexAttribPointer(
				(GLuint)pStream,
				pNum_coord,
				pType,
				pType == GL_BYTE,
				pStride,pPointer);
}

void GLES::DrawArray(GLenum pType, GLint pStart, GLsizei pCount)
{
	//Trap some daft render setups, like no texturing but uv's still enabled.
//	GLOBAL_ASSERT( m_current_texture || m_options_enabled[ARRAY_TEXTURE_COORD] == 0 );
//	GLOBAL_ASSERT( (int)pType > -1 && pType < NUM_RMPRIM );

	glDrawArrays(pType,pStart,pCount);
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
	std::cout << '\n'; // without this the command prompt may be at the end of the ^C char.
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// GLES Shader definition
///////////////////////////////////////////////////////////////////////////////////////////////////////////
GLShader::GLShader(const std::string& pName,const char* pVertex, const char* pFragment,bool pVerbose) :
	mName(pName),
	mVerbose(pVerbose)
{
	const int vertexShader = LoadShader(GL_VERTEX_SHADER,pVertex);
	const int fragmentShader = LoadShader(GL_FRAGMENT_SHADER,pFragment);

	VERBOSE_MESSAGE("GLShader::Create: " << mName << " :vertexShader("<<vertexShader<<") fragmentShader("<<fragmentShader<<")");

	mShader = glCreateProgram(); // create empty OpenGL Program
	CHECK_OGL_ERRORS();

	glAttachShader(mShader, vertexShader); // add the vertex shader to program
	CHECK_OGL_ERRORS();

	glAttachShader(mShader, fragmentShader); // add the fragment shader to program
	CHECK_OGL_ERRORS();
	//Set the input stream numbers.
	//Has to be done before linking.
	BindAttribLocation(ATTRIB_POS, "a_xyz");
	BindAttribLocation(ATTRIB_COLOUR, "a_col");
	BindAttribLocation(ATTRIB_UV0, "a_uv0");


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
		throw std::runtime_error(error);
	}

	VERBOSE_MESSAGE("Shader: " << mName << " Compiled ok");

	//Get the bits for the variables in the shader.
	mUniforms.proj_cam = GetUniformLocation("u_proj_cam");
	mUniforms.trans = GetUniformLocation("u_trans");
	mUniforms.global_colour = GetUniformLocation("u_global_colour");
	mUniforms.tex0 = GetUniformLocation("u_tex0");
}

GLShader::~GLShader()
{

}

int GLShader::GetUniformLocation(const char* pName)
{
	int location = glGetUniformLocation(mShader,pName);
	CHECK_OGL_ERRORS();

	if( location < 0 )
	{
		VERBOSE_MESSAGE("Shader: " << mName << " Failed to find UniformLocation " << pName);
	}

	return location;

}

void GLShader::BindAttribLocation(int location,const char* pName)
{
	glBindAttribLocation(mShader, location,pName);
	CHECK_OGL_ERRORS();
	VERBOSE_MESSAGE("Shader: " << mName << " AttribLocation("<< pName << "," << location << ")");
}

void GLShader::Enable(const Matrix4x4& projInvcam)
{
    glUseProgram(mShader);
    CHECK_OGL_ERRORS();

	float ident[16]={1,0,0,0,    0,1,0,0,    0,0,1,0,    0,0,0,1};

    glUniformMatrix4fv(mUniforms.proj_cam, 1, false,(const float*)projInvcam.m);
    CHECK_OGL_ERRORS();

    if( mUniforms.tex0 > 0 )
    {
    	glActiveTexture(GL_TEXTURE0);
    	glUniform1i(mUniforms.tex0,0);
    }
}

void GLShader::SetTransform(Matrix4x4& transform)
{
	glUniformMatrix4fv(mUniforms.trans, 1, false,(const GLfloat*)transform.m);
	CHECK_OGL_ERRORS();
}

void GLShader::SetTransform(float x,float y,float z)
{
	float transOnly[16] =
	{
		1,0,0,0,
		0,1,0,0,
		0,0,1,0,
		x,y,z,1
	};

	glUniformMatrix4fv(mUniforms.trans, 1, false, transOnly);
	CHECK_OGL_ERRORS();
}

void GLShader::SetTransformIdentity()
{
	static float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
	glUniformMatrix4fv(mUniforms.trans, 1, false, identity);
	CHECK_OGL_ERRORS();
}

void GLShader::SetGlobalColour(uint8_t pRed,uint8_t pGreen,uint8_t pBlue,uint8_t pAlpha)
{
	SetGlobalColour(
		ColourToFloat(pRed),
		ColourToFloat(pGreen),
		ColourToFloat(pBlue),
		ColourToFloat(pAlpha)
	);
}

void GLShader::SetGlobalColour(float pRed,float pGreen,float pBlue,float pAlpha)
{
	glUniform4f(mUniforms.global_colour,pRed,pGreen,pBlue,pAlpha);
}

void GLShader::SetTexture(int index,GLint texture)
{
	glActiveTexture(GL_TEXTURE0 + index);
	glBindTexture(GL_TEXTURE_2D,texture);
	glUniform1i(mUniforms.tex0,0);
}

int GLShader::LoadShader(int type, const char* shaderCode)
{
	// create a vertex shader type (GLES20.GL_VERTEX_SHADER)
	// or a fragment shader type (GLES20.GL_FRAGMENT_SHADER)
	int shaderFrag = glCreateShader(type);

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
		throw std::runtime_error(error);
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////
};//namespace tinygles
