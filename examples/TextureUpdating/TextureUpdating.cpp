#include "TinyGLES.h"

#include <iostream>

static uint32_t MakeStartingTexture(tinygles::GLES& GL)
{
	uint8_t pixels[16*32*4];
	uint8_t* dst = pixels;
	for( int y = 0 ; y < 16 ; y++ )
	{
		for( int x = 0 ; x < 32 ; x++ )
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
	
    return GL.CreateTexture(32,16,pixels,tinygles::TextureFormat::FORMAT_RGBA);
}

struct ABall
{
    int ballx = rand()%500;
    int bally = rand()%500;
    int vx = 1 + (rand()%10);
    int vy = 1 + (rand()%10);
    const int size = 150;
// I only fill the left half of the texture so you can see it changing and some not.
    void Update(tinygles::GLES &GL,uint32_t ballTexture)
    {
        ballx += vx;
        if( ballx > GL.GetWidth()-size)
        {
            vx = -(1+(rand()%7));
        }
        else if( ballx < 0 )
        {
            vx = (1+(rand()%7));
        }

        bally += vy;
        if( bally > GL.GetHeight()-size )
        {
            vy = -(1+(rand()%7));
        }
        else if( bally < 0 )
        {
            vy = (1+(rand()%7));
        }

        GL.FillRectangle(ballx,bally,ballx+size,bally+size/2,ballTexture);
    }
};

int main(int argc, char *argv[])
{
// Display the constants defined by app build. \n";
    std::cout << "Application Version " << APP_VERSION << '\n';
    std::cout << "Build date and time " << APP_BUILD_DATE_TIME << '\n';
    std::cout << "Build date " << APP_BUILD_DATE << '\n';
    std::cout << "Build time " << APP_BUILD_TIME << '\n';

    tinygles::GLES GL(true);

    const uint32_t text1 = MakeStartingTexture(GL);

    std::array<ABall,5> balls;

    int anim = 0;
    while( GL.BeginFrame() )
    {
        anim++;
        GL.Clear(30,60,90);

        for( auto& b : balls )
        {
            b.Update(GL,text1);
        }

        /// Now, each frame, I'll mess with it. :)
        switch(anim&3)
        {
        case 0:
            {
                const uint8_t pixels[] = {0xff,0xff,0xff,0x00,0x00,0x00,0xff,0xff,0xff,0x00,0x00,0x00};
                GL.FillTexture(text1,rand()%15,rand()%15,2,2,pixels);
            }
            break;

        case 1:
            {
                const uint8_t pixels[] = {0x0,0xff,0xff,0x00,0x00,0x00,0xff,0x0,0xff,0x00,0x00,0xff};
                GL.FillTexture(text1,rand()%15,rand()%15,2,2,pixels);
            }
            break;

        case 2:
            {
                const uint8_t pixels[] = {0xff,0xff,0x00,0x00,0xff,0x00,0xff,0xff,0xff,0xff,0x00,0x00};
                GL.FillTexture(text1,rand()%15,rand()%15,2,2,pixels);
            }
            break;

        case 3:
            {
                const uint8_t pixels[] = {0x80,0x80,0xff,0x00,0x00,0x80,0x80,0xff,0xff,0x00,0x80,0x00};
                GL.FillTexture(text1,rand()%15,rand()%15,2,2,pixels);
            }
            break;

        }

        GL.EndFrame();

    }

// And quit
    return EXIT_SUCCESS;
}
