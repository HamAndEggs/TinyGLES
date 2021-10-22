
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

#include "TinyGLES.h"
#include "../SupportCode/TinyPNG.h"

static tinygles::GLES GL(tinygles::ROTATE_FRAME_LANDSCAPE);
static uint32_t gNormalFont = 0;

struct Rectangle
{
    Rectangle(int pTopX,int pTopY,int pBottomX,int pBottomY) :
        top(pTopX,pTopY),
        bottom(pBottomX,pBottomY)
    {}
    
    struct r
    {
        r(int pX,int pY):x(pX),y(pY){}
        int x,y;
    }top,bottom;

    bool ContainsPoint(int pX,int pY)const
    {
        return top.x <= pX && top.y <= pY && bottom.x >= pX && bottom.y >= pY;
    }
};

class Text
{
public:
    Text(int pX,int pY,const std::string_view& pLabel,uint8_t pRed = 255,uint8_t pGreen = 255,uint8_t pBlue = 255):
        mX(pX),
        mY(pY),
        mLabel(pLabel)
    {
        SetColour(pRed,pGreen,pBlue);
    }

    void Draw()
    {
        GL.FontSetColour(gNormalFont,mRed,mGreen,mBlue);
        GL.FontPrint(gNormalFont,mX,mY,mLabel);
    }

    void SetColour(uint8_t pRed,uint8_t pGreen,uint8_t pBlue)
    {
        mRed = pRed;
        mGreen = pGreen;
        mBlue = pBlue;
    }

private:
    int mX,mY;
    uint8_t mRed,mGreen,mBlue;
    std::string mLabel;
};

class Button
{
public:
    Button(int pX,int pY,int pWidth,int pHeight,uint8_t pRed,uint8_t pGreen,uint8_t pBlue,const std::string_view& pLabel):
        mRect(pX,pY,pX+pWidth-1,pY+pHeight-1),
        mRed(pRed),mGreen(pGreen),mBlue(pBlue),
        mLabel(
            mRect.top.x + (pWidth/2) - (GL.FontGetPrintWidth(gNormalFont,pLabel) / 2),
            mRect.top.y + ((pHeight/2) + (GL.FontGetHeight(gNormalFont)/2)),
            pLabel
        )
    {
    }

    void Draw()
    {
        if( mPressed )
        {
            if( mPressedAnim < 5 )
                mPressedAnim++;
        }
        else
        {
            if( mPressedAnim > 0 )
                mPressedAnim--;
        }

        Rectangle r = mRect;

        r.top.x -= mPressedAnim;
        r.top.y -= mPressedAnim;
        r.bottom.x += mPressedAnim;
        r.bottom.y += mPressedAnim;

        // Drop shadow.
        GL.FillRoundedRectangle(r.top.x,r.top.y+mShadowOffset,r.bottom.x,r.bottom.y+mShadowOffset,mRound,0,0,0,30);

        // The button interior.
        GL.FillRoundedRectangle(r.top.x,r.top.y,r.bottom.x,r.bottom.y,mRound,mRed,mGreen,mBlue);

        mLabel.Draw();
    }

    bool ContainsPoint(int pX,int pY)const
    {
        return mRect.ContainsPoint(pX,pY);
    }

    void SetPressed(bool pPressed){mPressed = pPressed;}

private:
    const int mRound = 17;
    const int mShadowOffset = 8;
    Rectangle mRect;
    uint8_t mRed,mGreen,mBlue;

    bool mPressed = false;
    int mPressedAnim = 0;

    Text mLabel;
};

int main(int argc, char *argv[])
{
    const std::string faceName("../data/CM Sans Serif 2012.ttf");
    gNormalFont = GL.FontLoad(faceName,40);

    std::vector<std::unique_ptr<Button>> myButtons;

    myButtons.push_back( std::make_unique<Button>(100,100,220,80,33,150,243,"Log out") );
    myButtons.push_back( std::make_unique<Button>(350,100,220,80,33,150,243,"Invite") );

    int CX = 0,CY = 0;
    bool touched = false;

    tinygles::VerticesShortXY history;
    // Add one point to make logic in adding code easier.
    // As we know always will have at least one point.
    history.emplace_back((short)CX,(short)CY);

    GL.SetSystemEventHandler([&myButtons,&CX,&CY,&touched,&history](const tinygles::SystemEventData& pEvent)
    {
        if( pEvent.mType == tinygles::SystemEventType::POINTER_UPDATED )
        {
            CX = pEvent.mPointer.x;
            CY = pEvent.mPointer.y;

            // Only add if moved enough to be intresting.
            const int dx = (CX - history.end()->x);
            const int dy = (CY - history.end()->y);

            if( ( (dx*dx) + (dy*dy) ) > 40 )
            {
                history.emplace_back((short)CX,(short)CY);
            }

            // If too big, remove first point.
            if( history.size() > 100 )
            {
                history.erase(history.begin());
            }

            touched = pEvent.mPointer.touched;
            for( auto& b : myButtons )
            {
                if( b->ContainsPoint(pEvent.mPointer.x,pEvent.mPointer.y) )
                {
                    b->SetPressed(pEvent.mPointer.touched);
                }
                else
                {
                    b->SetPressed(false);
                }
            }
        }
    });

    while( GL.BeginFrame() )
    {
        GL.Clear(255,255,255);
        GL.FontPrint(0,0,"Basic touch screen test");

        for( auto& b : myButtons )
        {
            b->Draw();
        }

        GL.FillCircle(CX,CY,touched?50:30,0,0,0,100);

        // Only draw when we have some intresting points.
        if( history.size() > 3 )
        {
            GL.DrawLineList(history,5,50,200,50);
        }

        GL.EndFrame();
    }


// And quit";
    return EXIT_SUCCESS;
}
