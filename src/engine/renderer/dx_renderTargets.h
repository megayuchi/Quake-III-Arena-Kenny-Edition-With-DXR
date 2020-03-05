#pragma once

#include "D3d12.h"
#include "dx.h"

constexpr int SWAPCHAIN_BUFFER_COUNT = 2;

struct ID3D12Device5;
struct ID3D12DescriptorHeap;

class dx_renderTargets
{
public:	
	dx_renderTargets();
	~dx_renderTargets();

	static dx_renderTargets* Setup(ID3D12Device5* device);
	void Shutdown();

	static DXGI_FORMAT get_depth_format();

	void CreateDescriptorHeaps();
	void CreateDescriptors();
	void CreateDepthBufferResources();

	Dx_Image CreateImage(int width, int height, Dx_Image_Format format, int mip_levels, bool repeat_texture, int image_index);
	void UploadImageData(ID3D12Resource* texture, int width, int height, int mip_levels, const uint8_t* pixels, int bytes_per_pixel);
	void CreateSamplerDescriptor(const Vk_Sampler_Def& def, Dx_Sampler_Index sampler_index);

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
	ID3D12Resource* depth_stencil_buffer = nullptr;

	D3D12_CPU_DESCRIPTOR_HANDLE mBackBuffer_rtv_handles[SWAPCHAIN_BUFFER_COUNT];

	//
	// Descriptor heaps.
	//
	ID3D12DescriptorHeap* dsv_heap = (nullptr);	// depth-stencil view.

private:

	ID3D12DescriptorHeap* rtv_heap = (nullptr);//render-target view.
	UINT rtv_descriptor_size = (0);

	ID3D12DescriptorHeap* srv_heap = (nullptr);
	UINT srv_descriptor_size = (0);

	ID3D12DescriptorHeap* sampler_heap = (nullptr);//Dx_Sampler_Index
	UINT sampler_descriptor_size = (0);


};

