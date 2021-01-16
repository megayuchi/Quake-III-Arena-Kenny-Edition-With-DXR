#include "tr_local.h"

#include "dxr_heapManager.h"

#include "dxr.h"
#include "dx_shaders.h"
#include "dx_renderTargets.h"

#include "dxr_lights.h"

dxr_heapManager* dxr_heapManager::s_singletonPtr;

dxr_heapManager::dxr_heapManager()
{
	s_singletonPtr = this;
}

dxr_heapManager::~dxr_heapManager()
{
}

void dxr_heapManager::Reset()
{
	mTextureNodeList.clear();
}

void dxr_heapManager::Create_CBVSRVUAV_Heap(Dx_Instance &d3d, DXRGlobal &dxr, Dx_World &world)
{
	dx_renderTargets* rt = d3d.dx_renderTargets;
	mDevice = d3d.device;

	// Describe the CBV/SRV/UAV heap
	// Need 13 entries:
	// 1 CBV for the ViewCB
	// 1 CBV for the MaterialCB
	// 1 UAV for the RT output
	// 1 SRV for the Scene BVH

	// 1 SRV for the index buffer
	// 1 SRV for the vertex buffer
	// 1 SRV for the texture
	// 1 SRV for the texture
	// 1 SRV for the texture
	// 1 SRV for the depth texture
	// 1 SRV for the last depth texture

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = 13 + MAX_DRAWIMAGES;//+Game Textures
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	// Create the descriptor heap
	HRESULT hr = d3d.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mCbvSrvUavRayGenHeaps));
	if (FAILED(hr))
	{
		ri.Error(ERR_FATAL, "Error: failed to create DXR CBV/SRV/UAV descriptor heap!");
	}
#if defined(_DEBUG)
	mCbvSrvUavRayGenHeaps->SetName(L"cbvSrvUavRayGenHeaps");
#endif

	// Get the descriptor heap handle and increment size
	D3D12_CPU_DESCRIPTOR_HANDLE handle = mCbvSrvUavRayGenHeaps->GetCPUDescriptorHandleForHeapStart();
	mHandleIncrement = d3d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	

	// Create the ViewCB CBV
	ConstantBufferView(handle, world.viewCB->GetGPUVirtualAddress(), sizeof(*world.viewCBData));
	handle.ptr += mHandleIncrement;
	
	// Create the Light CBV
	ConstantBufferView(handle, d3d.dxr_lights->GetLightsCdGPUVirtualAddress(), d3d.dxr_lights->GetLightsCdDataSize());
	handle.ptr += mHandleIncrement;
	
	//Raytrace output
	UnorderedAccessViewTexture2D(handle, rt->GetRenderTargetTexture(dx_renderTargets::RAY_OUTPUT_RT));
	handle.ptr += mHandleIncrement;

	//Raytracing Acceleration Structure
	RaytracingAccelerationStructureView(handle, dxr.TLAS.pResult);
	handle.ptr += mHandleIncrement;

	{
		// Create the index buffer SRV
		UINT64 indexBuferByts = MEGA_INDEX_VERTEX_BUFFER_SIZE;
		UINT numElements = indexBuferByts / sizeof(float);

		BufferShaderResourceView(handle, d3d.resources->indexBufferMega, numElements);
		handle.ptr += mHandleIncrement;
	}

	{
		// Create the vertex buffer SRV
		UINT64 vertexBuferByts = MEGA_INDEX_VERTEX_BUFFER_SIZE; //TODO: tune vertex size
		UINT numElements = vertexBuferByts / sizeof(float);

		BufferShaderResourceView(handle, d3d.resources->vertexBufferMega, numElements);
		handle.ptr += mHandleIncrement;
	}

	// Albedo
	Texture2DShaderResourceView(handle, rt->GetRenderTargetTexture(dx_renderTargets::G_BUFFER_ALBEDO_RT), DXGI_FORMAT_R8G8B8A8_UNORM);
	handle.ptr += mHandleIncrement;

	// Normals
	Texture2DShaderResourceView(handle, rt->GetRenderTargetTexture(dx_renderTargets::G_BUFFER_NORMALS_RT), DXGI_FORMAT_R8G8B8A8_UNORM);
	handle.ptr += mHandleIncrement;

	// Velocity
	Texture2DShaderResourceView(handle, rt->GetRenderTargetTexture(dx_renderTargets::G_BUFFER_VELOCITY_RT), DXGI_FORMAT_R16G16_FLOAT);
	handle.ptr += mHandleIncrement;
	
	// Last Light
	Texture2DShaderResourceView(handle, rt->GetRenderTargetTexture(dx_renderTargets::POST_LAST_FRAME_LIGHT_RT), DXGI_FORMAT_R8G8B8A8_UNORM);
	handle.ptr += mHandleIncrement;

	// Create the material depth SRV
	Texture2DShaderResourceView(handle, rt->GetDepthStencilBuffer(dx_renderTargets::G_BUFFER_DEPTH_RT), DXGI_FORMAT_R32_FLOAT);
	handle.ptr += mHandleIncrement;

	// Create the material last depth SRV
	Texture2DShaderResourceView(handle, rt->GetDepthStencilBuffer(dx_renderTargets::G_BUFFER_LAST_DEPTH_RT), DXGI_FORMAT_R32_FLOAT);
	handle.ptr += mHandleIncrement;
	
	//Noise
	Texture2DArrayShaderResourceView(handle, rt->mNoiseImage.texture, 64);
	handle.ptr += mHandleIncrement;

	mGameTextureStartHandle = handle;

	AddGameTextureViews();
}

void dxr_heapManager::AddGameTextureViews(int image_index, ID3D12Resource* texture, DXGI_FORMAT dx_format, int mip_levels)
{
	TextureNode& textureNode = mTextureNodeList[image_index];
	textureNode.ImageIndex = image_index;
	textureNode.Texture = texture;
	textureNode.DxFormat = dx_format;
	textureNode.MipLevels = mip_levels;

	if (mGameTexturesLoaded)
	{
		AddGameTextureViews();
	}
}

void dxr_heapManager::AddGameTextureViews()
{
	for (auto const &n : mTextureNodeList)
	{
		auto const &t = n.second;
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
		srv_desc.Format = t.DxFormat;
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv_desc.Texture2D.MipLevels = t.MipLevels;

		D3D12_CPU_DESCRIPTOR_HANDLE handle;
		handle.ptr = mGameTextureStartHandle.ptr + t.ImageIndex * mHandleIncrement;
		mDevice->CreateShaderResourceView(t.Texture, &srv_desc, handle);
	}

	mTextureNodeList.clear();
	mGameTexturesLoaded = true;
}

D3D12_GPU_DESCRIPTOR_HANDLE dxr_heapManager::GetGameSrvHandle(int textureIndex)
{
	assert(textureIndex < MAX_DRAWIMAGES);

	D3D12_CPU_DESCRIPTOR_HANDLE handleCpuStart = mCbvSrvUavRayGenHeaps->GetCPUDescriptorHandleForHeapStart();
	SIZE_T offsetFromStart = mGameTextureStartHandle.ptr - handleCpuStart.ptr;

	D3D12_GPU_DESCRIPTOR_HANDLE handle = mCbvSrvUavRayGenHeaps->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += offsetFromStart;
	
	static cvar_t*	dxr_clear = ri.Cvar_Get("dxr_tex_offset", "-4", 0);

	handle.ptr += mHandleIncrement * (textureIndex + dxr_clear->integer);

	return handle;
}

void dxr_heapManager::ConstantBufferView(D3D12_CPU_DESCRIPTOR_HANDLE handle, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation, UINT sizeInBytes)
{
	D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
	desc.SizeInBytes = ALIGN(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, sizeInBytes);
	desc.BufferLocation = bufferLocation;

	mDevice->CreateConstantBufferView(&desc, handle);
}

void dxr_heapManager::UnorderedAccessViewTexture2D(D3D12_CPU_DESCRIPTOR_HANDLE handle, ID3D12Resource* renderTargetTexture)
{
	// Create the DXR output buffer UAV
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

	mDevice->CreateUnorderedAccessView(renderTargetTexture, nullptr, &uavDesc, handle);
}

void dxr_heapManager::RaytracingAccelerationStructureView(D3D12_CPU_DESCRIPTOR_HANDLE handle, ID3D12Resource* accelerationStructure)
{
	// Create the DXR Top Level Acceleration Structure SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.RaytracingAccelerationStructure.Location = accelerationStructure->GetGPUVirtualAddress();

	mDevice->CreateShaderResourceView(nullptr, &srvDesc, handle);
}

void dxr_heapManager::BufferShaderResourceView(D3D12_CPU_DESCRIPTOR_HANDLE handle, ID3D12Resource* buffer, UINT numElements)
{
	// Create the index buffer SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
	srvDesc.Buffer.StructureByteStride = 0;
	srvDesc.Buffer.FirstElement = 0;

	srvDesc.Buffer.NumElements = numElements;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	mDevice->CreateShaderResourceView(buffer, &srvDesc, handle);
}

void dxr_heapManager::Texture2DShaderResourceView(D3D12_CPU_DESCRIPTOR_HANDLE handle, ID3D12Resource* texture, DXGI_FORMAT format)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
	textureSRVDesc.Format = format;
	textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	textureSRVDesc.Texture2D.MipLevels = 1;
	textureSRVDesc.Texture2D.MostDetailedMip = 0;
	textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	mDevice->CreateShaderResourceView(texture, &textureSRVDesc, handle);
}

void dxr_heapManager::Texture2DArrayShaderResourceView(D3D12_CPU_DESCRIPTOR_HANDLE handle, ID3D12Resource* texture, UINT arraySize)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
	textureSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	textureSRVDesc.Texture2DArray.MipLevels = 1;
	textureSRVDesc.Texture2DArray.MostDetailedMip = 0;
	textureSRVDesc.Texture2DArray.FirstArraySlice = 0;
	textureSRVDesc.Texture2DArray.ArraySize = arraySize;
	textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	mDevice->CreateShaderResourceView(texture, &textureSRVDesc, handle);
}
