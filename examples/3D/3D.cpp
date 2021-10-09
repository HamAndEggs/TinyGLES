
#include "TinyGLES.h"

#include <iostream>
#include <vector>

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


int main(int argc, char *argv[])
{
    tinygles::GLES GL(tinygles::ROTATE_FRAME_LANDSCAPE);

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

    tinygles::VerticesXYZC aBox(6 * 2 * 3); // 6 Faces to the box, two triangles per face and three verts per triangle.

   	Set(aBox,verts,0,   0,1,2,3,    0xffff0000);//Front
	Set(aBox,verts,1,   5,4,7,6,    0xff00ff00);//Back
	Set(aBox,verts,2,   1,5,6,2,    0xff0000ff);//right
	Set(aBox,verts,3,   4,0,3,7,    0xffff00ff);//left
	Set(aBox,verts,4,   0,4,5,1,    0xffffff00);//top
	Set(aBox,verts,5,   3,2,6,7,    0xff00ffff);//bottom

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
        GL.RenderTriangles(aBox);

        r.Translate(0,-1,5);
        GL.SetTransform(r.m);
        GL.RenderTriangles(aBox);

        r.Translate(0,-2,5);
        GL.SetTransform(r.m);
        GL.RenderTriangles(aBox);

        r.Translate(1,-2,5);
        GL.SetTransform(r.m);
        GL.RenderTriangles(aBox);

        GL.EndFrame();

    }

// And quit
    return EXIT_SUCCESS;
}
