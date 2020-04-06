#include "tr_local.h"
#include "../../engine/platform/win_local.h"

#include "dx_renderTargets.h"

#include <d3dx12.h>
#include <functional>

DXGI_FORMAT dx_renderTargets::get_depth_format() {
	/*if (r_stencilbits->integer > 0) {
		glConfig.stencilBits = 8;
		return DXGI_FORMAT_D24_UNORM_S8_UINT;
	}
	else {
		glConfig.stencilBits = 0;
		return DXGI_FORMAT_D32_FLOAT;
	}*/

	return DXGI_FORMAT_D32_FLOAT;
}

static D3D12_HEAP_PROPERTIES get_heap_properties(D3D12_HEAP_TYPE heap_type) {
	D3D12_HEAP_PROPERTIES properties;
	properties.Type = heap_type;
	properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	properties.CreationNodeMask = 1;
	properties.VisibleNodeMask = 1;
	return properties;
}

static D3D12_RESOURCE_DESC get_buffer_desc(UINT64 size) {
	D3D12_RESOURCE_DESC desc;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Alignment = 0;
	desc.Width = size;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	return desc;
}

dx_renderTargets::dx_renderTargets()
{
}


dx_renderTargets::~dx_renderTargets()
{
}

dx_renderTargets* dx_renderTargets::Setup(ID3D12Device5* device)
{
	dx_renderTargets* self = new dx_renderTargets();
	self->mDevice = device;
	return self;
}

void dx_renderTargets::Shutdown()
{
	for (int i = 0; i < SWAPCHAIN_BUFFER_COUNT; i++) {
		render_targets[i]->Release();
	}

	rtv_heap->Release();
	rtv_G_BufferHeap->Release();
	srv_heap->Release();
	sampler_heap->Release();

	for (int i=0; i < DEPTH_TARGET_COUNT; ++i)
	{
		depth_stencil_buffer[i]->Release();
	}
	dsv_heap->Release();

}

#define DX_CHECK(function_call) { \
	HRESULT hr = function_call; \
	if (FAILED(hr)) \
		ri.Error(ERR_FATAL, "Direct3D: error returned by %s", #function_call); \
}

void dx_renderTargets::CreateDescriptorHeaps()
{
	// RTV heap.
	{
		D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
		heap_desc.NumDescriptors = SWAPCHAIN_BUFFER_COUNT + 1;
		heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		heap_desc.NodeMask = 0;
		DX_CHECK(mDevice->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap)));
		rtv_descriptor_size = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
		heap_desc.NumDescriptors = RENDER_TARGET_COUNT;
		heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		heap_desc.NodeMask = 0;
		DX_CHECK(mDevice->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_G_BufferHeap)));
		rtv_G_BufferDescriptor_size = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	// DSV heap.
	{
		D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
		heap_desc.NumDescriptors = DEPTH_TARGET_COUNT;
		heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		heap_desc.NodeMask = 0;
		DX_CHECK(mDevice->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&dsv_heap)));
		dsv_descriptor_size = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	}

	// SRV heap.
	{
		D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
		heap_desc.NumDescriptors = MAX_DRAWIMAGES;
		heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heap_desc.NodeMask = 0;
		DX_CHECK(mDevice->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&srv_heap)));
		srv_descriptor_size = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	// Sampler heap.
	{
		D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
		heap_desc.NumDescriptors = SAMPLER_COUNT;
		heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heap_desc.NodeMask = 0;
		DX_CHECK(mDevice->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&sampler_heap)));
		sampler_descriptor_size = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
	}
}

void dx_renderTargets::CreateDescriptors()
{
	// RTV descriptors.
	{
#if defined(_DEBUG)
		render_targets[0]->SetName(L"BackBufferRt_0");
		render_targets[1]->SetName(L"BackBufferRt_1");
#endif

		D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
		for (int i = 0; i < SWAPCHAIN_BUFFER_COUNT; i++)
		{
			mDevice->CreateRenderTargetView(render_targets[i], nullptr, rtv_handle);
			mBackBuffer_rtv_handles[i] = rtv_handle;
			rtv_handle.ptr += rtv_descriptor_size;
		}
	}

	// Samplers.
	{
		{
			Vk_Sampler_Def def;
			def.repeat_texture = true;
			def.gl_mag_filter = gl_filter_max;
			def.gl_min_filter = gl_filter_min;
			CreateSamplerDescriptor(def, SAMPLER_MIP_REPEAT);
		}
		{
			Vk_Sampler_Def def;
			def.repeat_texture = false;
			def.gl_mag_filter = gl_filter_max;
			def.gl_min_filter = gl_filter_min;
			CreateSamplerDescriptor(def, SAMPLER_MIP_CLAMP);
		}
		{
			Vk_Sampler_Def def;
			def.repeat_texture = true;
			def.gl_mag_filter = GL_LINEAR;
			def.gl_min_filter = GL_LINEAR;
			CreateSamplerDescriptor(def, SAMPLER_NOMIP_REPEAT);
		}
		{
			Vk_Sampler_Def def;
			def.repeat_texture = false;
			def.gl_mag_filter = GL_LINEAR;
			def.gl_min_filter = GL_LINEAR;
			CreateSamplerDescriptor(def, SAMPLER_NOMIP_CLAMP);
		}
		{
			Vk_Sampler_Def def;
			def.repeat_texture = false;
			def.gl_mag_filter = GL_NEAREST;
			def.gl_min_filter = GL_NEAREST;
			CreateSamplerDescriptor(def, SAMPLER_FULLSCREEN_CLAMP);
		}

	}
}
void dx_renderTargets::CreateRenderTargets(const int width, const int height)
{
	CreateRenderTarget(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE, nullptr, D3D12_RESOURCE_STATE_COPY_DEST, G_BUFFER_ALBEDO_RT);
	CreateRenderTarget(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE, nullptr, D3D12_RESOURCE_STATE_COPY_DEST, G_BUFFER_NORMALS_RT);
	CreateRenderTarget(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE, nullptr, D3D12_RESOURCE_STATE_COPY_DEST, G_BUFFER_VELOCITY_RT);
	CreateRenderTarget(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE, nullptr, D3D12_RESOURCE_STATE_COPY_DEST, G_BUFFER_LAST_FRAME_LIGHT_RT);
#if defined(_DEBUG)
	mRenderTargetTexture[G_BUFFER_ALBEDO_RT]->SetName(L"G_BUFFER_ALBEDO_RT-Texture");		
	mRenderTargetTexture[G_BUFFER_NORMALS_RT]->SetName(L"G_BUFFER_NORMALS_RT-Texture");
	mRenderTargetTexture[G_BUFFER_VELOCITY_RT]->SetName(L"G_BUFFER_VELOCITY_RT-Texture");
	mRenderTargetTexture[G_BUFFER_LAST_FRAME_LIGHT_RT]->SetName(L"G_BUFFER_LAST_FRAME_LIGHT_RT-Texture");
	
#endif
}

void dx_renderTargets::CreateRenderTarget(const int width, const int height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, D3D12_CLEAR_VALUE* optimized_clear_value, D3D12_RESOURCE_STATES initialResourceState, Dx_RenderTarget_Index index)
{
	//make texture?
	HRESULT hr;

	// Describe the texture
	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = format;
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | flags;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	textureDesc.Alignment = 0;
	textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	// Create the texture resource
	hr = mDevice->CreateCommittedResource(&get_heap_properties(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		initialResourceState,
		optimized_clear_value,
		IID_PPV_ARGS(&mRenderTargetTexture[index]));

	if (FAILED(hr))
	{
		ri.Error(ERR_FATAL, "Error: failed to create texture!");
	}



	mRenderTargetTextureCurrentState[index] = initialResourceState;

	//make target
	D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_G_BufferHeap->GetCPUDescriptorHandleForHeapStart();
	rtv_handle.ptr += index * rtv_G_BufferDescriptor_size;

	mDevice->CreateRenderTargetView(mRenderTargetTexture[index], nullptr, rtv_handle);
	mRenderTargetTexture_handle[index] = rtv_handle;
}

void dx_renderTargets::CreateDepthBufferResources()
{
	CreateDepthBufferResource(dx_renderTargets::BACKBUFFER_DEPTH_RT);
	CreateDepthBufferResource(dx_renderTargets::G_BUFFER_DEPTH_RT);

#if defined(_DEBUG)
	depth_stencil_buffer[BACKBUFFER_DEPTH_RT]->SetName(L"BACKBUFFER_DEPTH_RT");
	depth_stencil_buffer[G_BUFFER_DEPTH_RT]->SetName(L"G_BUFFER_DEPTH_RT");
#endif
}
void dx_renderTargets::CreateDepthBufferResource(Dx_DepthTarget_Index index)
{
	D3D12_RESOURCE_DESC depth_desc{};
	depth_desc.MipLevels = 1; 
	depth_desc.Format = get_depth_format();
	depth_desc.Width = glConfig.vidWidth;
	depth_desc.Height = glConfig.vidHeight;
	depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL /*| D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET*/;
	depth_desc.DepthOrArraySize = 1;
	depth_desc.SampleDesc.Count = 1;
	depth_desc.SampleDesc.Quality = 0;
	depth_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depth_desc.Alignment = 0;	
	depth_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	
	
	D3D12_CLEAR_VALUE optimized_clear_value{};
	optimized_clear_value.Format = get_depth_format();
	optimized_clear_value.DepthStencil.Depth = 1.0f;
	optimized_clear_value.DepthStencil.Stencil = 0;

	DX_CHECK(mDevice->CreateCommittedResource(
		&get_heap_properties(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&depth_desc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&optimized_clear_value,
		IID_PPV_ARGS(&depth_stencil_buffer[index])));

	D3D12_DEPTH_STENCIL_VIEW_DESC view_desc{};
	view_desc.Format = get_depth_format();
	view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	view_desc.Flags = D3D12_DSV_FLAG_NONE;

	D3D12_CPU_DESCRIPTOR_HANDLE handle = dsv_heap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += index * dsv_descriptor_size;
	mDevice->CreateDepthStencilView(depth_stencil_buffer[index], &view_desc, handle);


	mDepthTextureCurrentState[index] = D3D12_RESOURCE_STATE_DEPTH_WRITE;
}

Dx_Image dx_renderTargets::CreateImage(int width, int height, Dx_Image_Format format, int mip_levels, bool repeat_texture, int image_index)
{
	Dx_Image image;

	DXGI_FORMAT dx_format;
	if (format == IMAGE_FORMAT_RGBA8)
		dx_format = DXGI_FORMAT_R8G8B8A8_UNORM;
	else if (format == IMAGE_FORMAT_BGRA4)
		dx_format = DXGI_FORMAT_B4G4R4A4_UNORM;
	else {
		assert(format == IMAGE_FORMAT_BGR5A1);
		dx_format = DXGI_FORMAT_B5G5R5A1_UNORM;
	}

	// create texture
	{
		D3D12_RESOURCE_DESC desc;
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Alignment = 0;
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = mip_levels;
		desc.Format = dx_format;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		DX_CHECK(mDevice->CreateCommittedResource(
			&get_heap_properties(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&image.texture)));
	}

	// create texture descriptor
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
		srv_desc.Format = dx_format;
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv_desc.Texture2D.MipLevels = mip_levels;

		D3D12_CPU_DESCRIPTOR_HANDLE handle;
		handle.ptr = srv_heap->GetCPUDescriptorHandleForHeapStart().ptr + image_index * srv_descriptor_size;
		mDevice->CreateShaderResourceView(image.texture, &srv_desc, handle);

		dx_world.current_image_indices[glState.currenttmu] = image_index;
	}

	if (mip_levels > 0)
		image.sampler_index = repeat_texture ? SAMPLER_MIP_REPEAT : SAMPLER_MIP_CLAMP;
	else
		image.sampler_index = repeat_texture ? SAMPLER_NOMIP_REPEAT : SAMPLER_NOMIP_CLAMP;

	return image;
}

D3D12_RESOURCE_BARRIER get_transition_barrier2(
	ID3D12Resource* resource,
	D3D12_RESOURCE_STATES state_before,
	D3D12_RESOURCE_STATES state_after)
{
	D3D12_RESOURCE_BARRIER barrier;
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = state_before;
	barrier.Transition.StateAfter = state_after;
	return barrier;
}

static void record_and_run_commands(std::function<void(ID3D12GraphicsCommandList*)> recorder) {
	ID3D12GraphicsCommandList* command_list;
	DX_CHECK(dx.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, dx.helper_command_allocator,
		nullptr, IID_PPV_ARGS(&command_list)));

	recorder(command_list);
	DX_CHECK(command_list->Close());

	ID3D12CommandList* command_lists[] = { command_list };
	dx.command_queue->ExecuteCommandLists(1, command_lists);
	dx_wait_device_idle();

	command_list->Release();
	dx.helper_command_allocator->Reset();
}

void dx_renderTargets::UploadImageData(ID3D12Resource* texture, int width, int height, int mip_levels, const uint8_t* pixels, int bytes_per_pixel) {
	//
	// Initialize subresource layouts int the upload texture.
	//
	auto align = [](size_t value, size_t alignment) {
		return (value + alignment - 1) & ~(alignment - 1);
	};

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT regions[16];
	UINT64 buffer_size = 0;

	int w = width;
	int h = height;
	for (int i = 0; i < mip_levels; i++) {
		regions[i].Offset = buffer_size;
		regions[i].Footprint.Format = texture->GetDesc().Format;
		regions[i].Footprint.Width = w;
		regions[i].Footprint.Height = h;
		regions[i].Footprint.Depth = 1;
		regions[i].Footprint.RowPitch = static_cast<UINT>(align(w * bytes_per_pixel, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
		buffer_size += align(regions[i].Footprint.RowPitch * h, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
		w >>= 1;
		if (w < 1) w = 1;
		h >>= 1;
		if (h < 1) h = 1;
	}

	//
	// Create upload upload texture.
	//
	ID3D12Resource* upload_texture;
	DX_CHECK(mDevice->CreateCommittedResource(
		&get_heap_properties(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&get_buffer_desc(buffer_size),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&upload_texture)));

	byte* upload_texture_data;
	DX_CHECK(upload_texture->Map(0, nullptr, reinterpret_cast<void**>(&upload_texture_data)));
	w = width;
	h = height;
	for (int i = 0; i < mip_levels; i++) {
		byte* upload_subresource_base = upload_texture_data + regions[i].Offset;
		for (int y = 0; y < h; y++) {
			Com_Memcpy(upload_subresource_base + regions[i].Footprint.RowPitch * y, pixels, w * bytes_per_pixel);
			pixels += w * bytes_per_pixel;
		}
		w >>= 1;
		if (w < 1) w = 1;
		h >>= 1;
		if (h < 1) h = 1;
	}
	upload_texture->Unmap(0, nullptr);

	//
	// Copy data from upload texture to destination texture.
	//
	record_and_run_commands([texture, upload_texture, &regions, mip_levels]
	(ID3D12GraphicsCommandList* command_list)
	{
		command_list->ResourceBarrier(1, &get_transition_barrier2(texture,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST));

		for (UINT i = 0; i < mip_levels; ++i) {
			D3D12_TEXTURE_COPY_LOCATION  dst;
			dst.pResource = texture;
			dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			dst.SubresourceIndex = i;

			D3D12_TEXTURE_COPY_LOCATION src;
			src.pResource = upload_texture;
			src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			src.PlacedFootprint = regions[i];

			command_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		}

		command_list->ResourceBarrier(1, &get_transition_barrier2(texture,
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	});

	upload_texture->Release();
}

void dx_renderTargets::CreateSamplerDescriptor(const Vk_Sampler_Def& def, Dx_Sampler_Index sampler_index)
{
	uint32_t min, mag, mip;

	if (def.gl_mag_filter == GL_NEAREST) {
		mag = 0;
	}
	else if (def.gl_mag_filter == GL_LINEAR) {
		mag = 1;
	}
	else {
		ri.Error(ERR_FATAL, "create_sampler_descriptor: invalid gl_mag_filter");
	}

	bool max_lod_0_25 = false; // used to emulate OpenGL's GL_LINEAR/GL_NEAREST minification filter
	if (def.gl_min_filter == GL_NEAREST) {
		min = 0;
		mip = 0;
		max_lod_0_25 = true;
	}
	else if (def.gl_min_filter == GL_LINEAR) {
		min = 1;
		mip = 0;
		max_lod_0_25 = true;
	}
	else if (def.gl_min_filter == GL_NEAREST_MIPMAP_NEAREST) {
		min = 0;
		mip = 0;
	}
	else if (def.gl_min_filter == GL_LINEAR_MIPMAP_NEAREST) {
		min = 1;
		mip = 0;
	}
	else if (def.gl_min_filter == GL_NEAREST_MIPMAP_LINEAR) {
		min = 0;
		mip = 1;
	}
	else if (def.gl_min_filter == GL_LINEAR_MIPMAP_LINEAR) {
		min = 1;
		mip = 1;
	}
	else {
		ri.Error(ERR_FATAL, "vk_find_sampler: invalid gl_min_filter");
	}

	D3D12_TEXTURE_ADDRESS_MODE address_mode = def.repeat_texture ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

	D3D12_SAMPLER_DESC sampler_desc;
	sampler_desc.Filter = D3D12_ENCODE_BASIC_FILTER(min, mag, mip, 0);
	sampler_desc.AddressU = address_mode;
	sampler_desc.AddressV = address_mode;
	sampler_desc.AddressW = address_mode;
	sampler_desc.MipLODBias = 0.0f;
	sampler_desc.MaxAnisotropy = 1;
	sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	sampler_desc.BorderColor[0] = 0.0f;
	sampler_desc.BorderColor[1] = 0.0f;
	sampler_desc.BorderColor[2] = 0.0f;
	sampler_desc.BorderColor[3] = 0.0f;
	sampler_desc.MinLOD = 0.0f;
	sampler_desc.MaxLOD = max_lod_0_25 ? 0.25f : 12.0f;

	D3D12_CPU_DESCRIPTOR_HANDLE sampler_handle = sampler_heap->GetCPUDescriptorHandleForHeapStart();
	sampler_handle.ptr += sampler_descriptor_size * sampler_index;

	mDevice->CreateSampler(&sampler_desc, sampler_handle);
}

D3D12_RESOURCE_BARRIER dx_renderTargets::SetRenderTargetTextureState(D3D12_RESOURCE_STATES afterState, Dx_RenderTarget_Index index)
{
	D3D12_RESOURCE_BARRIER transitionBarrier = get_transition_barrier2(mRenderTargetTexture[index],
		mRenderTargetTextureCurrentState[index], afterState);

	mRenderTargetTextureCurrentState[index] = afterState;

	return transitionBarrier;
}

bool dx_renderTargets::DotRenderTargetTextureState(D3D12_RESOURCE_STATES afterState, Dx_RenderTarget_Index index)
{
	return mRenderTargetTextureCurrentState[index] != afterState;
}

bool dx_renderTargets::DoDepthTextureState(D3D12_RESOURCE_STATES afterState, Dx_DepthTarget_Index index)
{
	return mDepthTextureCurrentState[index] != afterState;
}

D3D12_RESOURCE_BARRIER dx_renderTargets::SetDepthTextureState(D3D12_RESOURCE_STATES afterState, Dx_DepthTarget_Index index)
{

	D3D12_RESOURCE_BARRIER transitionBarrier = get_transition_barrier2(depth_stencil_buffer[index],
		mDepthTextureCurrentState[index], afterState);

	mDepthTextureCurrentState[index] = afterState;

	return transitionBarrier;
}