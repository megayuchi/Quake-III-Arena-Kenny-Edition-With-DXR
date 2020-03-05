#include "tr_local.h"
#include "../../engine/platform/win_local.h"

#include <chrono>


#ifdef ENABLE_DX12

#include "D3d12.h"
#include "DXGI1_4.h"
#include "dxr.h"
#include "dx_renderTargets.h"

#include <sstream>

#pragma comment (lib, "D3d12.lib")
#pragma comment (lib, "DXGI.lib")

const int VERTEX_CHUNK_SIZE = 512 * 1024;

const int XYZ_SIZE = 4 * VERTEX_CHUNK_SIZE;
const int NORMAL_SIZE = 4 * VERTEX_CHUNK_SIZE;
const int COLOR_SIZE = 1 * VERTEX_CHUNK_SIZE;
const int ST0_SIZE = 2 * VERTEX_CHUNK_SIZE;
const int ST1_SIZE = 2 * VERTEX_CHUNK_SIZE;

const int XYZ_OFFSET = 0;
const int NORMAL_OFFSET = XYZ_OFFSET + XYZ_SIZE;
const int COLOR_OFFSET = NORMAL_OFFSET + NORMAL_SIZE;
const int ST0_OFFSET = COLOR_OFFSET + COLOR_SIZE;
const int ST1_OFFSET = ST0_OFFSET + ST0_SIZE;

const int VERTEX_BUFFER_SIZE = XYZ_SIZE + NORMAL_SIZE + COLOR_SIZE + ST0_SIZE + ST1_SIZE;
const int INDEX_BUFFER_SIZE = 2 * 1024 * 1024;

#define DX_CHECK(function_call) { \
	HRESULT hr = function_call; \
	if (FAILED(hr)) \
		ri.Error(ERR_FATAL, "Direct3D: error returned by %s", #function_call); \
}



static void get_hardware_adapter(IDXGIFactory4* factory, IDXGIAdapter1** adapter)
{
#ifdef ENABLE_DXR

	// Create the device
	UINT adapter_index = 0;

	while (factory->EnumAdapters1(adapter_index++, adapter) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC1 adapterDesc;
		(*adapter)->GetDesc1(&adapterDesc);
		ID3D12Device5* dummyDevice = nullptr;
		if (SUCCEEDED(D3D12CreateDevice(*adapter, D3D_FEATURE_LEVEL_12_1, _uuidof(ID3D12Device5), (void**)&dummyDevice)))
		{
			// Check if the device supports ray tracing.
			D3D12_FEATURE_DATA_D3D12_OPTIONS5 features = {};
			HRESULT hr = dummyDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features, sizeof(features));
			SAFE_RELEASE(dummyDevice);
			dummyDevice = nullptr;
			if (FAILED(hr) || features.RaytracingTier < D3D12_RAYTRACING_TIER_1_0)
			{
				continue;
			}
			else
			{
				return;
			}
			//printf("Running on DXGI Adapter %S\n", adapterDesc.Description);
			break;
		}
	}
#else

	UINT adapter_index = 0;
	while (factory->EnumAdapters1(adapter_index++, adapter) != DXGI_ERROR_NOT_FOUND) {
		DXGI_ADAPTER_DESC1 desc;
		(*adapter)->GetDesc1(&desc);
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
			continue;
		}
		// check for 11_0 feature level support
		if (SUCCEEDED(D3D12CreateDevice(*adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) {
			return;
		}
	}
	*adapter = nullptr;
#endif
}



D3D12_RESOURCE_BARRIER get_transition_barrier(
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





ID3D12PipelineState* create_pipeline(const Vk_Pipeline_Def& def);

void dx_initialize()
{
#ifdef ENABLE_DXR
	dx.dxr = new DXRGlobal;
	Com_Memset(dx.dxr, 0, sizeof(DXRGlobal));
#endif

	// enable validation in debug configuration
#if defined(_DEBUG)
	ID3D12Debug* debug_controller;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
		debug_controller->EnableDebugLayer();
		debug_controller->Release();
	}
#endif

	

	IDXGIFactory4* factory;
	DX_CHECK(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

	// Create device.
	{
		IDXGIAdapter1* hardware_adapter;
		get_hardware_adapter(factory, &hardware_adapter);

		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
#ifdef ENABLE_DXR
		featureLevel = D3D_FEATURE_LEVEL_12_1;
#endif
		DX_CHECK(D3D12CreateDevice(hardware_adapter, featureLevel, IID_PPV_ARGS(&dx.device)));

#ifdef ENABLE_DXR
		// Check if the device supports ray tracing.
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 features = {};
		HRESULT hr = dx.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features, sizeof(features));
		if (FAILED(hr) || features.RaytracingTier < D3D12_RAYTRACING_TIER_1_0)
		{
			dx.device = nullptr;
			ri.Error(ERR_FATAL, "Direct3D: error returned by %s", ".RaytracingTier < D3D12_RAYTRACING_TIER_1_0");
		}
#endif


		hardware_adapter->Release();
	}

	// Create command queue.
	{
		D3D12_COMMAND_QUEUE_DESC queue_desc{};
		queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		DX_CHECK(dx.device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&dx.command_queue)));
	}

	dx.dx_renderTargets = dx_renderTargets::Setup(dx.device);

	//
	// Create swap chain.
	//
	{
		DXGI_SWAP_CHAIN_DESC1 swap_chain_desc{};
		swap_chain_desc.BufferCount = SWAPCHAIN_BUFFER_COUNT;
		swap_chain_desc.Width = glConfig.vidWidth;
		swap_chain_desc.Height = glConfig.vidHeight;
		swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swap_chain_desc.SampleDesc.Count = 1;

		IDXGISwapChain1* swapchain;
		DX_CHECK(factory->CreateSwapChainForHwnd(
			dx.command_queue,
			g_wv.hWnd_dx,
			&swap_chain_desc,
			nullptr,
			nullptr,
			&swapchain
		));

		DX_CHECK(factory->MakeWindowAssociation(g_wv.hWnd_dx, DXGI_MWA_NO_ALT_ENTER));
		swapchain->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&dx.swapchain);
		swapchain->Release();

		for (int i = 0; i < SWAPCHAIN_BUFFER_COUNT; i++) {
			DX_CHECK(dx.swapchain->GetBuffer(i, IID_PPV_ARGS(&dx.dx_renderTargets->render_targets[i])));
		}
	}

	factory->Release();
	factory = nullptr;

	//
	// Create command allocators and command list.
	//
	{
		DX_CHECK(dx.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&dx.command_allocator)));

		DX_CHECK(dx.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&dx.helper_command_allocator)));

		DX_CHECK(dx.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, dx.command_allocator, nullptr,
			IID_PPV_ARGS(&dx.command_list)));
		DX_CHECK(dx.command_list->Close());
	}

	//
	// Create synchronization objects.
	//
	{
		DX_CHECK(dx.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&dx.fence)));
		dx.fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	}

	//
	// Create descriptor heaps.
	//
	dx.dx_renderTargets->CreateDescriptorHeaps();

	//
	// Create descriptors.
	//
	dx.dx_renderTargets->CreateDescriptors();

	//
	// Create depth buffer resources.
	//
	dx.dx_renderTargets->CreateDepthBufferResources();

	//
	// Create root signature.
	//
	{
		D3D12_DESCRIPTOR_RANGE ranges[4] = {};
		ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[0].NumDescriptors = 1;
		ranges[0].BaseShaderRegister = 0;

		ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		ranges[1].NumDescriptors = 1;
		ranges[1].BaseShaderRegister = 0;

		ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[2].NumDescriptors = 1;
		ranges[2].BaseShaderRegister = 1;

		ranges[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		ranges[3].NumDescriptors = 1;
		ranges[3].BaseShaderRegister = 1;

		D3D12_ROOT_PARAMETER root_parameters[5]{};

		root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		root_parameters[0].Constants.ShaderRegister = 0;
		root_parameters[0].Constants.Num32BitValues = 32;
		root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		for (int i = 1; i < 5; i++) {
			root_parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			root_parameters[i].DescriptorTable.NumDescriptorRanges = 1;
			root_parameters[i].DescriptorTable.pDescriptorRanges = &ranges[i - 1];
			root_parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		}

		D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
		root_signature_desc.NumParameters = _countof(root_parameters);
		root_signature_desc.pParameters = root_parameters;
		root_signature_desc.NumStaticSamplers = 0;
		root_signature_desc.pStaticSamplers = nullptr;
		root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		dx.root_signature = Create_Root_Signature(root_signature_desc);
	}

	//
	// Geometry buffers.
	//
	{
		D3D12_HEAP_PROPERTIES heap_properties;
		heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
		heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heap_properties.CreationNodeMask = 1;
		heap_properties.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC buffer_desc;
		buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		buffer_desc.Alignment = 0;
		buffer_desc.Width = VERTEX_BUFFER_SIZE + INDEX_BUFFER_SIZE;
		buffer_desc.Height = 1;
		buffer_desc.DepthOrArraySize = 1;
		buffer_desc.MipLevels = 1;
		buffer_desc.Format = DXGI_FORMAT_UNKNOWN;
		buffer_desc.SampleDesc.Count = 1;
		buffer_desc.SampleDesc.Quality = 0;
		buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		buffer_desc.Flags = D3D12_RESOURCE_FLAG_NONE;


		// store geometry in upload heap since Q3 regenerates it every frame
		DX_CHECK(dx.device->CreateCommittedResource(
			&heap_properties,
			D3D12_HEAP_FLAG_NONE,
			&buffer_desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&dx.geometry_buffer)));

		void* p_data;
		D3D12_RANGE read_range{};
		DX_CHECK(dx.geometry_buffer->Map(0, &read_range, &p_data));

		dx.vertex_buffer_ptr = static_cast<byte*>(p_data);

		assert((VERTEX_BUFFER_SIZE & 0xffff) == 0); // index buffer offset should be 64K aligned.
		dx.index_buffer_ptr = static_cast<byte*>(p_data) + VERTEX_BUFFER_SIZE;
	}

	//
	// Standard pipelines.
	//
	{
		// skybox
		{
			Vk_Pipeline_Def def;
			def.shader_type = Vk_Shader_Type::single_texture;
			def.state_bits = 0;
			def.face_culling = CT_FRONT_SIDED;
			def.polygon_offset = false;
			def.clipping_plane = false;
			def.mirror = false;
			dx.skybox_pipeline = create_pipeline(def);
		}

		// Q3 stencil shadows
		{
			{
				Vk_Pipeline_Def def;
				def.polygon_offset = false;
				def.state_bits = 0;
				def.shader_type = Vk_Shader_Type::single_texture;
				def.clipping_plane = false;
				def.shadow_phase = Vk_Shadow_Phase::shadow_edges_rendering;

				cullType_t cull_types[2] = { CT_FRONT_SIDED, CT_BACK_SIDED };
				bool mirror_flags[2] = { false, true };

				for (int i = 0; i < 2; i++) {
					def.face_culling = cull_types[i];
					for (int j = 0; j < 2; j++) {
						def.mirror = mirror_flags[j];
						dx.shadow_volume_pipelines[i][j] = create_pipeline(def);
					}
				}
			}

			{
				Vk_Pipeline_Def def;
				def.face_culling = CT_FRONT_SIDED;
				def.polygon_offset = false;
				def.state_bits = GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO;
				def.shader_type = Vk_Shader_Type::single_texture;
				def.clipping_plane = false;
				def.mirror = false;
				def.shadow_phase = Vk_Shadow_Phase::fullscreen_quad_rendering;
				dx.shadow_finish_pipeline = create_pipeline(def);
			}
		}

		// fog and dlights
		{
			Vk_Pipeline_Def def;
			def.shader_type = Vk_Shader_Type::single_texture;
			def.clipping_plane = false;
			def.mirror = false;

			unsigned int fog_state_bits[2] = {
				GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL,
				GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA
			};
			unsigned int dlight_state_bits[2] = {
				GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL,
				GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL
			};
			bool polygon_offset[2] = { false, true };

			for (int i = 0; i < 2; i++) {
				unsigned fog_state = fog_state_bits[i];
				unsigned dlight_state = dlight_state_bits[i];

				for (int j = 0; j < 3; j++) {
					def.face_culling = j; // cullType_t value

					for (int k = 0; k < 2; k++) {
						def.polygon_offset = polygon_offset[k];

						def.state_bits = fog_state;
						dx.fog_pipelines[i][j][k] = create_pipeline(def);

						def.state_bits = dlight_state;
						dx.dlight_pipelines[i][j][k] = create_pipeline(def);
					}
				}
			}
		}

		// debug pipelines
		{
			Vk_Pipeline_Def def;
			def.state_bits = GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE;
			dx.tris_debug_pipeline = create_pipeline(def);
		}
		{
			Vk_Pipeline_Def def;
			def.state_bits = GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE;
			def.face_culling = CT_BACK_SIDED;
			dx.tris_mirror_debug_pipeline = create_pipeline(def);
		}
		{
			Vk_Pipeline_Def def;
			def.state_bits = GLS_DEPTHMASK_TRUE;
			def.line_primitives = true;
			dx.normals_debug_pipeline = create_pipeline(def);
		}
		{
			Vk_Pipeline_Def def;
			def.state_bits = GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
			dx.surface_debug_pipeline_solid = create_pipeline(def);
		}
		{
			Vk_Pipeline_Def def;
			def.state_bits = GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
			def.line_primitives = true;
			dx.surface_debug_pipeline_outline = create_pipeline(def);
		}
		{
			Vk_Pipeline_Def def;
			def.state_bits = GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
			dx.images_debug_pipeline = create_pipeline(def);
		}
	}

	dx.active = true;
}

/**
* Create a root signature.
*/
ID3D12RootSignature* Create_Root_Signature(const D3D12_ROOT_SIGNATURE_DESC& desc)
{
	HRESULT hr;
	ID3DBlob* sig;
	ID3DBlob* error;

	hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &error);
	if (FAILED(hr))
	{
		ri.Error(ERR_FATAL, "Error: failed to serialize root signature!");
	}

	ID3D12RootSignature* pRootSig;
	LPVOID buffer = sig->GetBufferPointer();
	SIZE_T bufferSize = sig->GetBufferSize();
	hr = dx.device->CreateRootSignature(0, buffer, bufferSize, IID_PPV_ARGS(&pRootSig));
	if (FAILED(hr))
	{
		ri.Error(ERR_FATAL, "Error: failed to create root signature!");
	}

	if (sig != nullptr)
		sig->Release();
	if (error != nullptr)
		error->Release();

	return pRootSig;
}

void dx_shutdown() {
	::CloseHandle(dx.fence_event);

#ifdef ENABLE_DXR
	DXR::Destroy(dx, *dx.dxr);
	delete dx.dxr;
#endif

	
	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < 2; j++) {
			dx.shadow_volume_pipelines[i][j]->Release();
		}
	}
	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < 3; j++) {
			for (int k = 0; k < 2; k++) {
				dx.fog_pipelines[i][j][k]->Release();
				dx.dlight_pipelines[i][j][k]->Release();
			}
		}
	}

	dx.swapchain->Release();
	dx.command_allocator->Release();
	dx.helper_command_allocator->Release();
	
	dx.root_signature->Release();
	dx.command_queue->Release();
	dx.command_list->Release();
	dx.fence->Release();
	
	dx.geometry_buffer->Release();
	dx.skybox_pipeline->Release();
	dx.shadow_finish_pipeline->Release();
	dx.tris_debug_pipeline->Release();
	dx.tris_mirror_debug_pipeline->Release();
	dx.normals_debug_pipeline->Release();
	dx.surface_debug_pipeline_solid->Release();
	dx.surface_debug_pipeline_outline->Release();
	dx.images_debug_pipeline->Release();

	dx.dx_renderTargets->Shutdown();
	delete dx.dx_renderTargets;



	dx.device->Release();

	Com_Memset(&dx, 0, sizeof(dx));
}

void dx_release_resources() {
	dx_wait_device_idle();

	dx_world.pipeline_create_time = 0.0f;
	for (int i = 0; i < dx_world.num_pipelines; i++) {
		dx_world.pipelines[i]->Release();
	}

	for (int i = 0; i < MAX_VK_IMAGES; i++) {
		if (dx_world.images[i].texture != nullptr) {
			dx_world.images[i].texture->Release();
		}
	}

	Com_Memset(&dx_world, 0, sizeof(dx_world));

	// Reset geometry buffer's current offsets.
	dx.xyz_elements = 0;
	dx.color_st_elements = 0;
	dx.index_buffer_offset = 0;


}

void dx_wait_device_idle() {
	dx.fence_value++;
	DX_CHECK(dx.command_queue->Signal(dx.fence, dx.fence_value));
	DX_CHECK(dx.fence->SetEventOnCompletion(dx.fence_value, dx.fence_event));
	WaitForSingleObject(dx.fence_event, INFINITE);
}



static ID3D12PipelineState* create_pipeline(const Vk_Pipeline_Def& def) {

	//NOTE run compile_hlsl.bat

	// single texture VS
	extern unsigned char single_texture_vs[];
	extern long long single_texture_vs_size;

	extern unsigned char single_texture_clipping_plane_vs[];
	extern long long single_texture_clipping_plane_vs_size;

	// multi texture VS
	extern unsigned char multi_texture_vs[];
	extern long long multi_texture_vs_size;

	extern unsigned char multi_texture_clipping_plane_vs[];
	extern long long multi_texture_clipping_plane_vs_size;

	// single texture PS
	extern unsigned char single_texture_ps[];
	extern long long single_texture_ps_size;

	extern unsigned char single_texture_gt0_ps[];
	extern long long single_texture_gt0_ps_size;

	extern unsigned char single_texture_lt80_ps[];
	extern long long single_texture_lt80_ps_size;

	extern unsigned char single_texture_ge80_ps[];
	extern long long single_texture_ge80_ps_size;

	// multi texture mul PS
	extern unsigned char multi_texture_mul_ps[];
	extern long long multi_texture_mul_ps_size;

	extern unsigned char multi_texture_mul_gt0_ps[];
	extern long long multi_texture_mul_gt0_ps_size;

	extern unsigned char multi_texture_mul_lt80_ps[];
	extern long long multi_texture_mul_lt80_ps_size;

	extern unsigned char multi_texture_mul_ge80_ps[];
	extern long long multi_texture_mul_ge80_ps_size;

	// multi texture add PS
	extern unsigned char multi_texture_add_ps[];
	extern long long multi_texture_add_ps_size;

	extern unsigned char multi_texture_add_gt0_ps[];
	extern long long multi_texture_add_gt0_ps_size;

	extern unsigned char multi_texture_add_lt80_ps[];
	extern long long multi_texture_add_lt80_ps_size;

	extern unsigned char multi_texture_add_ge80_ps[];
	extern long long multi_texture_add_ge80_ps_size;

#define BYTECODE(name) D3D12_SHADER_BYTECODE{name, (SIZE_T)name##_size}

#define GET_PS_BYTECODE(base_name) \
	if ((def.state_bits & GLS_ATEST_BITS) == 0) \
		ps_bytecode = BYTECODE(base_name##_ps); \
	else if (def.state_bits & GLS_ATEST_GT_0) \
		ps_bytecode = BYTECODE(base_name##_gt0_ps); \
	else if (def.state_bits & GLS_ATEST_LT_80) \
		ps_bytecode = BYTECODE(base_name##_lt80_ps); \
	else if (def.state_bits & GLS_ATEST_GE_80) \
		ps_bytecode = BYTECODE(base_name##_ge80_ps); \
	else \
		ri.Error(ERR_DROP, "create_pipeline: invalid alpha test state bits\n");

	D3D12_SHADER_BYTECODE vs_bytecode;
	D3D12_SHADER_BYTECODE ps_bytecode;
	if (def.shader_type == Vk_Shader_Type::single_texture) {
		if (def.clipping_plane) {
			vs_bytecode = BYTECODE(single_texture_clipping_plane_vs);
		}
		else {
			vs_bytecode = BYTECODE(single_texture_vs);
		}
		GET_PS_BYTECODE(single_texture)
	}
	else if (def.shader_type == Vk_Shader_Type::multi_texture_mul) {
		if (def.clipping_plane) {
			vs_bytecode = BYTECODE(multi_texture_clipping_plane_vs);
		}
		else {
			vs_bytecode = BYTECODE(multi_texture_vs);
		}
		GET_PS_BYTECODE(multi_texture_mul)
	}
	else if (def.shader_type == Vk_Shader_Type::multi_texture_add) {
		if (def.clipping_plane) {
			vs_bytecode = BYTECODE(multi_texture_clipping_plane_vs);
		}
		else {
			vs_bytecode = BYTECODE(multi_texture_vs);
		}
		GET_PS_BYTECODE(multi_texture_add)
	}

#undef GET_PS_BYTECODE
#undef BYTECODE

	// Vertex elements.
	D3D12_INPUT_ELEMENT_DESC input_element_desc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 2, 0,		D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 3, 0,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 4, 0,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	//
	// Blend.
	//
	D3D12_BLEND_DESC blend_state;
	blend_state.AlphaToCoverageEnable = FALSE;
	blend_state.IndependentBlendEnable = FALSE;
	auto& rt_blend_desc = blend_state.RenderTarget[0];
	rt_blend_desc.BlendEnable = (def.state_bits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) ? TRUE : FALSE;
	rt_blend_desc.LogicOpEnable = FALSE;

	if (rt_blend_desc.BlendEnable) {
		switch (def.state_bits & GLS_SRCBLEND_BITS) {
		case GLS_SRCBLEND_ZERO:
			rt_blend_desc.SrcBlend = D3D12_BLEND_ZERO;
			rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_ZERO;
			break;
		case GLS_SRCBLEND_ONE:
			rt_blend_desc.SrcBlend = D3D12_BLEND_ONE;
			rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_ONE;
			break;
		case GLS_SRCBLEND_DST_COLOR:
			rt_blend_desc.SrcBlend = D3D12_BLEND_DEST_COLOR;
			rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_DEST_ALPHA;
			break;
		case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
			rt_blend_desc.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
			rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_INV_DEST_ALPHA;
			break;
		case GLS_SRCBLEND_SRC_ALPHA:
			rt_blend_desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
			break;
		case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
			rt_blend_desc.SrcBlend = D3D12_BLEND_INV_SRC_ALPHA;
			rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
			break;
		case GLS_SRCBLEND_DST_ALPHA:
			rt_blend_desc.SrcBlend = D3D12_BLEND_DEST_ALPHA;
			rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_DEST_ALPHA;
			break;
		case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
			rt_blend_desc.SrcBlend = D3D12_BLEND_INV_DEST_ALPHA;
			rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_INV_DEST_ALPHA;
			break;
		case GLS_SRCBLEND_ALPHA_SATURATE:
			rt_blend_desc.SrcBlend = D3D12_BLEND_SRC_ALPHA_SAT;
			rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA_SAT;
			break;
		default:
			ri.Error(ERR_DROP, "create_pipeline: invalid src blend state bits\n");
			break;
		}
		switch (def.state_bits & GLS_DSTBLEND_BITS) {
		case GLS_DSTBLEND_ZERO:
			rt_blend_desc.DestBlend = D3D12_BLEND_ZERO;
			rt_blend_desc.DestBlendAlpha = D3D12_BLEND_ZERO;
			break;
		case GLS_DSTBLEND_ONE:
			rt_blend_desc.DestBlend = D3D12_BLEND_ONE;
			rt_blend_desc.DestBlendAlpha = D3D12_BLEND_ONE;
			break;
		case GLS_DSTBLEND_SRC_COLOR:
			rt_blend_desc.DestBlend = D3D12_BLEND_SRC_COLOR;
			rt_blend_desc.DestBlendAlpha = D3D12_BLEND_SRC_ALPHA;
			break;
		case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
			rt_blend_desc.DestBlend = D3D12_BLEND_INV_SRC_COLOR;
			rt_blend_desc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
			break;
		case GLS_DSTBLEND_SRC_ALPHA:
			rt_blend_desc.DestBlend = D3D12_BLEND_SRC_ALPHA;
			rt_blend_desc.DestBlendAlpha = D3D12_BLEND_SRC_ALPHA;
			break;
		case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
			rt_blend_desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			rt_blend_desc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
			break;
		case GLS_DSTBLEND_DST_ALPHA:
			rt_blend_desc.DestBlend = D3D12_BLEND_DEST_ALPHA;
			rt_blend_desc.DestBlendAlpha = D3D12_BLEND_DEST_ALPHA;
			break;
		case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
			rt_blend_desc.DestBlend = D3D12_BLEND_INV_DEST_ALPHA;
			rt_blend_desc.DestBlendAlpha = D3D12_BLEND_INV_DEST_ALPHA;
			break;
		default:
			ri.Error(ERR_DROP, "create_pipeline: invalid dst blend state bits\n");
			break;
		}
	}
	rt_blend_desc.BlendOp = D3D12_BLEND_OP_ADD;
	rt_blend_desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rt_blend_desc.LogicOp = D3D12_LOGIC_OP_COPY;
	rt_blend_desc.RenderTargetWriteMask = (def.shadow_phase == Vk_Shadow_Phase::shadow_edges_rendering) ? 0 : D3D12_COLOR_WRITE_ENABLE_ALL;

	//
	// Rasteriazation state.
	//
	D3D12_RASTERIZER_DESC rasterization_state = {};
	rasterization_state.FillMode = (def.state_bits & GLS_POLYMODE_LINE) ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;

	if (def.face_culling == CT_TWO_SIDED)
		rasterization_state.CullMode = D3D12_CULL_MODE_NONE;
	else if (def.face_culling == CT_FRONT_SIDED)
		rasterization_state.CullMode = (def.mirror ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_BACK);
	else if (def.face_culling == CT_BACK_SIDED)
		rasterization_state.CullMode = (def.mirror ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_FRONT);
	else
		ri.Error(ERR_DROP, "create_pipeline: invalid face culling mode\n");

	rasterization_state.FrontCounterClockwise = FALSE; // Q3 defaults to clockwise vertex order
	rasterization_state.DepthBias = def.polygon_offset ? r_offsetUnits->integer : 0;
	rasterization_state.DepthBiasClamp = 0.0f;
	rasterization_state.SlopeScaledDepthBias = def.polygon_offset ? r_offsetFactor->value : 0.0f;
	rasterization_state.DepthClipEnable = TRUE;
	rasterization_state.MultisampleEnable = FALSE;
	rasterization_state.AntialiasedLineEnable = FALSE;
	rasterization_state.ForcedSampleCount = 0;
	rasterization_state.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	//
	// Depth/stencil state.
	//
	D3D12_DEPTH_STENCIL_DESC depth_stencil_state = {};
	depth_stencil_state.DepthEnable = (def.state_bits & GLS_DEPTHTEST_DISABLE) ? FALSE : TRUE;
	depth_stencil_state.DepthWriteMask = (def.state_bits & GLS_DEPTHMASK_TRUE) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	depth_stencil_state.DepthFunc = (def.state_bits & GLS_DEPTHFUNC_EQUAL) ? D3D12_COMPARISON_FUNC_EQUAL : D3D12_COMPARISON_FUNC_LESS_EQUAL;
	depth_stencil_state.StencilEnable = (def.shadow_phase != Vk_Shadow_Phase::disabled) ? TRUE : FALSE;
	depth_stencil_state.StencilReadMask = 255;
	depth_stencil_state.StencilWriteMask = 255;

	if (def.shadow_phase == Vk_Shadow_Phase::shadow_edges_rendering) {
		depth_stencil_state.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		depth_stencil_state.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		depth_stencil_state.FrontFace.StencilPassOp = (def.face_culling == CT_FRONT_SIDED) ? D3D12_STENCIL_OP_INCR_SAT : D3D12_STENCIL_OP_DECR_SAT;
		depth_stencil_state.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

		depth_stencil_state.BackFace = depth_stencil_state.FrontFace;
	}
	else if (def.shadow_phase == Vk_Shadow_Phase::fullscreen_quad_rendering) {
		depth_stencil_state.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		depth_stencil_state.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		depth_stencil_state.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		depth_stencil_state.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_NOT_EQUAL;

		depth_stencil_state.BackFace = depth_stencil_state.FrontFace;
	}
	else {
		depth_stencil_state.FrontFace = {};
		depth_stencil_state.BackFace = {};
	}

	//
	// Create pipeline state.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
	pipeline_desc.pRootSignature = dx.root_signature;
	pipeline_desc.VS = vs_bytecode;
	pipeline_desc.PS = ps_bytecode;
	pipeline_desc.BlendState = blend_state;
	pipeline_desc.SampleMask = UINT_MAX;
	pipeline_desc.RasterizerState = rasterization_state;
	pipeline_desc.DepthStencilState = depth_stencil_state;
	pipeline_desc.InputLayout = { input_element_desc, def.shader_type == Vk_Shader_Type::single_texture ? 4u : 5u };
	pipeline_desc.PrimitiveTopologyType = def.line_primitives ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipeline_desc.NumRenderTargets = 1;
	pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pipeline_desc.DSVFormat = dx_renderTargets::get_depth_format();
	pipeline_desc.SampleDesc.Count = 1;
	pipeline_desc.SampleDesc.Quality = 0;

	ID3D12PipelineState* pipeline;
	DX_CHECK(dx.device->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline)));
	return pipeline;
}

struct Timer {
	using Clock = std::chrono::high_resolution_clock;
	using Second = std::chrono::duration<double, std::ratio<1>>;

	Clock::time_point start = Clock::now();
	double elapsed_seconds() const {
		const auto duration = Clock::now() - start;
		double seconds = std::chrono::duration_cast<Second>(duration).count();
		return seconds;
	}
};

Dx_Image dx_create_image(int width, int height, Dx_Image_Format format, int mip_levels, bool repeat_texture, int image_index)
{
	return dx.dx_renderTargets->CreateImage(width, height, format, mip_levels, repeat_texture, image_index);
}

void dx_upload_image_data(ID3D12Resource* texture, int width, int height, int mip_levels, const uint8_t* pixels, int bytes_per_pixel)
{
	dx.dx_renderTargets->UploadImageData(texture, width, height, mip_levels, pixels, bytes_per_pixel);
}

void dx_create_sampler_descriptor(const Vk_Sampler_Def& def, Dx_Sampler_Index sampler_index)
{
	dx.dx_renderTargets->CreateSamplerDescriptor(def, sampler_index);
}

ID3D12PipelineState* dx_find_pipeline(const Vk_Pipeline_Def& def) {
	for (int i = 0; i < dx_world.num_pipelines; i++) {
		const auto& cur_def = dx_world.pipeline_defs[i];

		if (cur_def.shader_type == def.shader_type &&
			cur_def.state_bits == def.state_bits &&
			cur_def.face_culling == def.face_culling &&
			cur_def.polygon_offset == def.polygon_offset &&
			cur_def.clipping_plane == def.clipping_plane &&
			cur_def.mirror == def.mirror &&
			cur_def.line_primitives == def.line_primitives &&
			cur_def.shadow_phase == def.shadow_phase)
		{
			return dx_world.pipelines[i];
		}
	}

	if (dx_world.num_pipelines >= MAX_VK_PIPELINES) {
		ri.Error(ERR_DROP, "dx_find_pipeline: MAX_VK_PIPELINES hit\n");
	}

	Timer t;
	ID3D12PipelineState* pipeline = create_pipeline(def);
	dx_world.pipeline_create_time += t.elapsed_seconds();

	dx_world.pipeline_defs[dx_world.num_pipelines] = def;
	dx_world.pipelines[dx_world.num_pipelines] = pipeline;
	dx_world.num_pipelines++;
	return pipeline;
}

static void get_mvp_transform(float* mvp) {
	if (backEnd.projection2D) {
		float mvp0 = 2.0f / glConfig.vidWidth;
		float mvp5 = 2.0f / glConfig.vidHeight;

		mvp[0] = mvp0; mvp[1] = 0.0f; mvp[2] = 0.0f; mvp[3] = 0.0f;
		mvp[4] = 0.0f; mvp[5] = -mvp5; mvp[6] = 0.0f; mvp[7] = 0.0f;
		mvp[8] = 0.0f; mvp[9] = 0.0f; mvp[10] = 1.0f; mvp[11] = 0.0f;
		mvp[12] = -1.0f; mvp[13] = 1.0f; mvp[14] = 0.0f; mvp[15] = 1.0f;

	}
	else {
		const float* p = backEnd.viewParms.projectionMatrix;

		// update q3's proj matrix (opengl) to d3d conventions: z - [0, 1] instead of [-1, 1]
		float zNear = r_znear->value;
		float zFar = backEnd.viewParms.zFar;
		float P10 = -zFar / (zFar - zNear);
		float P14 = -zFar * zNear / (zFar - zNear);

		float proj[16] = {
			p[0],  p[1],  p[2], p[3],
			p[4],  p[5],  p[6], p[7],
			p[8],  p[9],  P10,  p[11],
			p[12], p[13], P14,  p[15]
		};

		myGlMultMatrix(dx_world.modelview_transform, proj, mvp);
	}
}

static D3D12_RECT get_viewport_rect() {
	D3D12_RECT r;
	if (backEnd.projection2D) {
		r.left = 0.0f;
		r.top = 0.0f;
		r.right = glConfig.vidWidth;
		r.bottom = glConfig.vidHeight;
	}
	else {
		r.left = backEnd.viewParms.viewportX;
		r.top = glConfig.vidHeight - (backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight);
		r.right = r.left + backEnd.viewParms.viewportWidth;
		r.bottom = r.top + backEnd.viewParms.viewportHeight;
	}
	return r;
}

static D3D12_VIEWPORT get_viewport(Vk_Depth_Range depth_range) {
	D3D12_RECT r = get_viewport_rect();

	D3D12_VIEWPORT viewport;
	viewport.TopLeftX = (float)r.left;
	viewport.TopLeftY = (float)r.top;
	viewport.Width = (float)(r.right - r.left);
	viewport.Height = (float)(r.bottom - r.top);

	if (depth_range == Vk_Depth_Range::force_zero) {
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 0.0f;
	}
	else if (depth_range == Vk_Depth_Range::force_one) {
		viewport.MinDepth = 1.0f;
		viewport.MaxDepth = 1.0f;
	}
	else if (depth_range == Vk_Depth_Range::weapon) {
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 0.3f;
	}
	else {
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
	}
	return viewport;
}

static D3D12_RECT get_scissor_rect() {
	D3D12_RECT r = get_viewport_rect();

	if (r.left < 0)
		r.left = 0;
	if (r.top < 0)
		r.top = 0;

	if (r.right > glConfig.vidWidth)
		r.right = glConfig.vidWidth;
	if (r.bottom > glConfig.vidHeight)
		r.bottom = glConfig.vidHeight;

	return r;
}

void dx_clear_attachments(bool clear_depth_stencil, bool clear_color, vec4_t color) {
	if (!dx.active)
		return;

	if (!clear_depth_stencil && !clear_color)
		return;

	D3D12_RECT clear_rect = get_scissor_rect();

	if (clear_depth_stencil) {
		D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAG_DEPTH;
		if (r_shadows->integer == 2)
			flags |= D3D12_CLEAR_FLAG_STENCIL;

		D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = dx.dx_renderTargets->dsv_heap->GetCPUDescriptorHandleForHeapStart();
		dx.command_list->ClearDepthStencilView(dsv_handle, flags, 1.0f, 0, 1, &clear_rect);
	}

	if (clear_color) {
		/*D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = dx.dx_renderTargets->rtv_heap->GetCPUDescriptorHandleForHeapStart();
		rtv_handle.ptr += dx.frame_index * dx.dx_renderTargets->rtv_descriptor_size;*/

		D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = dx.dx_renderTargets->GetBackBuffer_rtv_handles(dx.frame_index);
		dx.command_list->ClearRenderTargetView(rtv_handle, color, 1, &clear_rect);
	}
}

void drx_Reset()
{
	dxr_acceleration_structure_manager& drxAsm = dx.dxr->acceleration_structure_manager;
	drxAsm.Reset();
}

void drx_AddBottomLevelMeshForWorld()
{
	dxr_acceleration_structure_manager& drxAsm = dx.dxr->acceleration_structure_manager;

	if (drxAsm.MeshCount() <= 0)
	{
		drxAsm.AddMesh(dxr_acceleration_structure_manager::STATIC_MESH);
	}
}

int drx_AddBottomLevelMesh(dxr_acceleration_structure_manager::meshType_t meshType)
{
	dxr_acceleration_structure_manager& drxAsm = dx.dxr->acceleration_structure_manager;
	return drxAsm.AddMesh(meshType);
}

void drx_AddBottomLevelMeshData(unsigned *indices, float* points, int numIndices, int numPoints, vec3_t normal)
{
	dxr_acceleration_structure_manager& drxAsm = dx.dxr->acceleration_structure_manager;

	drxAsm.AddIndexesData(indices, numIndices);
	drxAsm.AddVertexData(points, normal, numPoints, VERTEXSIZE);
}

void drx_AddBottomLeveIndexesData(unsigned *indices, int numIndices)
{
	dxr_acceleration_structure_manager& drxAsm = dx.dxr->acceleration_structure_manager;

	drxAsm.AddIndexesData(indices, numIndices);
}

void drx_AddBottomLevelIndex(unsigned index)
{
	dxr_acceleration_structure_manager& drxAsm = dx.dxr->acceleration_structure_manager;

	drxAsm.AddIndex(index);
}

void drx_AddBottomLevelVertex(float	*xyz, float* normal)
{
	dxr_acceleration_structure_manager& drxAsm = dx.dxr->acceleration_structure_manager;

	drxAsm.AddVertexData(xyz, normal);
}

void drx_UpdateBottomLevelVertex(int bottomLevelIndex, float *xyz, float* normal)
{
	dxr_acceleration_structure_manager& drxAsm = dx.dxr->acceleration_structure_manager;

	drxAsm.UpdateVertexData(bottomLevelIndex, xyz, normal);
}

void drx_AddTopLevelIndex(int bottomLevelIndex)
{
	dxr_acceleration_structure_manager& drxAsm = dx.dxr->acceleration_structure_manager;
	if (bottomLevelIndex != -1)
	{
		drxAsm.AddInstance(bottomLevelIndex);
	}
}

void drx_AddTopLevelIndexWithTransform(int bottomLevelIndex, vec3_t axis[3], float origin[3])
{
	dxr_acceleration_structure_manager& drxAsm = dx.dxr->acceleration_structure_manager;
	if (bottomLevelIndex != -1)
	{
		drxAsm.AddInstanceWithTransform(bottomLevelIndex, axis, origin);
	}
}

void drx_ResetMeshForUpdating(int bottomLevelIndex)
{
	dxr_acceleration_structure_manager& drxAsm = dx.dxr->acceleration_structure_manager;
	if (bottomLevelIndex != -1)
	{
		drxAsm.ResetMeshForUpdating(bottomLevelIndex);
	}
}

void dxr_AddWorldSurfaceGeometry(unsigned *indices, float* points, int numIndices, int numPoints, vec3_t normal)
{
#if defined( ENABLE_DXR)

	dxr_acceleration_structure_manager& drxAsm = dx.dxr->acceleration_structure_manager;

	if (drxAsm.MeshCount() <= 0)
	{
		drxAsm.AddMesh(dxr_acceleration_structure_manager::STATIC_MESH);
	}
	drxAsm.AddIndexesData(indices, numIndices);
	drxAsm.AddVertexData(points, normal, numPoints, VERTEXSIZE);
#endif
}

UINT dxr_MeshVertexCount(int bottomLevelIndex)
{
	dxr_acceleration_structure_manager& drxAsm = dx.dxr->acceleration_structure_manager;

	UINT meshVertexCount = drxAsm.MeshVertexCount(bottomLevelIndex);
	return meshVertexCount;


}
void dx_bind_geometry()
{	
	// xyz stream
	{
		if ((dx.xyz_elements + tess.numVertexes) * sizeof(vec4_t) > XYZ_SIZE)
			ri.Error(ERR_DROP, "dx_bind_geometry: vertex buffer overflow (xyz)\n");

		{
			byte* dst = dx.vertex_buffer_ptr + XYZ_OFFSET + dx.xyz_elements * sizeof(vec4_t);
			Com_Memcpy(dst, tess.xyz, tess.numVertexes * sizeof(vec4_t));
		}
		{
			byte* dst = dx.vertex_buffer_ptr + NORMAL_OFFSET + dx.xyz_elements * sizeof(vec4_t);
			Com_Memcpy(dst, tess.normal, tess.numVertexes * sizeof(vec4_t));
		}

		uint32_t xyz_offset = XYZ_OFFSET + dx.xyz_elements * sizeof(vec4_t);

		D3D12_VERTEX_BUFFER_VIEW xyz_view[2];
		xyz_view[0].BufferLocation = dx.geometry_buffer->GetGPUVirtualAddress() + xyz_offset;
		xyz_view[0].SizeInBytes = static_cast<UINT>(tess.numVertexes * sizeof(vec4_t));
		xyz_view[0].StrideInBytes = static_cast<UINT>(sizeof(vec4_t));

		xyz_view[1].BufferLocation = dx.geometry_buffer->GetGPUVirtualAddress() + NORMAL_OFFSET + dx.xyz_elements * sizeof(vec4_t);
		xyz_view[1].SizeInBytes = static_cast<UINT>(tess.numVertexes * sizeof(vec4_t));
		xyz_view[1].StrideInBytes = static_cast<UINT>(sizeof(vec4_t));

		dx.command_list->IASetVertexBuffers(0, 2, xyz_view);

		dx.xyz_elements += tess.numVertexes;
	}

	// indexes stream
	{
		std::size_t indexes_size = tess.numIndexes * sizeof(uint32_t);

		if (dx.index_buffer_offset + indexes_size > INDEX_BUFFER_SIZE)
			ri.Error(ERR_DROP, "dx_bind_geometry: index buffer overflow\n");

		byte* dst = dx.index_buffer_ptr + dx.index_buffer_offset;
		Com_Memcpy(dst, tess.indexes, indexes_size);

		D3D12_INDEX_BUFFER_VIEW index_view;
		index_view.BufferLocation = dx.geometry_buffer->GetGPUVirtualAddress() + VERTEX_BUFFER_SIZE + dx.index_buffer_offset;
		index_view.SizeInBytes = static_cast<UINT>(indexes_size);
		index_view.Format = DXGI_FORMAT_R32_UINT;
		dx.command_list->IASetIndexBuffer(&index_view);

		dx.index_buffer_offset += static_cast<int>(indexes_size);
	}

	//
	// Specify push constants.
	//
	float root_constants[16 + 16 + 12 + 4]; // mvp transform + world+ eye transform + clipping plane in eye space

	get_mvp_transform(root_constants);
	int root_constant_count = 16;

	for (int i = 0; i < 16; ++i)
	{
		root_constants[i + root_constant_count] = dx_world.model_transform[i];
	}
	root_constant_count = 32;

	if (backEnd.viewParms.isPortal)
	{
		// Eye space transform.
		// NOTE: backEnd.or.modelMatrix incorporates s_flipMatrix, so it should be taken into account
		// when computing clipping plane too.
		float* eye_xform = root_constants + root_constant_count;
		for (int i = 0; i < 12; i++) {
			eye_xform[i] = backEnd. or .modelViewMatrix[(i % 4) * 4 + i / 4];
		}

		// Clipping plane in eye coordinates.
		float world_plane[4];
		world_plane[0] = backEnd.viewParms.portalPlane.normal[0];
		world_plane[1] = backEnd.viewParms.portalPlane.normal[1];
		world_plane[2] = backEnd.viewParms.portalPlane.normal[2];
		world_plane[3] = backEnd.viewParms.portalPlane.dist;

		float eye_plane[4];
		eye_plane[0] = DotProduct(backEnd.viewParms. or .axis[0], world_plane);
		eye_plane[1] = DotProduct(backEnd.viewParms. or .axis[1], world_plane);
		eye_plane[2] = DotProduct(backEnd.viewParms. or .axis[2], world_plane);
		eye_plane[3] = DotProduct(world_plane, backEnd.viewParms. or .origin) - world_plane[3];

		// Apply s_flipMatrix to be in the same coordinate system as eye_xfrom.
		root_constants[28] = -eye_plane[1];
		root_constants[29] = eye_plane[2];
		root_constants[30] = -eye_plane[0];
		root_constants[31] = eye_plane[3];

		root_constant_count += 16;
	}
	dx.command_list->SetGraphicsRoot32BitConstants(0, root_constant_count, root_constants, 0);

}

void dx_shade_geometry(ID3D12PipelineState* pipeline, bool multitexture, Vk_Depth_Range depth_range, bool indexed, bool lines)
{	
	// color
	{
		if ((dx.color_st_elements + tess.numVertexes) * sizeof(color4ub_t) > COLOR_SIZE)
			ri.Error(ERR_DROP, "vulkan: vertex buffer overflow (color)\n");

		byte* dst = dx.vertex_buffer_ptr + COLOR_OFFSET + dx.color_st_elements * sizeof(color4ub_t);
		Com_Memcpy(dst, tess.svars.colors, tess.numVertexes * sizeof(color4ub_t));
	}
	// st0
	{
		if ((dx.color_st_elements + tess.numVertexes) * sizeof(vec2_t) > ST0_SIZE)
			ri.Error(ERR_DROP, "vulkan: vertex buffer overflow (st0)\n");

		byte* dst = dx.vertex_buffer_ptr + ST0_OFFSET + dx.color_st_elements * sizeof(vec2_t);
		Com_Memcpy(dst, tess.svars.texcoords[0], tess.numVertexes * sizeof(vec2_t));
	}
	// st1
	if (multitexture) {
		if ((dx.color_st_elements + tess.numVertexes) * sizeof(vec2_t) > ST1_SIZE)
			ri.Error(ERR_DROP, "vulkan: vertex buffer overflow (st1)\n");

		byte* dst = dx.vertex_buffer_ptr + ST1_OFFSET + dx.color_st_elements * sizeof(vec2_t);
		Com_Memcpy(dst, tess.svars.texcoords[1], tess.numVertexes * sizeof(vec2_t));
	}

	//
	// Configure vertex data stream.
	//
	D3D12_VERTEX_BUFFER_VIEW color_st_views[3];
	color_st_views[0].BufferLocation = dx.geometry_buffer->GetGPUVirtualAddress() + COLOR_OFFSET + dx.color_st_elements * sizeof(color4ub_t);
	color_st_views[0].SizeInBytes = static_cast<UINT>(tess.numVertexes * sizeof(color4ub_t));
	color_st_views[0].StrideInBytes = static_cast<UINT>(sizeof(color4ub_t));

	color_st_views[1].BufferLocation = dx.geometry_buffer->GetGPUVirtualAddress() + ST0_OFFSET + dx.color_st_elements * sizeof(vec2_t);
	color_st_views[1].SizeInBytes = static_cast<UINT>(tess.numVertexes * sizeof(vec2_t));
	color_st_views[1].StrideInBytes = static_cast<UINT>(sizeof(vec2_t));

	color_st_views[2].BufferLocation = dx.geometry_buffer->GetGPUVirtualAddress() + ST1_OFFSET + dx.color_st_elements * sizeof(vec2_t);
	color_st_views[2].SizeInBytes = static_cast<UINT>(tess.numVertexes * sizeof(vec2_t));
	color_st_views[2].StrideInBytes = static_cast<UINT>(sizeof(vec2_t));

	dx.command_list->IASetVertexBuffers(2, multitexture ? 3 : 2, color_st_views);
	dx.color_st_elements += tess.numVertexes;

	//
	// Set descriptor tables.
	//
	{
		int textureIndex = dx_world.current_image_indices[0];

		D3D12_GPU_DESCRIPTOR_HANDLE srv_handle = dx.dx_renderTargets->GetSrvHandle(textureIndex);
		dx.command_list->SetGraphicsRootDescriptorTable(1, srv_handle);
		
		const Dx_Sampler_Index sampler_index = dx_world.images[textureIndex].sampler_index;
		D3D12_GPU_DESCRIPTOR_HANDLE sampler_handle = dx.dx_renderTargets->GetSamplerHandle(sampler_index);

		dx.command_list->SetGraphicsRootDescriptorTable(2, sampler_handle);
	}

	if (multitexture)
	{
		int textureIndex = dx_world.current_image_indices[1];
		D3D12_GPU_DESCRIPTOR_HANDLE srv_handle = dx.dx_renderTargets->GetSrvHandle(textureIndex);
		dx.command_list->SetGraphicsRootDescriptorTable(3, srv_handle);
		
		const Dx_Sampler_Index sampler_index = dx_world.images[textureIndex].sampler_index;
		D3D12_GPU_DESCRIPTOR_HANDLE sampler_handle = dx.dx_renderTargets->GetSamplerHandle(sampler_index);

		dx.command_list->SetGraphicsRootDescriptorTable(4, sampler_handle);
	}

	//
	// Configure pipeline.
	//
	dx.command_list->SetPipelineState(pipeline);
	dx.command_list->IASetPrimitiveTopology(lines ? D3D10_PRIMITIVE_TOPOLOGY_LINELIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3D12_RECT scissor_rect = get_scissor_rect();
	dx.command_list->RSSetScissorRects(1, &scissor_rect);

	D3D12_VIEWPORT viewport = get_viewport(depth_range);
	dx.command_list->RSSetViewports(1, &viewport);

	//
	// Draw.
	//
	if (indexed)
		dx.command_list->DrawIndexedInstanced(tess.numIndexes, 1, 0, 0, 0);
	else
		dx.command_list->DrawInstanced(tess.numVertexes, 1, 0, 0);

}

void SetupTagets()
{
	static cvar_t*	dxr_on = ri.Cvar_Get("dxr_on", "0", 0);
	dxr_acceleration_structure_manager& drxAsm = dx.dxr->acceleration_structure_manager;

	/*if (dxr_on->value && drxAsm.MeshCount() > 0)
	{
		ID3D12DescriptorHeap* heaps[] = { dx.srv_heap, dx.sampler_heap };
		dx.command_list->SetDescriptorHeaps(_countof(heaps), heaps);

		dx.command_list->ResourceBarrier(1, &get_transition_barrier(dx_world.texture,
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

		D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = dx.dsv_heap->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = dx_world.textureUploadHeap->GetCPUDescriptorHandleForHeapStart();

		rtv_handle.ptr += dx.frame_index * dx.rtv_descriptor_size;

		dx.command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, &dsv_handle);

	}
	else*/


	{

		ID3D12DescriptorHeap* samplerHeap = dx.dx_renderTargets->GetSamplerHeap();
		ID3D12DescriptorHeap* srv_heap = dx.dx_renderTargets->GetSrvHeap();		
		
		ID3D12DescriptorHeap* heaps[] = { srv_heap, samplerHeap };
		dx.command_list->SetDescriptorHeaps(_countof(heaps), heaps);

		dx.command_list->ResourceBarrier(1, &get_transition_barrier(dx.dx_renderTargets->GetBackBufferRenderTarget(dx.frame_index),
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

		D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = dx.dx_renderTargets->dsv_heap->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = dx.dx_renderTargets->GetBackBuffer_rtv_handles(dx.frame_index);

		dx.command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, &dsv_handle);
	}
}

void dx_begin_frame()
{
	if (!dx.active)
		return;

	if (dx.fence->GetCompletedValue() < dx.fence_value) {
		DX_CHECK(dx.fence->SetEventOnCompletion(dx.fence_value, dx.fence_event));
		WaitForSingleObject(dx.fence_event, INFINITE);
	}

	dx.frame_index = dx.swapchain->GetCurrentBackBufferIndex();

	DX_CHECK(dx.command_allocator->Reset());
	DX_CHECK(dx.command_list->Reset(dx.command_allocator, nullptr));

#ifdef ENABLE_DXR
	dxr_acceleration_structure_manager& drxAsm = dx.dxr->acceleration_structure_manager;
	if (drxAsm.MeshCount() > 0) //we have some dxr meshs
	{
		DXR::SetupDXR(dx, dx_world, glConfig.vidWidth, glConfig.vidHeight);
	}
#endif

	dx.command_list->SetGraphicsRootSignature(dx.root_signature);

	SetupTagets();

	dx.xyz_elements = 0;
	dx.color_st_elements = 0;
	dx.index_buffer_offset = 0;

	/*{
		//clear
		D3D12_RECT r;

		r.left = 0.0f;
		r.top = 0.0f;
		r.right = glConfig.vidWidth;
		r.bottom = glConfig.vidHeight;

		dx.command_list->ClearRenderTargetView(rtv_handle, colorCyan, 1, &r);
	}*/
}

void dx_end_frame() {
	if (!dx.active)
		return;
	
	static cvar_t*	dxr_on = ri.Cvar_Get("dxr_on", "0", 0);
	static cvar_t*	dxr_dump = ri.Cvar_Get("dxr_dump", "0", 0);

	dxr_acceleration_structure_manager& drxAsm = dx.dxr->acceleration_structure_manager;

	if (dxr_dump->value)
	{
		dxr_dump->value = 0;
		drxAsm.WriteAllTopLevelMeshsDebug();
		//drxAsm.WriteAllBottomLevelMeshsDebug();
	}
	
	if (dxr_on->value && drxAsm.MeshCount() > 0)
	{
		//Do RAYtrace

		//use forword buffer as a texture

		/*
		dx.command_list->ResourceBarrier(1, &get_transition_barrier(dx_world.texture,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST));	

		dx.command_list->ResourceBarrier(1, &get_transition_barrier(dx.dx_renderTargets->render_targets[dx.frame_index],
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE));

		dx.command_list->CopyResource(dx_world.texture, dx.dx_renderTargets->render_targets[dx.frame_index]);

		dx.command_list->ResourceBarrier(1, &get_transition_barrier(dx_world.texture,
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

		*/


		DXR::UpdateAccelerationStructures(dx, *dx.dxr, dx_world);
		DXR::Build_Command_List(dx, *dx.dxr, dx_world);

		dx.command_list->ResourceBarrier(1, &get_transition_barrier(dx.dx_renderTargets->render_targets[dx.frame_index],
			D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST));

		dx.command_list->CopyResource(dx.dx_renderTargets->render_targets[dx.frame_index], dx.DXROutput);

		dx.command_list->ResourceBarrier(1, &get_transition_barrier(dx.dx_renderTargets->render_targets[dx.frame_index],
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT));

		drxAsm.EndFrame();
	}
	else
	{
		dx.command_list->ResourceBarrier(1, &get_transition_barrier(dx.dx_renderTargets->render_targets[dx.frame_index],
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
	}

	DX_CHECK(dx.command_list->Close());

	ID3D12CommandList* command_list = dx.command_list;
	dx.command_queue->ExecuteCommandLists(1, &command_list);

	dx.fence_value++;
	DX_CHECK(dx.command_queue->Signal(dx.fence, dx.fence_value));

	DX_CHECK(dx.swapchain->Present(0, 0));
}

#else // ENABLE_DX12

void dx_initialize() {}
void dx_shutdown() {}
void dx_release_resources() {}
void dx_wait_device_idle() {}

Dx_Image dx_create_image(int width, int height, Dx_Image_Format format, int mip_levels, bool repeat_texture, int image_index) { return Dx_Image{}; }
void dx_upload_image_data(ID3D12Resource* texture, int width, int height, int mip_levels, const uint8_t* pixels, int bytes_per_pixel) {}
void dx_create_sampler_descriptor(const Vk_Sampler_Def& def, Dx_Sampler_Index sampler_index) {}
ID3D12PipelineState* dx_find_pipeline(const Vk_Pipeline_Def& def) { return nullptr; }

void dx_clear_attachments(bool clear_depth_stencil, bool clear_color, vec4_t color) {}
void dx_bind_geometry() {}
void dx_shade_geometry(ID3D12PipelineState* pipeline, bool multitexture, Vk_Depth_Range depth_range, bool indexed, bool lines) {}
void dx_begin_frame() {}
void dx_end_frame() {}

#endif  // ENABLE_DX12
