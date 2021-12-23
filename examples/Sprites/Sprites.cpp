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
    int16_t ballx = (int16_t)(rand()%1000);
    int16_t bally = (int16_t)(rand()%500);
    int16_t vx = (int16_t)(1 + (rand()%10));
    int16_t vy = (int16_t)(1 + (rand()%10));
    float mRotation = 0.0f;
    float rotSpeed = 5.0f;
    float scale = 0.25f + (((float)rand()/(float)RAND_MAX) * 1.0f);

    void Update(tinygles::GLES &GL)
    {
        const int16_t size = (int16_t)(32.0f * scale);
        ballx += vx;
        if( ballx > GL.GetWidth()-size)
        {
            vx = -(1+(rand()%7));
        const float speed = sqrt( vx*vx + vy*vy );
            if( vy < 0 )
                rotSpeed = speed;
            else
                rotSpeed = -speed;
        }
        else if( ballx < size )
        {
            vx = (1+(rand()%7));
        const float speed = sqrt( vx*vx + vy*vy );
            if( vy < 0 )
                rotSpeed = -speed;
            else
                rotSpeed = speed;
        }

        bally += vy;
        if( bally > GL.GetHeight()-size )
        {
            vy = -(1+(rand()%7));
        const float speed = sqrt( vx*vx + vy*vy );
            if( vx < 0 )
                rotSpeed = -speed;
            else
                rotSpeed = speed;
        }
        else if( bally < size )
        {
            vy = (1+(rand()%7));
        const float speed = sqrt( vx*vx + vy*vy );
            if( vx < 0 )
                rotSpeed = speed;
            else
                rotSpeed = -speed;
        }

        mRotation += rotSpeed;
        if( mRotation > 360.0f )
            mRotation -= 360.0f;
        else if( mRotation < 0.0f )
            mRotation += 360.0f;
    }

    void Draw(tinygles::GLES &GL,uint32_t ballSprite)
    {
        GL.SetTransform2D(ballx,bally,tinygles::DegreeToRadian(mRotation),scale);
        GL.SpriteDraw(ballSprite);
    }

    void GetTransform(tinygles::QuadBatchTransform* pTransform)
    {
        pTransform->SetTransform(ballx,bally,tinygles::DegreeToRadian(mRotation),64 * scale);
    }
};

int main(int argc, char *argv[])
{
// Display the constants defined by app build. \n";
    std::cout << "Application Version " << APP_VERSION << '\n';
    std::cout << "Build date and time " << APP_BUILD_DATE_TIME << '\n';
    std::cout << "Build date " << APP_BUILD_DATE << '\n';
    std::cout << "Build time " << APP_BUILD_TIME << '\n';

    tinygles::GLES GL(tinygles::ROTATE_FRAME_LANDSCAPE);

    bool usingBatch = true;
    GL.SetSystemEventHandler([&usingBatch](auto pEvent)
    {
        if( pEvent.mType == tinygles::SystemEventType::POINTER_UPDATED )
        {
            if( pEvent.mPointer.touched )
            {
                usingBatch = !usingBatch;
            }
        }
    });

    // Load in a test texture
    uint32_t Bird_by_Magnus = LoadTexture(GL,"../data/Bird_by_Magnus.png");
    uint32_t ball = LoadTexture(GL,"../data/foot-ball2.png");
    uint32_t tree = LoadTexture(GL,"../data/tree.png");
    uint32_t Dial = LoadTexture(GL,"../data/Dial.png");
    uint32_t Needle = LoadTexture(GL,"../data/Needle.png");

    uint32_t ballSprite = GL.SpriteCreate(ball);
    uint32_t NeedleSprite = GL.SpriteCreate(Needle,16,64,8,80);

    std::array<ABall,2000> balls;

    uint32_t ballBatch = GL.QuadBatchCreate(ball,balls.size());

    int anim = 0;
    std::cout << "Starting render loop\n";
    while( GL.BeginFrame() )
    {
        anim++;
        GL.Clear(Bird_by_Magnus);
    
        for( auto& b : balls )
        {
            b.Update(GL);
        }

        if( usingBatch )
        {
            tinygles::QuadBatchTransform* trans = GL.QuadBatchGetTransform(ballBatch).data();
            for( auto& b : balls )
            {
                b.GetTransform(trans);                
                trans++;
            }
            GL.QuadBatchDraw(ballBatch);
        }
        else
        {
            for( auto& b : balls )
            {
                b.Draw(GL,ballSprite);
            }
        }

        GL.FillRectangle(0,GL.GetHeight()-400,256,GL.GetHeight(),tree);

        const int DialX = GL.GetWidth()-256;
        const int DialY = 0;
        const float a = anim;
        const float r = tinygles::DegreeToRadian(125.3f * std::sin(a * 0.01f));

        GL.Blit(Dial,DialX,DialY);

        GL.SetTransform2D(DialX+128,DialY+128,r,1.3f);
        GL.SpriteDraw(NeedleSprite);

        GL.FontPrint(0,0,"Press here to toggle batch drawing");
        if( usingBatch )
            GL.FontPrint(0,15,"Drawing with a batch");
        else
            GL.FontPrint(0,15,"Drawing one at a time");

        GL.EndFrame();

    }

    return EXIT_SUCCESS;
}
