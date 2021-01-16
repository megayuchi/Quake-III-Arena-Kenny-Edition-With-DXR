#include "D3d12.h"
#include <d3dx12.h>

#include <string>
#include "tr_local.h"

#include "dx_postProcessTemporalReproject.h"
#include "dx_renderTargets.h"
#include "dx.h"

dx_postProcessTemporalReproject::dx_postProcessTemporalReproject()
{
}

dx_postProcessTemporalReproject::~dx_postProcessTemporalReproject()
{
}


void dx_postProcessTemporalReproject::InitShader(D3DShaders::D3D12ShaderCompilerInfo &shaderCompiler)
{
	// Load and compile the ray generation shader
	mPass.m_rgs = D3DShaders::RtProgram(D3DShaders::D3D12ShaderInfo(L"shaders\\TemporalReprojectCS.hlsl", L"main", L"cs_6_0"));
	D3DShaders::Compile_Shader(shaderCompiler, mPass.m_rgs);

	D3D12_ROOT_PARAMETER rootParams[5] = {};

	RootParameter32BitConstantsHelper(rootParams, 0, 0, 7);
	RootParameter32BitConstantsHelper(rootParams, 1, 1, 16 * 4);
	RootParameter32BitConstantsHelper(rootParams, 2, 2, 16 * 2);

	D3D12_DESCRIPTOR_RANGE ranges[2];
	DescriptorRangeUavHelper(ranges, 0, 2, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
	DescriptorRangeUavHelper(ranges, 1, 8, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
	RootParameterDescriptorTableHelper(rootParams, 3, ranges, _countof(ranges));

	D3D12_DESCRIPTOR_RANGE rangeSample;
	DescriptorRangeUavHelper(&rangeSample, 0, 1, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER);
	RootParameterDescriptorTableHelper(rootParams, 4, &rangeSample, 1);

	D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
	rootDesc.NumParameters = _countof(rootParams);
	rootDesc.pParameters = rootParams;
	rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

	// Create the root signature
	mPass.m_rgs.pRootSignature = Create_Root_Signature(rootDesc);
	mPass.m_rgs.pRootSignature->SetName(L"post.pRootSignature");
}

void dx_postProcessTemporalReproject::InitPipeline()
{
	D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {};
	pipeline_desc.pRootSignature = mPass.m_rgs.pRootSignature;

	LPVOID code = mPass.m_rgs.blob->GetBufferPointer();
	SIZE_T codeSize = mPass.m_rgs.blob->GetBufferSize();

	pipeline_desc.CS = D3D12_SHADER_BYTECODE{ code, codeSize };

	pipeline_desc.NodeMask = 1;
	pipeline_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	HRESULT hr = mDevice->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&mPass.m_pipeline));
	if (FAILED(hr))
	{
		ri.Error(ERR_FATAL, "FAILED CreateGraphicsPipelineState %d", hr);
	}
}


void dx_postProcessTemporalReproject::Init_CBVSRVUAV_Heap()
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = 10;
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	// Create the descriptor heap
	HRESULT hr = mDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mPass.m_cbvSrvUavRayGenHeaps));
	if (FAILED(hr))
	{
		ri.Error(ERR_FATAL, "Error: failed to create DXR CBV/SRV/UAV descriptor heap!");
	}

	// Get the descriptor heap handle and increment size
	D3D12_CPU_DESCRIPTOR_HANDLE handle = mPass.m_cbvSrvUavRayGenHeaps->GetCPUDescriptorHandleForHeapStart();
	UINT handleIncrement = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	
	HeapAddUavHelper(mDevice, handle, dx_renderTargets::POST_Reproject_RT);										handle.ptr += handleIncrement;
	HeapAddUavHelper(mDevice, handle, dx_renderTargets::POST_SPP_RT);											handle.ptr += handleIncrement;
	HeapAddSrvHelper(mDevice, handle, dx_renderTargets::RAY_OUTPUT_RT, DXGI_FORMAT_R8G8B8A8_UNORM);				handle.ptr += handleIncrement;
	HeapAddSrvHelper(mDevice, handle, dx_renderTargets::POST_LAST_FRAME_LIGHT_RT, DXGI_FORMAT_R8G8B8A8_UNORM);	handle.ptr += handleIncrement;
	HeapAddSrvHelper(mDevice, handle, dx_renderTargets::G_BUFFER_NORMALS_RT, DXGI_FORMAT_R8G8B8A8_UNORM);		handle.ptr += handleIncrement;
	HeapAddSrvHelper(mDevice, handle, dx_renderTargets::G_BUFFER_LAST_NORMALS_RT, DXGI_FORMAT_R8G8B8A8_UNORM);	handle.ptr += handleIncrement;
	HeapAddSrvDepthHelper(mDevice, handle, dx_renderTargets::G_BUFFER_DEPTH_RT);								handle.ptr += handleIncrement;
	HeapAddSrvDepthHelper(mDevice, handle, dx_renderTargets::G_BUFFER_LAST_DEPTH_RT);							handle.ptr += handleIncrement;
	HeapAddSrvHelper(mDevice, handle, dx_renderTargets::G_BUFFER_VELOCITY_RT, DXGI_FORMAT_R16G16_FLOAT);		handle.ptr += handleIncrement;
	HeapAddSrvHelper(mDevice, handle, dx_renderTargets::POST_LAST_SPP_RT, DXGI_FORMAT_R32_UINT);				handle.ptr += handleIncrement;
}

void dx_postProcessTemporalReproject::Render()
{
	dx.command_list->SetComputeRootSignature(mPass.m_rgs.pRootSignature);

	// Set the shader constants
	uint32_t bufferWidth = dx.width;
	uint32_t bufferHeight = dx.height;

	static cvar_t*	dxr_temporal_debug = ri.Cvar_Get("dxr_temporal_debug", "0", 0);

	float dimensions[6] = { (float)bufferWidth, (float)bufferHeight, 1.0f / (float)bufferWidth, 1.0f / (float)bufferHeight,
	dxr_temporal_debug->value,
	dx_world.zFar };
	
	dx.command_list->SetComputeRoot32BitConstants(0, 6, dimensions, 0);

	{
		float root_constants[16 * 4];
		int root_constant_count = 0;

		root_constant_count = HelperDxMatrixToFloatArray(root_constants, root_constant_count, DirectX::XMMATRIX(dx_world.proj_transform));
		root_constant_count = HelperDxMatrixToFloatArray(root_constants, root_constant_count, DirectX::XMMatrixInverse(NULL, DirectX::XMMATRIX(dx_world.proj_transform)));
		root_constant_count = HelperDxMatrixToFloatArray(root_constants, root_constant_count, DirectX::XMMATRIX(dx_world.view_transform3D));
		root_constant_count = HelperDxMatrixToFloatArray(root_constants, root_constant_count, DirectX::XMMatrixInverse(NULL, DirectX::XMMATRIX(dx_world.view_transform3D)));

		dx.command_list->SetComputeRoot32BitConstants(1, root_constant_count, root_constants, 0);
	}

	{
		float root_constants[16 * 2];
		int root_constant_count = 0;

		root_constant_count = HelperDxMatrixToFloatArray(root_constants, root_constant_count, DirectX::XMMATRIX(dx_world.view_transformLast3D));
		root_constant_count = HelperDxMatrixToFloatArray(root_constants, root_constant_count, DirectX::XMMatrixInverse(NULL, DirectX::XMMATRIX(dx_world.view_transformLast3D)));

		dx.command_list->SetComputeRoot32BitConstants(2, root_constant_count, root_constants, 0);
	}

	dx.dx_renderTargets->SetRenderTargetTextureState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, dx_renderTargets::G_BUFFER_ALBEDO_RT);
	dx.dx_renderTargets->SetRenderTargetTextureState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, dx_renderTargets::G_BUFFER_NORMALS_RT);
	dx.dx_renderTargets->SetRenderTargetTextureState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, dx_renderTargets::G_BUFFER_LAST_NORMALS_RT);
	dx.dx_renderTargets->SetRenderTargetTextureState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, dx_renderTargets::POST_LAST_SPP_RT);
	dx.dx_renderTargets->SetRenderTargetTextureState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, dx_renderTargets::G_BUFFER_VELOCITY_RT);	
	dx.dx_renderTargets->SetRenderTargetTextureState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, dx_renderTargets::POST_Reproject_RT);
	dx.dx_renderTargets->SetRenderTargetTextureState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, dx_renderTargets::POST_SPP_RT);
	
	ID3D12DescriptorHeap* samplerHeap = dx.dx_renderTargets->GetSamplerHeap();
	ID3D12DescriptorHeap* heaps[] = { mPass.m_cbvSrvUavRayGenHeaps, samplerHeap };

	dx.command_list->SetDescriptorHeaps(_countof(heaps), heaps);

	dx.command_list->SetComputeRootDescriptorTable(3, mPass.m_cbvSrvUavRayGenHeaps->GetGPUDescriptorHandleForHeapStart());

	{
		D3D12_GPU_DESCRIPTOR_HANDLE sampler_handle = dx.dx_renderTargets->GetSamplerHandle(SAMPLER_FULLSCREEN_CLAMP);
		dx.command_list->SetComputeRootDescriptorTable(4, sampler_handle);
	}

	dx.command_list->SetPipelineState(mPass.m_pipeline);

	// Dispatch the compute shader with default 8x8 thread groups
	dx.command_list->Dispatch(bufferWidth / 8, bufferHeight / 8, 1);

	CopyTargetHelper(dx_renderTargets::POST_Reproject_RT, dx_renderTargets::POST_LAST_FRAME_LIGHT_RT);
	CopyTargetHelper(dx_renderTargets::G_BUFFER_NORMALS_RT, dx_renderTargets::G_BUFFER_LAST_NORMALS_RT);
	CopyTargetHelper(dx_renderTargets::POST_SPP_RT, dx_renderTargets::POST_LAST_SPP_RT);
}
