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


//private:
	ID3D12Device5* mDevice = (nullptr);

	ID3D12Resource* render_targets[SWAPCHAIN_BUFFER_COUNT];
	ID3D12Resource* depth_stencil_buffer = nullptr;

	//
	// Descriptor heaps.
	//
	ID3D12DescriptorHeap* rtv_heap = (nullptr);
	UINT rtv_descriptor_size = (0);

	ID3D12DescriptorHeap* dsv_heap = (nullptr);

	ID3D12DescriptorHeap* srv_heap = (nullptr);
	UINT srv_descriptor_size = (0);

	ID3D12DescriptorHeap* sampler_heap = (nullptr);
	UINT sampler_descriptor_size = (0);
};

