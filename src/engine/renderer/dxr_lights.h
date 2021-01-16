#pragma once

#include "D3d12.h"

#include <vector>

struct ID3D12Device5;
struct Dx_Instance;

class dxr_lights
{
public:
	dxr_lights();
	~dxr_lights();

	void Setup(ID3D12Device5* device, Dx_Instance &d3d);	
	void Shutdown();

	void AddLevelLight(const vec3_t origin, const vec3_t color, float size);
	void DrawLightDebug();

	void UpdateConstantBuffer();

	UINT GetLightsCdDataSize();
	D3D12_GPU_VIRTUAL_ADDRESS GetLightsCdGPUVirtualAddress();

private:
	void Create_Lights_CB();

	ID3D12Device5* mDevice = (nullptr);

	struct Light
	{
		vec3_t Position;
		vec3_t Color;
		float Size;
	};

	std::vector <Light> mLightArray;

	ID3D12Resource*									lightsCB;
	ViewCB*											lightsCBData;
	UINT8*											lightsCBStart;
};

