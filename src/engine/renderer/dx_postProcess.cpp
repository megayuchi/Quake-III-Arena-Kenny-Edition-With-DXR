#include "D3d12.h"
#include <d3dx12.h>

#include <string>
#include "tr_local.h"

#include "dx_postProcess.h"
#include "dx_renderTargets.h"
#include "dx.h"

dx_postProcess::dx_postProcess()
{
}

dx_postProcess::~dx_postProcess()
{
}

void dx_postProcess::Setup(ID3D12Device5* device, Dx_Instance &d3d)
{
	this->mDevice = device;

	this->InitShader(*d3d.shaderCompiler);
	this->InitPipeline();
}

#define SAFE_RELEASE( x ) { if ( x ) { x->Release(); x = NULL; } }

void dx_postProcess::Shutdown()
{
	//SAFE_RELEASE(m_rgs.blob);
}

void dx_postProcess::RootParameter32BitConstantsHelper(D3D12_ROOT_PARAMETER rootParams[], int index, UINT shaderRegister, UINT num32BitValues)
{
	rootParams[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; //SetGraphicsRoot32BitConstants
	rootParams[index].Constants.ShaderRegister = shaderRegister;
	rootParams[index].Constants.RegisterSpace = 0;
	rootParams[index].Constants.Num32BitValues = num32BitValues;
	rootParams[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
}

void dx_postProcess::RootParameterDescriptorTableHelper(D3D12_ROOT_PARAMETER rootParams[], int index, D3D12_DESCRIPTOR_RANGE rangeSample[], UINT count)
{
	rootParams[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[index].DescriptorTable.NumDescriptorRanges = count;
	rootParams[index].DescriptorTable.pDescriptorRanges = rangeSample;
	rootParams[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
}

void dx_postProcess::DescriptorRangeUavHelper(D3D12_DESCRIPTOR_RANGE ranges[], int index, UINT numDescriptors, D3D12_DESCRIPTOR_RANGE_TYPE rangeType)
{
	UINT offsetInDescriptorsFromTableStart = 0;

	for (int i = 0; i < index; ++i)
	{
		offsetInDescriptorsFromTableStart += ranges[i].NumDescriptors;
	}

	ranges[index].BaseShaderRegister = 0;
	ranges[index].NumDescriptors = numDescriptors;
	ranges[index].RegisterSpace = 0;
	ranges[index].RangeType = rangeType;
	ranges[index].OffsetInDescriptorsFromTableStart = offsetInDescriptorsFromTableStart;
}


void dx_postProcess::HeapAddUavHelper(ID3D12Device5* device, D3D12_CPU_DESCRIPTOR_HANDLE handle, dx_renderTargets::Dx_RenderTarget_Index index)
{
	// Create the DXR output buffer UAV
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	device->CreateUnorderedAccessView(dx.dx_renderTargets->GetRenderTargetTexture(index), nullptr, &uavDesc, handle);
}

void dx_postProcess::HeapAddSrvHelper(ID3D12Device5* device, D3D12_CPU_DESCRIPTOR_HANDLE handle, dx_renderTargets::Dx_RenderTarget_Index index, DXGI_FORMAT format)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
	textureSRVDesc.Format = format;
	textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	textureSRVDesc.Texture2D.MipLevels = 1;
	textureSRVDesc.Texture2D.MostDetailedMip = 0;
	textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	device->CreateShaderResourceView(dx.dx_renderTargets->GetRenderTargetTexture(index), &textureSRVDesc, handle);
}

void dx_postProcess::HeapAddSrvDepthHelper(ID3D12Device5* device, D3D12_CPU_DESCRIPTOR_HANDLE handle, dx_renderTargets::Dx_DepthTarget_Index index)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
	textureSRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	textureSRVDesc.Texture2D.MipLevels = 1;
	textureSRVDesc.Texture2D.MostDetailedMip = 0;
	textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	device->CreateShaderResourceView(dx.dx_renderTargets->GetDepthStencilBuffer(index), &textureSRVDesc, handle);
}

void dx_postProcess::HeapAddSrvArrayHelper(ID3D12Device5* device, D3D12_CPU_DESCRIPTOR_HANDLE handle, ID3D12Resource* texture)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
	textureSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	textureSRVDesc.Texture2DArray.MipLevels = 1;
	textureSRVDesc.Texture2DArray.MostDetailedMip = 0;
	textureSRVDesc.Texture2DArray.FirstArraySlice = 0;
	textureSRVDesc.Texture2DArray.ArraySize = 64;
	textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	device->CreateShaderResourceView(texture, &textureSRVDesc, handle);
}


int dx_postProcess::HelperDxMatrixToFloatArray(float floatArray[], int startIndex, DirectX::XMMATRIX matrix)
{
	int index = startIndex;
	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			float v = matrix.r[r].m128_f32[c];
			floatArray[index] = v;
			index++;
		}
	}
	return index;
}

void dx_postProcess::CopyTargetHelper(dx_renderTargets::Dx_RenderTarget_Index sourceIndex, dx_renderTargets::Dx_RenderTarget_Index destIndex)
{
	dx.dx_renderTargets->SetRenderTargetTextureState(D3D12_RESOURCE_STATE_COPY_DEST, destIndex);
	dx.dx_renderTargets->SetRenderTargetTextureState(D3D12_RESOURCE_STATE_COPY_SOURCE, sourceIndex);

	dx.command_list->CopyResource(dx.dx_renderTargets->GetRenderTargetTexture(destIndex),
		dx.dx_renderTargets->GetRenderTargetTexture(sourceIndex));
}