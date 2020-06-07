#pragma once

#include "D3d12.h"
#include "dx.h"

constexpr int SWAPCHAIN_BUFFER_COUNT = 2;

struct ID3D12Device5;
struct ID3D12DescriptorHeap;

class dx_renderTargets
{
public:	

	enum Dx_DepthTarget_Index {
		BACKBUFFER_DEPTH_RT = 0,
		G_BUFFER_DEPTH_RT,
		G_BUFFER_LAST_DEPTH_RT,
		DEPTH_TARGET_COUNT
	};


	enum Dx_RenderTarget_Index {
		G_BUFFER_ALBEDO_RT = 0,
		G_BUFFER_NORMALS_RT,
		G_BUFFER_LAST_NORMALS_RT,
		G_BUFFER_VELOCITY_RT,
		POST_LAST_FRAME_LIGHT_RT,
		RAY_OUTPUT_RT,
		POST_Reproject_RT,
		POST_OUTPUT_RT,
		POST_SPP_RT,
		POST_LAST_SPP_RT,
		RENDER_TARGET_COUNT
	};

	dx_renderTargets();
	~dx_renderTargets();

	static dx_renderTargets* Setup(ID3D12Device5* device);
	void Shutdown();

	static DXGI_FORMAT get_depth_format();

	void CreateDescriptorHeaps();
	void CreateDescriptors();	
	void CreateDepthBufferResources();
	void CreateDepthBufferResource(Dx_DepthTarget_Index index);

	void CreateRenderTargets(const int width, const int height);
	void CreateRenderTarget(const int width, const int height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, D3D12_CLEAR_VALUE* optimized_clear_value, D3D12_RESOURCE_STATES initialResourceState, Dx_RenderTarget_Index index);

	void CreateHelperTextures();
	Dx_Image CreateImage(int width, int height, Dx_Image_Format format, int mip_levels, UINT16 depthOrArraySize, bool repeat_texture, int image_index);
	void UploadImageData(ID3D12Resource* texture, int width, int height, int mip_levels, const uint8_t* pixels, int bytes_per_pixel);
	void UploadImageArrayData(ID3D12Resource* texture, int width, int height, UINT32 i, const uint8_t* pixels, int bytes_per_pixel);
	void CreateSamplerDescriptor(const Vk_Sampler_Def& def, Dx_Sampler_Index sampler_index);
	void MakeTemporalSampler(Dx_Sampler_Index sampler_index);

	ID3D12Resource* GetBackBufferRenderTarget(UINT frame_index)
	{
		assert(frame_index < SWAPCHAIN_BUFFER_COUNT);
		return render_targets[frame_index];
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetBackBuffer_rtv_handles(UINT frame_index)
	{
		assert(frame_index < SWAPCHAIN_BUFFER_COUNT);
		return mBackBuffer_rtv_handles[frame_index];
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GetBackBuffer_gpu_rtv_handles(UINT frame_index)
	{
		D3D12_GPU_DESCRIPTOR_HANDLE handle = rtv_heap->GetGPUDescriptorHandleForHeapStart();
		handle.ptr += frame_index * rtv_descriptor_size;
		
		return handle;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetBackBuffer_dsv_handle(Dx_DepthTarget_Index index)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE handle = dsv_heap->GetCPUDescriptorHandleForHeapStart();
		handle.ptr += index * dsv_descriptor_size;

		return handle;
	}
	
	D3D12_GPU_DESCRIPTOR_HANDLE GetSamplerHandle(Dx_Sampler_Index index)
	{
		assert(index < SAMPLER_COUNT);
		D3D12_GPU_DESCRIPTOR_HANDLE  handle = sampler_heap->GetGPUDescriptorHandleForHeapStart();
		handle.ptr += sampler_descriptor_size * index;
		return handle;
	}

	ID3D12DescriptorHeap* GetSamplerHeap()
	{
		return sampler_heap;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandle(int textureIndex)
	{
		assert(textureIndex < MAX_DRAWIMAGES);
		D3D12_GPU_DESCRIPTOR_HANDLE srv_handle = srv_heap->GetGPUDescriptorHandleForHeapStart();
		srv_handle.ptr += srv_descriptor_size * textureIndex;

		return srv_handle;
	}

	ID3D12DescriptorHeap* GetSrvHeap()
	{
		return srv_heap;
	}	
	
	ID3D12Device5* mDevice = (nullptr);

	ID3D12Resource* render_targets[SWAPCHAIN_BUFFER_COUNT];
	D3D12_RESOURCE_STATES render_targetsCurrentState[SWAPCHAIN_BUFFER_COUNT];

	void render_targetsSetState(D3D12_RESOURCE_STATES afterState, UINT frame_index);

	ID3D12Resource* depth_stencil_buffer[DEPTH_TARGET_COUNT];
	D3D12_RESOURCE_STATES mDepthTextureCurrentState[DEPTH_TARGET_COUNT];

	D3D12_CPU_DESCRIPTOR_HANDLE mBackBuffer_rtv_handles[SWAPCHAIN_BUFFER_COUNT];
	

private:

	//
	// Descriptor heaps.
	//
	ID3D12DescriptorHeap* dsv_heap = (nullptr);	// depth-stencil view.
	UINT dsv_descriptor_size = (0);
	
	ID3D12DescriptorHeap* rtv_heap = (nullptr);//render-target view.
	UINT rtv_descriptor_size = (0);

	ID3D12DescriptorHeap* srv_heap = (nullptr);
	UINT srv_descriptor_size = (0);

	ID3D12DescriptorHeap* sampler_heap = (nullptr);//Dx_Sampler_Index
	UINT sampler_descriptor_size = (0);


//public:
	//render targets
	ID3D12Resource* mRenderTargetTexture[RENDER_TARGET_COUNT];

	D3D12_CPU_DESCRIPTOR_HANDLE mRenderTargetTexture_handle[RENDER_TARGET_COUNT];
	D3D12_RESOURCE_STATES mRenderTargetTextureCurrentState[RENDER_TARGET_COUNT];

	ID3D12DescriptorHeap* rtv_G_BufferHeap = (nullptr);//render-target.
	UINT rtv_G_BufferDescriptor_size = (0);

	public:

	
	bool DoRenderTargetTextureState(D3D12_RESOURCE_STATES afterState, Dx_RenderTarget_Index index);
	void SetRenderTargetTextureState(D3D12_RESOURCE_STATES afterState, Dx_RenderTarget_Index index);
	bool DoDepthTextureState(D3D12_RESOURCE_STATES afterState, Dx_DepthTarget_Index index);
	void SetDepthTextureState(D3D12_RESOURCE_STATES afterState, Dx_DepthTarget_Index index);

	D3D12_CPU_DESCRIPTOR_HANDLE GetRenderTargetTextureHandle(Dx_RenderTarget_Index index)
	{
		return mRenderTargetTexture_handle[index];
	}

	ID3D12Resource*	GetRenderTargetTexture(Dx_RenderTarget_Index index)
	{
		return mRenderTargetTexture[index];
	}

	//depth
	ID3D12Resource* GetDepthStencilBuffer(Dx_DepthTarget_Index index)
	{
		return depth_stencil_buffer[index];
	}

	Dx_Image mNoiseImage;

};

