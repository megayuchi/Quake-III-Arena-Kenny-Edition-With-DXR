#include "tr_local.h"
#include "dxr_lights.h"

dxr_lights::dxr_lights()
{
}

dxr_lights::~dxr_lights()
{
}

void dxr_lights::Setup(ID3D12Device5* device, Dx_Instance &d3d)
{
	this->mDevice = device;
}

void dxr_lights::Shutdown()
{

}

void dxr_lights::AddLevelLight(const vec3_t origin, const vec3_t color, float size)
{
	Light l;
	l.Position[0] = origin[0];
	l.Position[1] = origin[1];
	l.Position[2] = origin[2];

	l.Color[0] = color[0];
	l.Color[1] = color[1];
	l.Color[2] = color[2];

	l.Size = size;

	mLightArray.push_back(l);
}

void dxr_lights::DrawLightDebug()
{
	int i = 0;
	int index = 0;
	for (auto& l : mLightArray)
	{
		vec3_t toVec;
		toVec[0] = l.Position[0] - dx_world.view_transform3D[12];
		toVec[1] = l.Position[1] - dx_world.view_transform3D[13];
		toVec[2] = l.Position[2] - dx_world.view_transform3D[14];

		toVec[0] = l.Position[0] - dx_world.viewOrg[0];
		toVec[1] = l.Position[1] - dx_world.viewOrg[1];
		toVec[2] = l.Position[2] - dx_world.viewOrg[2];

		toVec[0] = toVec[0] * toVec[0];
		toVec[1] = toVec[1] * toVec[1];
		toVec[2] = toVec[2] * toVec[2];
		float distSqr = toVec[0] + toVec[1] + toVec[2];
		float dist = sqrt(distSqr);
		//dist -= l.Size;
		if (dist < 500.0f)
		{
				for (int j = 0; j < 6; ++j)
			{
				tess.svars.colors[i + j][0] = /*l.Color[0] **/ 255.0f;
				tess.svars.colors[i + j][1] = /*l.Color[1] **/ 255.0f;
				tess.svars.colors[i + j][2] = /*l.Color[2] **/ 255.0f;
				tess.svars.colors[i + j][3] = 255;
			}

			tess.indexes[index++] = i + 0;
			tess.indexes[index++] = i + 2;

			tess.indexes[index++] = i + 0;
			tess.indexes[index++] = i + 3;

			tess.indexes[index++] = i + 0;
			tess.indexes[index++] = i + 4;

			tess.indexes[index++] = i + 0;
			tess.indexes[index++] = i + 5;

			//-----------------
			tess.indexes[index++] = i + 1;
			tess.indexes[index++] = i + 2;

			tess.indexes[index++] = i + 1;
			tess.indexes[index++] = i + 3;

			tess.indexes[index++] = i + 1;
			tess.indexes[index++] = i + 4;

			tess.indexes[index++] = i + 1;
			tess.indexes[index++] = i + 5;

			//-----------------
			tess.indexes[index++] = i + 2;
			tess.indexes[index++] = i + 4;

			tess.indexes[index++] = i + 2;
			tess.indexes[index++] = i + 5;

			//-----------------
			tess.indexes[index++] = i + 3;
			tess.indexes[index++] = i + 4;

			tess.indexes[index++] = i + 3;
			tess.indexes[index++] = i + 5;
			//top 0
			tess.xyz[i][0] = l.Position[0] + l.Size;
			tess.xyz[i][1] = l.Position[1];
			tess.xyz[i][2] = l.Position[2];
			i++;
			//bottom 1
			tess.xyz[i][0] = l.Position[0] - l.Size;
			tess.xyz[i][1] = l.Position[1];
			tess.xyz[i][2] = l.Position[2];
			i++;
			//left 2
			tess.xyz[i][0] = l.Position[0];
			tess.xyz[i][1] = l.Position[1] + l.Size;
			tess.xyz[i][2] = l.Position[2];
			i++;
			//right 3
			tess.xyz[i][0] = l.Position[0];
			tess.xyz[i][1] = l.Position[1] - l.Size;
			tess.xyz[i][2] = l.Position[2];
			i++;
			//front 4
			tess.xyz[i][0] = l.Position[0];
			tess.xyz[i][1] = l.Position[1];
			tess.xyz[i][2] = l.Position[2] + l.Size;
			i++;
			//back 5
			tess.xyz[i][0] = l.Position[0];
			tess.xyz[i][1] = l.Position[1];
			tess.xyz[i][2] = l.Position[2] - l.Size;
			i++;


		}

	}
	tess.numVertexes = i;
	tess.numIndexes = index;

	Com_Memcpy(dx_world.modelview_transform, dx_world.view_transform3D, 64);

	GL_Bind(tr.whiteImage);
	dx_bind_geometry();
	dx_shade_geometry(dx.normals_debug_pipeline, false, Vk_Depth_Range::force_zero, true, true);
}

void dxr_lights::Create_Lights_CB()
{
	/*
	Create_Constant_Buffer(&world.viewCB, sizeof(ViewCB));

	HRESULT hr = world.viewCB->Map(0, nullptr, reinterpret_cast<void**>(&world.viewCBStart));

	if (FAILED(hr))
	{
		ri.Error(ERR_FATAL, "Error: failed to map View constant buffer!");
	}

	memcpy(world.viewCBStart, &world.viewCBData, sizeof(*world.viewCBData));
	*/
}

void dxr_lights::UpdateConstantBuffer()
{

}

UINT dxr_lights::GetLightsCdDataSize()
{
	return 0;
}

D3D12_GPU_VIRTUAL_ADDRESS dxr_lights::GetLightsCdGPUVirtualAddress()
{
	return 0;
}