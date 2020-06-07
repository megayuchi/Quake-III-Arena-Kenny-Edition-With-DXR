#pragma once

#include "D3d12.h"

#include <algorithm>
#include "dx_shaders.h"
#include "dx_renderTargets.h"

struct ID3D12Device5;
struct Dx_Instance;

class dx_postProcess
{
private:
	struct Pass
	{
		D3DShaders::RtProgram m_rgs;
		ID3D12PipelineState* m_pipeline;
		ID3D12DescriptorHeap* m_cbvSrvUavRayGenHeaps;
	};
public:
	dx_postProcess();
	~dx_postProcess();

	void Setup(ID3D12Device5* device, Dx_Instance &d3d);
	void Shutdown();

	virtual void Init_CBVSRVUAV_Heap() = 0;

	virtual void Render() = 0;

protected:

	virtual void InitShader(D3DShaders::D3D12ShaderCompilerInfo &shaderCompiler) = 0;
	
	virtual void InitPipeline() = 0;

	
	ID3D12Device5* mDevice = (nullptr);

	Pass mPass;

	static void dx_postProcess::RootParameter32BitConstantsHelper(D3D12_ROOT_PARAMETER rootParams[], int index, UINT shaderRegister, UINT num32BitValues);
	static void dx_postProcess::RootParameterDescriptorTableHelper(D3D12_ROOT_PARAMETER rootParams[], int index, D3D12_DESCRIPTOR_RANGE rangeSample[], UINT count);
	static void DescriptorRangeUavHelper(D3D12_DESCRIPTOR_RANGE ranges[], int index, UINT numDescriptors, D3D12_DESCRIPTOR_RANGE_TYPE rangeType);
	static void HeapAddUavHelper(ID3D12Device5* device, D3D12_CPU_DESCRIPTOR_HANDLE handle, dx_renderTargets::Dx_RenderTarget_Index index);
	static void HeapAddSrvHelper(ID3D12Device5* device, D3D12_CPU_DESCRIPTOR_HANDLE handle, dx_renderTargets::Dx_RenderTarget_Index index, DXGI_FORMAT format);
	static void HeapAddSrvDepthHelper(ID3D12Device5* device, D3D12_CPU_DESCRIPTOR_HANDLE handle, dx_renderTargets::Dx_DepthTarget_Index index);
	static void HeapAddSrvArrayHelper(ID3D12Device5* device, D3D12_CPU_DESCRIPTOR_HANDLE handle, ID3D12Resource* texture);
	static int HelperDxMatrixToFloatArray(float floatArray[], int startIndex, DirectX::XMMATRIX matrix);
	static void CopyTargetHelper(dx_renderTargets::Dx_RenderTarget_Index sourceIndex, dx_renderTargets::Dx_RenderTarget_Index destIndex);
	
};

