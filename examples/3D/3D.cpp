
#include "TinyGLES.h"
#include "../SupportCode/TinyPNG.h"

#include <iostream>
#include <vector>

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

static void Set(tinygles::VertXYZC& pVert,float x,float y, float z)
{
    pVert.x = x;
    pVert.y = y;
    pVert.z = z;
    pVert.argb = 0;
}

static void Set(tinygles::VerticesXYZC& pBox,const tinygles::VertXYZC pVerts[],int pQuadIndex,int v0,int v1,int v2,int v3, uint32_t pARGB)
{
    pQuadIndex *= 6;// siz verts per quad
    assert( pQuadIndex < 36 );

    pBox[pQuadIndex + 0] = pVerts[v0];
    pBox[pQuadIndex + 1] = pVerts[v1];
    pBox[pQuadIndex + 2] = pVerts[v3];
    pBox[pQuadIndex + 3] = pVerts[v1];
    pBox[pQuadIndex + 4] = pVerts[v2];
    pBox[pQuadIndex + 5] = pVerts[v3];

    pBox[pQuadIndex + 0].argb = pARGB;
    pBox[pQuadIndex + 1].argb = pARGB;
    pBox[pQuadIndex + 2].argb = pARGB;
    pBox[pQuadIndex + 3].argb = pARGB;
    pBox[pQuadIndex + 4].argb = pARGB;
    pBox[pQuadIndex + 5].argb = pARGB;
}

static void MakeColouredBox(tinygles::VerticesXYZC& aBox)
{
    float bx = 0.5f,by = 0.5f,bz = 0.5f;
    tinygles::VertXYZC verts[8];    // 8 verts used to build the box.

	Set(verts[0],-bx, by,-bz);
	Set(verts[1], bx, by,-bz);
	Set(verts[2], bx,-by,-bz);
	Set(verts[3],-bx,-by,-bz);
	Set(verts[4],-bx, by, bz);
	Set(verts[5], bx, by, bz);
	Set(verts[6], bx,-by, bz);
	Set(verts[7],-bx,-by, bz);
    
   	Set(aBox,verts,0,   0,1,2,3,    0xffff0000);//Front
	Set(aBox,verts,1,   5,4,7,6,    0xff00ff00);//Back
	Set(aBox,verts,2,   1,5,6,2,    0xff0000ff);//right
	Set(aBox,verts,3,   4,0,3,7,    0xffff00ff);//left
	Set(aBox,verts,4,   0,4,5,1,    0xffffff00);//top
	Set(aBox,verts,5,   3,2,6,7,    0xff00ffff);//bottom
}

static void Set(tinygles::VertXYZUV& pVert,float x,float y, float z)
{
    pVert.x = x;
    pVert.y = y;
    pVert.z = z;
}

static void Set(tinygles::VerticesXYZUV& pBox,const tinygles::VertXYZUV pVerts[],int pQuadIndex,int v0,int v1,int v2,int v3)
{
    pQuadIndex *= 6;// siz verts per quad
    assert( pQuadIndex < 36 );

    pBox[pQuadIndex + 0] = pVerts[v0];
    pBox[pQuadIndex + 1] = pVerts[v1];
    pBox[pQuadIndex + 2] = pVerts[v3];
    pBox[pQuadIndex + 3] = pVerts[v1];
    pBox[pQuadIndex + 4] = pVerts[v2];
    pBox[pQuadIndex + 5] = pVerts[v3];

    pBox[pQuadIndex + 0].SetUV(0,0);
    pBox[pQuadIndex + 1].SetUV(1,0);
    pBox[pQuadIndex + 2].SetUV(0,1);
    pBox[pQuadIndex + 3].SetUV(1,0);
    pBox[pQuadIndex + 4].SetUV(1,1);
    pBox[pQuadIndex + 5].SetUV(0,1);
}

static void MakeTexturedBox(tinygles::VerticesXYZUV& aBox)
{
    float bx = 0.5f,by = 0.5f,bz = 0.5f;
    tinygles::VertXYZUV verts[8];    // 8 verts used to build the box.

	Set(verts[0],-bx, by,-bz);
	Set(verts[1], bx, by,-bz);
	Set(verts[2], bx,-by,-bz);
	Set(verts[3],-bx,-by,-bz);
	Set(verts[4],-bx, by, bz);
	Set(verts[5], bx, by, bz);
	Set(verts[6], bx,-by, bz);
	Set(verts[7],-bx,-by, bz);
    
   	Set(aBox,verts,0,   0,1,2,3);//Front
	Set(aBox,verts,1,   5,4,7,6);//Back
	Set(aBox,verts,2,   1,5,6,2);//right
	Set(aBox,verts,3,   4,0,3,7);//left
	Set(aBox,verts,4,   0,4,5,1);//top
	Set(aBox,verts,5,   3,2,6,7);//bottom

}

int main(int argc, char *argv[])
{
    tinygles::GLES GL(tinygles::ROTATE_FRAME_LANDSCAPE);


    tinygles::VerticesXYZC colouredBox(6 * 2 * 3); // 6 Faces to the box, two triangles per face and three verts per triangle.
    MakeColouredBox(colouredBox);

    tinygles::VerticesXYZUV texturedBox(6 * 2 * 3); // 6 Faces to the box, two triangles per face and three verts per triangle.
    MakeTexturedBox(texturedBox);

    uint32_t create = LoadTexture(GL,"../data/tile_1.png");

    int anim = 0;
    tinygles::Matrix r,t;
    while( GL.BeginFrame() )
    {
        anim++;
        GL.Clear(100,100,100);

        GL.Begin2D();

        GL.FontPrint(0,0,"3D Basic Example");

        GL.Begin3D(45.0f,0.1f,100.0f);

        r.SetRotationX(anim);
        t.SetRotationY(anim*2.7f);
        r.Mul(t);
        t.SetRotationZ(anim*3.11f);
        r.Mul(t);

        r.Translate(0,0,5);
        GL.SetTransform(r.m);
        GL.RenderTriangles(colouredBox);

        r.Translate(0,-1,5);
        GL.SetTransform(r.m);
        GL.RenderTriangles(colouredBox);

        r.Translate(0,-2,5);
        GL.SetTransform(r.m);
        GL.RenderTriangles(colouredBox);

        r.Translate(1,-2,5);
        GL.SetTransform(r.m);
        GL.RenderTriangles(colouredBox);

        r.Translate(3,0,5);
        GL.SetTransform(r.m);
        GL.RenderTriangles(texturedBox,create);

        GL.EndFrame();

    }

// And quit
    return EXIT_SUCCESS;
}
