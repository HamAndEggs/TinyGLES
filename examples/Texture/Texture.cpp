#include "TinyGLES.h"
#include "../SupportCode/TinyPNG.h"

#include <iostream>
#include <assert.h>

static uint32_t LoadTexture(tinygles::GLES &GL,const char* pFilename,bool pFiltered = false)
{
        // Load in a test texture
    uint32_t textureHandle = 0;
    tinypng::Loader png(false);
    if( png.LoadFromFile(pFilename) )
    {
        if( png.GetHasAlpha() )
        {
            std::vector<uint8_t> RGBA;
            png.GetRGBA(RGBA);
            textureHandle = GL.CreateTexture(png.GetWidth(),png.GetHeight(),RGBA.data(),tinygles::TextureFormat::FORMAT_RGBA,pFiltered);
        }
        else
        {
            std::vector<uint8_t> RGB;
            png.GetRGB(RGB);
            textureHandle = GL.CreateTexture(png.GetWidth(),png.GetHeight(),RGB.data(),tinygles::TextureFormat::FORMAT_RGB,pFiltered);
        }
    }
    assert(textureHandle);

    return textureHandle;
}

struct ABall
{
    int ballx = rand()%500;
    int bally = rand()%500;
    int vx = 1 + (rand()%10);
    int vy = 1 + (rand()%10);

    void Update(tinygles::GLES &GL,uint32_t ballTexture)
    {
        ballx += vx;
        if( ballx > GL.GetWidth()-64)
        {
            vx = -(1+(rand()%7));
        }
        else if( ballx < 0 )
        {
            vx = (1+(rand()%7));
        }

        bally += vy;
        if( bally > GL.GetHeight()-64 )
        {
            vy = -(1+(rand()%7));
        }
        else if( bally < 0 )
        {
            vy = (1+(rand()%7));
        }

        GL.Blit(ballTexture,ballx,bally);
    }
};

int main(int argc, char *argv[])
{
// Say hello to the world!
    std::cout << "Hello world, a skeleton app generated by appbuild.\n";

// Display the constants defined by app build. \n";
    std::cout << "Application Version " << APP_VERSION << '\n';
    std::cout << "Build date and time " << APP_BUILD_DATE_TIME << '\n';
    std::cout << "Build date " << APP_BUILD_DATE << '\n';
    std::cout << "Build time " << APP_BUILD_TIME << '\n';

    tinygles::GLES GL;

    // Load in a test texture
    uint32_t Bird_by_Magnus = LoadTexture(GL,"../data/Bird_by_Magnus.png");
    uint32_t create = LoadTexture(GL,"../data/crate.png");
    uint32_t plant = LoadTexture(GL,"../data/plant.png");
    uint32_t debug1 = LoadTexture(GL,"../data/debug.png");
    uint32_t debug2 = LoadTexture(GL,"../data/debug2.png",true);
    uint32_t ball = LoadTexture(GL,"../data/foot-ball.png");

    std::array<ABall,20> balls;

    int anim = 0;
    std::cout << "Starting render loop\n";
    while( GL.BeginFrame() )
    {
        anim++;

        GL.Clear(Bird_by_Magnus);

        GL.FillRoundedRectangle(50,50,550,550,100,55,20,155,100);
        GL.DrawRoundedRectangle(50,50,550,550,100,255,255,255);
    
        for( auto& b : balls )
        {
            b.Update(GL,ball);
        }

        GL.FillRectangle(100,100,200,200,create);
        GL.FillRectangle(300,100,400,200,plant);

        GL.FillRectangle(100,300,300,500,debug1);

        float sx = ((1.0f + std::sin(anim * 0.021f)) * 0.2f) + 0.9f;
        float sy = ((1.0f + std::cos(anim * 0.022f)) * 0.2f) + 0.9f;
        float dx = ((1.0f + std::sin(anim * 0.023f)) * 0.2f) + 0.9f;
        float dy = ((1.0f + std::cos(anim * 0.024f)) * 0.2f) + 0.9f;
        GL.FillRectangle((int)(350.0f * sx),(int)(250.0f * sy),(int)(550.0f * dx),(int)(450.0f * dy),debug2);


        GL.EndFrame();

    }

    return EXIT_SUCCESS;
}
