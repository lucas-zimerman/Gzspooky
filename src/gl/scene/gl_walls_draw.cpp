/*
** gl_walls_draw.cpp
** Wall rendering
**
**---------------------------------------------------------------------------
** Copyright 2000-2005 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
** 4. When not used as part of GZDoom or a GZDoom derivative, this code will be
**    covered by the terms of the GNU Lesser General Public License as published
**    by the Free Software Foundation; either version 2.1 of the License, or (at
**    your option) any later version.
** 5. Full disclosure of the entire project's source code, except for third
**    party libraries is mandatory. (NOTE: This clause is non-negotiable!)
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include "gl/system/gl_system.h"
#include "p_local.h"
#include "p_lnspec.h"
#include "a_sharedglobal.h"
#include "gl/gl_functions.h"

#include "gl/system/gl_interface.h"
#include "gl/system/gl_cvars.h"
#include "gl/renderer/gl_lightdata.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/data/gl_data.h"
#include "gl/data/gl_vertexbuffer.h"
#include "gl/dynlights/gl_dynlight.h"
#include "gl/dynlights/gl_glow.h"
#include "gl/scene/gl_drawinfo.h"
#include "gl/scene/gl_portal.h"
#include "gl/shaders/gl_shader.h"
#include "gl/textures/gl_material.h"
#include "gl/utility/gl_clock.h"
#include "gl/utility/gl_templates.h"

EXTERN_CVAR(Bool, gl_seamless)

//==========================================================================
//
// Sets up the texture coordinates for one light to be rendered
//
//==========================================================================
bool GLWall::PrepareLight(texcoord * tcs, ADynamicLight * light)
{
	float vtx[]={glseg.x1,zbottom[0],glseg.y1, glseg.x1,ztop[0],glseg.y1, glseg.x2,ztop[1],glseg.y2, glseg.x2,zbottom[1],glseg.y2};
	Plane p;
	Vector nearPt, up, right;
	float scale;

	p.Init(vtx,4);

	if (!p.ValidNormal()) 
	{
		return false;
	}

	if (!gl_SetupLight(p, light, nearPt, up, right, scale, Colormap.desaturation, true, !!(flags&GLWF_FOGGY))) 
	{
		return false;
	}

	if (tcs != NULL)
	{
		Vector t1;
		int outcnt[4]={0,0,0,0};

		for(int i=0;i<4;i++)
		{
			t1.Set(&vtx[i*3]);
			Vector nearToVert = t1 - nearPt;
			tcs[i].u = (nearToVert.Dot(right) * scale) + 0.5f;
			tcs[i].v = (nearToVert.Dot(up) * scale) + 0.5f;

			// quick check whether the light touches this polygon
			if (tcs[i].u<0) outcnt[0]++;
			if (tcs[i].u>1) outcnt[1]++;
			if (tcs[i].v<0) outcnt[2]++;
			if (tcs[i].v>1) outcnt[3]++;

		}
		// The light doesn't touch this polygon
		if (outcnt[0]==4 || outcnt[1]==4 || outcnt[2]==4 || outcnt[3]==4) return false;
	}

	draw_dlight++;
	return true;
}

//==========================================================================
//
// Collect lights for shader
//
//==========================================================================
FDynLightData lightdata;

void GLWall::SetupLights()
{
	float vtx[]={glseg.x1,zbottom[0],glseg.y1, glseg.x1,ztop[0],glseg.y1, glseg.x2,ztop[1],glseg.y2, glseg.x2,zbottom[1],glseg.y2};
	Plane p;

	lightdata.Clear();
	p.Init(vtx,4);

	if (!p.ValidNormal()) 
	{
		return;
	}
	for(int i=0;i<2;i++)
	{
		FLightNode *node;
		if (seg->sidedef == NULL)
		{
			node = NULL;
		}
		else if (!(seg->sidedef->Flags & WALLF_POLYOBJ))
		{
			node = seg->sidedef->lighthead[i];
		}
		else if (sub)
		{
			// Polobject segs cannot be checked per sidedef so use the subsector instead.
			node = sub->lighthead[i];
		}
		else node = NULL;

		// Iterate through all dynamic lights which touch this wall and render them
		while (node)
		{
			if (!(node->lightsource->flags2&MF2_DORMANT))
			{
				iter_dlight++;

				Vector fn, pos;

				float x = FIXED2FLOAT(node->lightsource->x);
				float y = FIXED2FLOAT(node->lightsource->y);
				float z = FIXED2FLOAT(node->lightsource->z);
				float dist = fabsf(p.DistToPoint(x, z, y));
				float radius = (node->lightsource->GetRadius() * gl_lights_size);
				float scale = 1.0f / ((2.f * radius) - dist);

				if (radius > 0.f && dist < radius)
				{
					Vector nearPt, up, right;

					pos.Set(x,z,y);
					fn=p.Normal();
					fn.GetRightUp(right, up);

					Vector tmpVec = fn * dist;
					nearPt = pos + tmpVec;

					Vector t1;
					int outcnt[4]={0,0,0,0};
					texcoord tcs[4];

					// do a quick check whether the light touches this polygon
					for(int i=0;i<4;i++)
					{
						t1.Set(&vtx[i*3]);
						Vector nearToVert = t1 - nearPt;
						tcs[i].u = (nearToVert.Dot(right) * scale) + 0.5f;
						tcs[i].v = (nearToVert.Dot(up) * scale) + 0.5f;

						if (tcs[i].u<0) outcnt[0]++;
						if (tcs[i].u>1) outcnt[1]++;
						if (tcs[i].v<0) outcnt[2]++;
						if (tcs[i].v>1) outcnt[3]++;

					}
					if (outcnt[0]!=4 && outcnt[1]!=4 && outcnt[2]!=4 && outcnt[3]!=4) 
					{
						gl_GetLight(p, node->lightsource, true, false, lightdata);
					}
				}
			}
			node = node->nextLight;
		}
	}
	int numlights[3];

	lightdata.Combine(numlights, gl.MaxLights());
	if (numlights[2] > 0)
	{
		draw_dlight+=numlights[2]/2;
		gl_RenderState.EnableLight(true);
		gl_RenderState.SetLights(numlights, &lightdata.arrays[0][0]);
	}
}


//==========================================================================
//
// General purpose wall rendering function
// everything goes through here
//
//==========================================================================

void GLWall::RenderWall(int textured, ADynamicLight * light, unsigned int *store)
{
	static texcoord tcs[4]; // making this variable static saves us a relatively costly stack integrity check.
	bool split = (gl_seamless && !(textured&RWF_NOSPLIT) && seg->sidedef != NULL && !(seg->sidedef->Flags & WALLF_POLYOBJ));

	if (!light)
	{
		tcs[0]=lolft;
		tcs[1]=uplft;
		tcs[2]=uprgt;
		tcs[3]=lorgt;
		if ((flags&GLWF_GLOW) && (textured & RWF_GLOW))
		{
			gl_RenderState.SetGlowPlanes(topplane, bottomplane);
			gl_RenderState.SetGlowParams(topglowcolor, bottomglowcolor);
		}

	}
	else
	{
		if (!PrepareLight(tcs, light)) return;
	}


	if (!(textured & RWF_NORENDER))
	{
		// disable the clip plane if it isn't needed (which can be determined by a simple check.)
		float ct = gl_RenderState.GetClipHeightTop();
		float cb = gl_RenderState.GetClipHeightBottom();
		if (ztop[0] <= ct && ztop[1] <= ct) gl_RenderState.SetClipHeightTop(65536.f);
		if (zbottom[0] >= cb && zbottom[1] >= cb) gl_RenderState.SetClipHeightBottom(-65536.f);
		gl_RenderState.Apply();
		gl_RenderState.SetClipHeightTop(ct);
		gl_RenderState.SetClipHeightBottom(cb);
	}

	// the rest of the code is identical for textured rendering and lights
	FFlatVertex *ptr = GLRenderer->mVBO->GetBuffer();
	unsigned int count, offset;

	ptr->Set(glseg.x1, zbottom[0], glseg.y1, tcs[0].u, tcs[0].v);
	ptr++;
	if (split && glseg.fracleft == 0) SplitLeftEdge(tcs, ptr);
	ptr->Set(glseg.x1, ztop[0], glseg.y1, tcs[1].u, tcs[1].v);
	ptr++;
	if (split && !(flags & GLWF_NOSPLITUPPER)) SplitUpperEdge(tcs, ptr);
	ptr->Set(glseg.x2, ztop[1], glseg.y2, tcs[2].u, tcs[2].v);
	ptr++;
	if (split && glseg.fracright == 1) SplitRightEdge(tcs, ptr);
	ptr->Set(glseg.x2, zbottom[1], glseg.y2, tcs[3].u, tcs[3].v);
	ptr++;
	if (split && !(flags & GLWF_NOSPLITLOWER)) SplitLowerEdge(tcs, ptr);
	count = GLRenderer->mVBO->GetCount(ptr, &offset);
	if (!(textured & RWF_NORENDER))
	{
		GLRenderer->mVBO->RenderArray(GL_TRIANGLE_FAN, offset, count);
		vertexcount += count;
	}
	if (store != NULL)
	{
		store[0] = offset;
		store[1] = count;
	}
}

//==========================================================================
//
// 
//
//==========================================================================

void GLWall::RenderFogBoundary()
{
	if (gl_fogmode && gl_fixedcolormap == 0)
	{
		int rel = rellight + getExtraLight();
		gl_SetFog(lightlevel, rel, &Colormap, false);
		gl_RenderState.SetEffect(EFF_FOGBOUNDARY);
		gl_RenderState.AlphaFunc(GL_GEQUAL, 0.f);
		RenderWall(RWF_BLANK);
		gl_RenderState.SetEffect(EFF_NONE);
	}
}


//==========================================================================
//
// 
//
//==========================================================================
void GLWall::RenderMirrorSurface()
{
	if (GLRenderer->mirrortexture == NULL) return;

	// For the sphere map effect we need a normal of the mirror surface,
	Vector v(glseg.y2-glseg.y1, 0 ,-glseg.x2+glseg.x1);
	v.Normalize();

	// we use texture coordinates and texture matrix to pass the normal stuff to the shader so that the default vertex buffer format can be used as is.
	lolft.u = lorgt.u = uplft.u = uprgt.u = v.X();
	lolft.v = lorgt.v = uplft.v = uprgt.v = v.Z();

	gl_RenderState.EnableTextureMatrix(true);
	gl_RenderState.mTextureMatrix.computeNormalMatrix(gl_RenderState.mViewMatrix);

	// Use sphere mapping for this
	gl_RenderState.SetEffect(EFF_SPHEREMAP);

	gl_SetColor(lightlevel, 0, Colormap ,0.1f);
	gl_SetFog(lightlevel, 0, &Colormap, true);
	gl_RenderState.BlendFunc(GL_SRC_ALPHA,GL_ONE);
	gl_RenderState.AlphaFunc(GL_GREATER,0);
	glDepthFunc(GL_LEQUAL);

	FMaterial * pat=FMaterial::ValidateTexture(GLRenderer->mirrortexture);
	pat->BindPatch(0);

	flags &= ~GLWF_GLOW;
	RenderWall(RWF_BLANK);

	gl_RenderState.EnableTextureMatrix(false);
	gl_RenderState.SetEffect(EFF_NONE);

	// Restore the defaults for the translucent pass
	gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl_RenderState.AlphaFunc(GL_GEQUAL, gl_mask_sprite_threshold);
	glDepthFunc(GL_LESS);

	// This is drawn in the translucent pass which is done after the decal pass
	// As a result the decals have to be drawn here.
	if (seg->sidedef->AttachedDecals)
	{
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-1.0f, -128.0f);
		glDepthMask(false);
		DoDrawDecals();
		glDepthMask(true);
		glPolygonOffset(0.0f, 0.0f);
		glDisable(GL_POLYGON_OFFSET_FILL);
		gl_RenderState.SetTextureMode(TM_MODULATE);
		gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
}


//==========================================================================
//
// 
//
//==========================================================================

void GLWall::RenderTranslucentWall()
{
	bool transparent = gltexture? gltexture->GetTransparent() : false;
	
	// currently the only modes possible are solid, additive or translucent
	// and until that changes I won't fix this code for the new blending modes!
	bool isadditive = RenderStyle == STYLE_Add;

	if (!transparent) gl_RenderState.AlphaFunc(GL_GEQUAL, gl_mask_threshold);
	else gl_RenderState.AlphaFunc(GL_GEQUAL, 0.f);
	if (isadditive) gl_RenderState.BlendFunc(GL_SRC_ALPHA,GL_ONE);

	int extra;
	if (gltexture) 
	{
		gl_RenderState.EnableGlow(!!(flags & GLWF_GLOW));
		gltexture->Bind(flags, 0);
		extra = getExtraLight();
	}
	else 
	{
		gl_RenderState.EnableTexture(false);
		extra = 0;
	}

	gl_SetColor(lightlevel, extra, Colormap, fabsf(alpha));
	if (type!=RENDERWALL_M2SNF) gl_SetFog(lightlevel, extra, &Colormap, isadditive);
	else gl_SetFog(255, 0, NULL, false);

	RenderWall(RWF_TEXTURED|RWF_NOSPLIT);

	// restore default settings
	if (isadditive) gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	if (!gltexture)	
	{
		gl_RenderState.EnableTexture(true);
	}
	gl_RenderState.EnableGlow(false);
}

//==========================================================================
//
// 
//
//==========================================================================
void GLWall::Draw(int pass)
{
	FLightNode * node;
	int rel;

#ifdef _DEBUG
	if (seg->linedef-lines==879)
	{
		int a = 0;
	}
#endif


	// This allows mid textures to be drawn on lines that might overlap a sky wall
	if ((flags&GLWF_SKYHACK && type==RENDERWALL_M2S) || type == RENDERWALL_COLORLAYER)
	{
		if (pass != GLPASS_DECALS)
		{
			glEnable(GL_POLYGON_OFFSET_FILL);
			glPolygonOffset(-1.0f, -128.0f);
		}
	}

	switch (pass)
	{
	case GLPASS_ALL:			// Single-pass rendering
		SetupLights();
		// fall through
	case GLPASS_PLAIN:			// Single-pass rendering
		rel = rellight + getExtraLight();
		gl_SetColor(lightlevel, rel, Colormap,1.0f);
		if (type!=RENDERWALL_M2SNF) gl_SetFog(lightlevel, rel, &Colormap, false);
		else gl_SetFog(255, 0, NULL, false);

		gl_RenderState.EnableGlow(!!(flags & GLWF_GLOW));
		gltexture->Bind(flags, 0);
		RenderWall(RWF_TEXTURED|RWF_GLOW);
		gl_RenderState.EnableGlow(false);
		gl_RenderState.EnableLight(false);
		break;

	case GLPASS_BASE:			// Base pass for non-masked polygons (all opaque geometry)
	case GLPASS_BASE_MASKED:	// Base pass for masked polygons (2sided mid-textures and transparent 3D floors)
		rel = rellight + getExtraLight();
		gl_SetColor(lightlevel, rel, Colormap,1.0f);
		if (!(flags&GLWF_FOGGY)) 
		{
			if (type!=RENDERWALL_M2SNF) gl_SetFog(lightlevel, rel, &Colormap, false);
			else gl_SetFog(255, 0, NULL, false);
		}
		gl_RenderState.EnableGlow(!!(flags & GLWF_GLOW));
		// fall through

		if (pass != GLPASS_BASE)
		{
			gltexture->Bind(flags, 0);
		}
		RenderWall(RWF_TEXTURED|RWF_GLOW);
		gl_RenderState.EnableGlow(false);
		gl_RenderState.EnableLight(false);
		break;

	case GLPASS_TEXTURE:		// modulated texture
		gltexture->Bind(flags, 0);
		RenderWall(RWF_TEXTURED);
		break;

	case GLPASS_LIGHT:
	case GLPASS_LIGHT_ADDITIVE:
		// black fog is diminishing light and should affect lights less than the rest!
		if (!(flags&GLWF_FOGGY)) gl_SetFog((255+lightlevel)>>1, 0, NULL, false);
		else gl_SetFog(lightlevel, 0, &Colormap, true);	

		if (seg->sidedef == NULL)
		{
			node = NULL;
		}
		else if (!(seg->sidedef->Flags & WALLF_POLYOBJ))
		{
			// Iterate through all dynamic lights which touch this wall and render them
			node = seg->sidedef->lighthead[pass==GLPASS_LIGHT_ADDITIVE];
		}
		else if (sub)
		{
			// To avoid constant rechecking for polyobjects use the subsector's lightlist instead
			node = sub->lighthead[pass==GLPASS_LIGHT_ADDITIVE];
		}
		else node = NULL;
		while (node)
		{
			if (!(node->lightsource->flags2&MF2_DORMANT))
			{
				iter_dlight++;
				RenderWall(RWF_TEXTURED, node->lightsource);
			}
			node = node->nextLight;
		}
		break;

	case GLPASS_DECALS:
	case GLPASS_DECALS_NOFOG:
		if (seg->sidedef && seg->sidedef->AttachedDecals)
		{
			if (pass==GLPASS_DECALS) 
			{
				gl_SetFog(lightlevel, rellight + getExtraLight(), &Colormap, false);
			}
			DoDrawDecals();
		}
		break;

	case GLPASS_TRANSLUCENT:
		switch (type)
		{
		case RENDERWALL_MIRRORSURFACE:
			RenderMirrorSurface();
			break;

		case RENDERWALL_FOGBOUNDARY:
			RenderFogBoundary();
			break;

		default:
			RenderTranslucentWall();
			break;
		}
	}

	if ((flags&GLWF_SKYHACK && type==RENDERWALL_M2S) || type == RENDERWALL_COLORLAYER)
	{
		if (pass!=GLPASS_DECALS)
		{
			glDisable(GL_POLYGON_OFFSET_FILL);
			glPolygonOffset(0, 0);
		}
	}
}