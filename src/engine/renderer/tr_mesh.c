/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_mesh.c: triangle model functions

#include "tr_local.h"

static float ProjectRadius(float r, vec3_t location)
{
	float pr;
	float dist;
	float c;
	vec3_t	p;
	float	projected[4];

	c = DotProduct(tr.viewParms. or .axis[0], tr.viewParms. or .origin);
	dist = DotProduct(tr.viewParms. or .axis[0], location) - c;

	if (dist <= 0)
		return 0;

	p[0] = 0;
	p[1] = fabs(r);
	p[2] = -dist;

	projected[0] = p[0] * tr.viewParms.projectionMatrix[0] +
		p[1] * tr.viewParms.projectionMatrix[4] +
		p[2] * tr.viewParms.projectionMatrix[8] +
		tr.viewParms.projectionMatrix[12];

	projected[1] = p[0] * tr.viewParms.projectionMatrix[1] +
		p[1] * tr.viewParms.projectionMatrix[5] +
		p[2] * tr.viewParms.projectionMatrix[9] +
		tr.viewParms.projectionMatrix[13];

	projected[2] = p[0] * tr.viewParms.projectionMatrix[2] +
		p[1] * tr.viewParms.projectionMatrix[6] +
		p[2] * tr.viewParms.projectionMatrix[10] +
		tr.viewParms.projectionMatrix[14];

	projected[3] = p[0] * tr.viewParms.projectionMatrix[3] +
		p[1] * tr.viewParms.projectionMatrix[7] +
		p[2] * tr.viewParms.projectionMatrix[11] +
		tr.viewParms.projectionMatrix[15];


	pr = projected[1] / projected[3];

	if (pr > 1.0f)
		pr = 1.0f;

	return pr;
}

/*
=============
R_CullModel
=============
*/
static int R_CullModel(md3Header_t *header, trRefEntity_t *ent) {
	vec3_t		bounds[2];
	md3Frame_t	*oldFrame, *newFrame;
	int			i;

	// compute frame pointers
	newFrame = (md3Frame_t *)((byte *)header + header->ofsFrames) + ent->e.frame;
	oldFrame = (md3Frame_t *)((byte *)header + header->ofsFrames) + ent->e.oldframe;

	// cull bounding sphere ONLY if this is not an upscaled entity
	if (!ent->e.nonNormalizedAxes)
	{
		if (ent->e.frame == ent->e.oldframe)
		{
			switch (R_CullLocalPointAndRadius(newFrame->localOrigin, newFrame->radius))
			{
			case CULL_OUT:
				tr.pc.c_sphere_cull_md3_out++;
				return CULL_OUT;

			case CULL_IN:
				tr.pc.c_sphere_cull_md3_in++;
				return CULL_IN;

			case CULL_CLIP:
				tr.pc.c_sphere_cull_md3_clip++;
				break;
			}
		}
		else
		{
			int sphereCull, sphereCullB;

			sphereCull = R_CullLocalPointAndRadius(newFrame->localOrigin, newFrame->radius);
			if (newFrame == oldFrame) {
				sphereCullB = sphereCull;
			}
			else {
				sphereCullB = R_CullLocalPointAndRadius(oldFrame->localOrigin, oldFrame->radius);
			}

			if (sphereCull == sphereCullB)
			{
				if (sphereCull == CULL_OUT)
				{
					tr.pc.c_sphere_cull_md3_out++;
					return CULL_OUT;
				}
				else if (sphereCull == CULL_IN)
				{
					tr.pc.c_sphere_cull_md3_in++;
					return CULL_IN;
				}
				else
				{
					tr.pc.c_sphere_cull_md3_clip++;
				}
			}
		}
	}

	// calculate a bounding box in the current coordinate system
	for (i = 0; i < 3; i++) {
		bounds[0][i] = oldFrame->bounds[0][i] < newFrame->bounds[0][i] ? oldFrame->bounds[0][i] : newFrame->bounds[0][i];
		bounds[1][i] = oldFrame->bounds[1][i] > newFrame->bounds[1][i] ? oldFrame->bounds[1][i] : newFrame->bounds[1][i];
	}

	switch (R_CullLocalBox(bounds))
	{
	case CULL_IN:
		tr.pc.c_box_cull_md3_in++;
		return CULL_IN;
	case CULL_CLIP:
		tr.pc.c_box_cull_md3_clip++;
		return CULL_CLIP;
	case CULL_OUT:
	default:
		tr.pc.c_box_cull_md3_out++;
		return CULL_OUT;
	}
}


/*
=================
R_ComputeLOD

=================
*/
int R_ComputeLOD(trRefEntity_t *ent) {
	float radius;
	float flod, lodscale;
	float projectedRadius;
	md3Frame_t *frame;
	int lod;

	if (tr.currentModel->numLods < 2)
	{
		// model has only 1 LOD level, skip computations and bias
		lod = 0;
	}
	else
	{
		// multiple LODs exist, so compute projected bounding sphere
		// and use that as a criteria for selecting LOD

		frame = (md3Frame_t *)(((unsigned char *)tr.currentModel->md3[0]) + tr.currentModel->md3[0]->ofsFrames);

		frame += ent->e.frame;

		radius = RadiusFromBounds(frame->bounds[0], frame->bounds[1]);

		if ((projectedRadius = ProjectRadius(radius, ent->e.origin)) != 0)
		{
			lodscale = r_lodscale->value;
			if (lodscale > 20) lodscale = 20;
			flod = 1.0f - projectedRadius * lodscale;
		}
		else
		{
			// object intersects near view plane, e.g. view weapon
			flod = 0;
		}

		flod *= tr.currentModel->numLods;
		lod = myftol(flod);

		if (lod < 0)
		{
			lod = 0;
		}
		else if (lod >= tr.currentModel->numLods)
		{
			lod = tr.currentModel->numLods - 1;
		}
	}

	lod += r_lodbias->integer;

	if (lod >= tr.currentModel->numLods)
		lod = tr.currentModel->numLods - 1;
	if (lod < 0)
		lod = 0;

	return lod;
}

/*
=================
R_ComputeFogNum

=================
*/
int R_ComputeFogNum(md3Header_t *header, trRefEntity_t *ent) {
	int				i, j;
	fog_t			*fog;
	md3Frame_t		*md3Frame;
	vec3_t			localOrigin;

	if (tr.refdef.rdflags & RDF_NOWORLDMODEL) {
		return 0;
	}

	// FIXME: non-normalized axis issues
	md3Frame = (md3Frame_t *)((byte *)header + header->ofsFrames) + ent->e.frame;
	VectorAdd(ent->e.origin, md3Frame->localOrigin, localOrigin);
	for (i = 1; i < tr.world->numfogs; i++) {
		fog = &tr.world->fogs[i];
		for (j = 0; j < 3; j++) {
			if (localOrigin[j] - md3Frame->radius >= fog->bounds[1][j]) {
				break;
			}
			if (localOrigin[j] + md3Frame->radius <= fog->bounds[0][j]) {
				break;
			}
		}
		if (j == 3) {
			return i;
		}
	}

	return 0;
}

/*
=================
R_AddMD3Surfaces

=================
*/
void R_AddMD3Surfaces(trRefEntity_t *ent)
{
	int				i;
	md3Header_t		*header = 0;
	md3Surface_t	*surface = 0;
	md3Shader_t		*md3Shader = 0;
	shader_t		*shader = 0;
	int				cull;
	int				lod;
	int				fogNum;
	qboolean		personalModel;

	model_t* pModel = R_GetModelByHandle(ent->e.hModel);

	// don't add third_person objects if not in a portal
	personalModel = (qboolean)((ent->e.renderfx & RF_THIRD_PERSON) && !tr.viewParms.isPortal);

	if (ent->e.renderfx & RF_WRAP_FRAMES) {
		ent->e.frame %= tr.currentModel->md3[0]->numFrames;
		ent->e.oldframe %= tr.currentModel->md3[0]->numFrames;
	}

	//
	// Validate the frames so there is no chance of a crash.
	// This will write directly into the entity structure, so
	// when the surfaces are rendered, they don't need to be
	// range checked again.
	//
	if ((ent->e.frame >= tr.currentModel->md3[0]->numFrames)
		|| (ent->e.frame < 0)
		|| (ent->e.oldframe >= tr.currentModel->md3[0]->numFrames)
		|| (ent->e.oldframe < 0)) {
		ri.Printf(PRINT_DEVELOPER, "R_AddMD3Surfaces: no such frame %d to %d for '%s'\n",
			ent->e.oldframe, ent->e.frame,
			tr.currentModel->name);
		ent->e.frame = 0;
		ent->e.oldframe = 0;
	}

	//
	// compute LOD
	//
	lod = R_ComputeLOD(ent);

	header = tr.currentModel->md3[lod];

	//
	// cull the entire model if merged bounding box of both frames
	// is outside the view frustum.
	//
	cull = R_CullModel(header, ent);
	if (cull == CULL_OUT) {
		return;
	}

	//
	// set up lighting now that we know we aren't culled
	//
	if (!personalModel || r_shadows->integer > 1) {
		R_SetupEntityLighting(&tr.refdef, ent);
	}

	//
	// see if we are in a fog volume
	//
	fogNum = R_ComputeFogNum(header, ent);
	
	bool animated = false;
	if (ent->e.oldframe != ent->e.frame) {
		animated = true;
	}
	
	//
	// draw all surfaces
	//

	surface = (md3Surface_t *)((byte *)header + header->ofsSurfaces);
	for (i = 0; i < header->numSurfaces; i++)
	{
		const int dxrSurfaceId = i % MD3_MAX_DXR_SURFACES;//Dont't think it will ever get this high, just just incase 
		if (ent->e.customShader)
		{
			shader = R_GetShaderByHandle(ent->e.customShader);
		}
		else if (ent->e.customSkin > 0 && ent->e.customSkin < tr.numSkins)
		{
			skin_t *skin;
			int		j;

			skin = R_GetSkinByHandle(ent->e.customSkin);

			// match the surface name to something in the skin file
			shader = tr.defaultShader;
			for (j = 0; j < skin->numSurfaces; j++) {
				// the names have both been lowercased
				if (!strcmp(skin->surfaces[j]->name, surface->name)) {
					shader = skin->surfaces[j]->shader;
					break;
				}
			}
			if (shader == tr.defaultShader) {
				ri.Printf(PRINT_DEVELOPER, "WARNING: no shader for surface %s in skin %s\n", surface->name, skin->name);
			}
			else if (shader->defaultShader) {
				ri.Printf(PRINT_DEVELOPER, "WARNING: shader %s in skin %s not found\n", shader->name, skin->name);
			}
		}
		else if (surface->numShaders <= 0) {
			shader = tr.defaultShader;
		}
		else {
			md3Shader = (md3Shader_t *)((byte *)surface + surface->ofsShaders);
			md3Shader += ent->e.skinNum % surface->numShaders;
			shader = tr.shaders[md3Shader->shaderIndex];
		}

		bool updating =  animated;
		bool addingToDxr = false;
		if (-1 == pModel->bottomLevelIndexDxr[lod][dxrSurfaceId] && !personalModel)
		{
			int surfaceIndex = shader->stages[0]->bundle[0].image[0]->index;
			int texWidth = shader->stages[0]->bundle[0].image[0]->uploadWidth;
			int texHeight = shader->stages[0]->bundle[0].image[0]->uploadHeight;

			dxr_acceleration_model::meshType_t meshType = 1 < header->numFrames ? dxr_acceleration_model::DYNAMIC_MESH : dxr_acceleration_model::STATIC_MESH;
			pModel->bottomLevelIndexDxr[lod][dxrSurfaceId] = dxr_AddBottomLevelMesh(meshType, surfaceIndex, texWidth, texHeight);
			
			addingToDxr = true;
			updating = false;//getting Added not Updated
		}		

		if (!personalModel)
		{
			if (updating)
			{
				dxr_ResetMeshForUpdating(pModel->bottomLevelIndexDxr[lod][dxrSurfaceId]);
			}
		}
		
		// we will add shadows even if the main object isn't visible in the view

		// stencil shadows can't do personal models unless I polyhedron clip
		if (!personalModel
			&& r_shadows->integer == 2
			&& fogNum == 0
			&& !(ent->e.renderfx & (RF_NOSHADOW | RF_DEPTHHACK))
			&& shader->sort == SS_OPAQUE) {
			R_AddDrawSurf((surfaceType_t*)(void *)surface, tr.shadowShader, 0, qfalse);
		}

		// projection shadows work fine with personal models
		if (r_shadows->integer == 3
			&& fogNum == 0
			&& (ent->e.renderfx & RF_SHADOW_PLANE)
			&& shader->sort == SS_OPAQUE) {
			R_AddDrawSurf((surfaceType_t*)(void *)surface, tr.projectionShadowShader, 0, qfalse);
		}

		// don't add third_person objects if not viewing through a portal
		if (!personalModel)
		{
			R_AddDrawSurf((surfaceType_t*)(void *)surface, shader, fogNum, qfalse);

			float backlerp = 0.0f;

			if (ent->e.oldframe != ent->e.frame) {
				backlerp = ent->e.backlerp;
			}

			if (addingToDxr || updating)
			{
				//add drx model
				unsigned int	*indexes;
				int				numIndexes;
				int				numVerts;

				short	*newXyz, *newNormals;
				float	outXyz[4];
				float	outNormal[4];

				float	newXyzScale;
				float	newNormalScale;
				int		vertNum;
				unsigned lat, lng;

				int frameNumber = ent->e.frame;

				float *texCoords = (float *)((byte *)surface + surface->ofsSt);

				newXyz = (short *)((byte *)surface + surface->ofsXyzNormals)
					+ (frameNumber * surface->numVerts * 4);
				newNormals = newXyz + 3;

				newXyzScale = MD3_XYZ_SCALE * (1.0 - backlerp);
				newNormalScale = 1.0 - backlerp;

				numVerts = surface->numVerts;

				if (!updating)
				{
					indexes = (unsigned int *)((byte *)surface + surface->ofsTriangles);
					numIndexes = surface->numTriangles * 3;

					dxr_AddBottomLeveIndexesData(pModel->bottomLevelIndexDxr[lod][dxrSurfaceId], indexes, numIndexes);
				}

				if (backlerp == 0)
				{
					for (vertNum = 0; vertNum < numVerts; vertNum++,
						newXyz += 4, newNormals += 4, texCoords += 2)
					{
						outXyz[0] = newXyz[0] * newXyzScale;
						outXyz[1] = newXyz[1] * newXyzScale;
						outXyz[2] = newXyz[2] * newXyzScale;

						lat = (newNormals[0] >> 8) & 0xff;
						lng = (newNormals[0] & 0xff);
						lat *= (FUNCTABLE_SIZE / 256);
						lng *= (FUNCTABLE_SIZE / 256);

						outNormal[0] = tr.sinTable[(lat + (FUNCTABLE_SIZE / 4))&FUNCTABLE_MASK] * tr.sinTable[lng];
						outNormal[1] = tr.sinTable[lat] * tr.sinTable[lng];
						outNormal[2] = tr.sinTable[(lng + (FUNCTABLE_SIZE / 4))&FUNCTABLE_MASK];
						
						dxr_AddBottomLevelVertex(pModel->bottomLevelIndexDxr[lod][dxrSurfaceId], outXyz, outNormal, texCoords);
					}
				}
				else
				{
					//
					// interpolate and copy the vertex and normal
					//
					short	*oldXyz;
					oldXyz = (short *)((byte *)surface + surface->ofsXyzNormals)
						+ (ent->e.oldframe * surface->numVerts * 4);
					short	*oldNormals = oldXyz + 3;

					float oldXyzScale = MD3_XYZ_SCALE * backlerp;
					float oldNormalScale = backlerp;

					float *texCoords = (float *)((byte *)surface + surface->ofsSt);

					for (vertNum = 0; vertNum < numVerts; vertNum++,
						oldXyz += 4, newXyz += 4, oldNormals += 4, newNormals += 4, texCoords += 2)
					{
						vec3_t uncompressedOldNormal, uncompressedNewNormal;

						// interpolate the xyz
						outXyz[0] = oldXyz[0] * oldXyzScale + newXyz[0] * newXyzScale;
						outXyz[1] = oldXyz[1] * oldXyzScale + newXyz[1] * newXyzScale;
						outXyz[2] = oldXyz[2] * oldXyzScale + newXyz[2] * newXyzScale;

						// FIXME: interpolate lat/long instead?
						lat = (newNormals[0] >> 8) & 0xff;
						lng = (newNormals[0] & 0xff);
						lat *= 4;
						lng *= 4;
						uncompressedNewNormal[0] = tr.sinTable[(lat + (FUNCTABLE_SIZE / 4))&FUNCTABLE_MASK] * tr.sinTable[lng];
						uncompressedNewNormal[1] = tr.sinTable[lat] * tr.sinTable[lng];
						uncompressedNewNormal[2] = tr.sinTable[(lng + (FUNCTABLE_SIZE / 4))&FUNCTABLE_MASK];

						lat = (oldNormals[0] >> 8) & 0xff;
						lng = (oldNormals[0] & 0xff);
						lat *= 4;
						lng *= 4;

						uncompressedOldNormal[0] = tr.sinTable[(lat + (FUNCTABLE_SIZE / 4))&FUNCTABLE_MASK] * tr.sinTable[lng];
						uncompressedOldNormal[1] = tr.sinTable[lat] * tr.sinTable[lng];
						uncompressedOldNormal[2] = tr.sinTable[(lng + (FUNCTABLE_SIZE / 4))&FUNCTABLE_MASK];

						outNormal[0] = uncompressedOldNormal[0] * oldNormalScale + uncompressedNewNormal[0] * newNormalScale;
						outNormal[1] = uncompressedOldNormal[1] * oldNormalScale + uncompressedNewNormal[1] * newNormalScale;
						outNormal[2] = uncompressedOldNormal[2] * oldNormalScale + uncompressedNewNormal[2] * newNormalScale;

						VectorNormalize(outNormal);
						
						dxr_AddBottomLevelVertex(pModel->bottomLevelIndexDxr[lod][dxrSurfaceId], outXyz, outNormal, texCoords);
					}
				}
			}
		}

		if (!personalModel)
		{
			dxr_AddTopLevelIndexWithTransform(pModel->bottomLevelIndexDxr[lod][dxrSurfaceId], ent->e.axis, ent->e.origin);
		}

		surface = (md3Surface_t *)((byte *)surface + surface->ofsEnd);
	}	
}

