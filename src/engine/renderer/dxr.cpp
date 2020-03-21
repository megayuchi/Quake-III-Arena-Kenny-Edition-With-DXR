#include "tr_local.h"

#include "D3d12.h"
#include <d3dx12.h>

#include "dx.h"
#include "dxr.h"
#include "dx_shaders.h"
#include "dx_renderTargets.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN			// Exclude rarely-used items from Windows headers.
#endif

#pragma warning(push)
#pragma warning(disable:4191)
#include <Windows.h>
#include <wrl.h>
#include <atlcomcli.h>

#pragma warning(pop)

#define MAX_TOP_LEVEL_INSTANCE_COUNT 6000
#define MAX_BOTTOM_LEVEL_MESH_COUNT 6000
#define MEGA_INDEX_VERTEX_BUFFER_SIZE 0x0000000000ffffff //big ;) got to hold all the polys

void Create_Buffer(Dx_Instance &d3d, D3D12BufferCreateInfo& info, ID3D12Resource** ppResource)
{
	HRESULT hr;

	D3D12_HEAP_PROPERTIES heapDesc = {};
	heapDesc.Type = info.heapType;
	heapDesc.CreationNodeMask = 1;
	heapDesc.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Alignment = info.alignment;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Width = info.size;
	resourceDesc.Flags = info.flags;

	// Create the GPU resource
	hr = d3d.device->CreateCommittedResource(&heapDesc, D3D12_HEAP_FLAG_NONE, &resourceDesc, info.state, nullptr, IID_PPV_ARGS(ppResource));
	if (FAILED(hr))
	{
		ri.Error(ERR_FATAL, "Error: failed to create buffer resource!");
	}
}

namespace DXR
{
	void SetupDXR(Dx_Instance &d3d, Dx_World &world, int vidWidth, int vidHeight)
	{
		if (d3d.dxr_initialized)
		{
			return;
		}
		d3d.dxr_initialized = true;

		d3d.width = vidWidth;
		d3d.height = vidHeight;

		d3d.shaderCompiler = new D3D12ShaderCompilerInfo;
		d3d.resources = new D3D12Resources;
		Com_Memset(d3d.resources, 0, sizeof(D3D12Resources));

		world.viewCBData = new ViewCB;
		world.materialCBData = new MaterialCB;

		Init_Shader_Compiler(*d3d.shaderCompiler);

		// Create common resources
		DXR::Create_View_CB(d3d, *d3d.dxr, world);
		DXR::Create_Material_CB(d3d, *d3d.dxr, world);

		// Create DXR specific resources
		DXR::Create_Index_Vertex_Buffers(dx, *dx.dxr, world);
		DXR::BuildAccelerationStructure(dx, *dx.dxr, world);
		DXR::Create_DXR_Output(d3d);
		Create_CBVSRVUAV_Heap(d3d, *d3d.dxr, world);
		DXR::Create_RayGen_Program(d3d, *d3d.dxr, *d3d.shaderCompiler);
		DXR::Create_Miss_Program(d3d, *d3d.dxr, *d3d.shaderCompiler);
		DXR::Create_Closest_Hit_Program(d3d, *d3d.dxr, *d3d.shaderCompiler);
		DXR::Create_Global_RootSignature(d3d, *d3d.dxr, *d3d.shaderCompiler);
		DXR::Create_Pipeline_State_Object(d3d, *d3d.dxr);
		DXR::Create_Shader_Table(d3d, *d3d.dxr, world);
	}

	void BuildAccelerationStructure(Dx_Instance &d3d, DXRGlobal &dxr, Dx_World &world)
	{
		DXR::Create_Bottom_Level_AS(d3d, *d3d.dxr, world);
		DXR::Create_Top_Level_AS(d3d, *d3d.dxr);
	}

	void Init_Shader_Compiler(D3D12ShaderCompilerInfo &shaderCompiler)
	{
		HRESULT hr = shaderCompiler.DxcDllHelper.Initialize();
		if (FAILED(hr))
		{
			ri.Error(ERR_FATAL, "Failed to initialize DxCDllSupport!");
		}

		hr = shaderCompiler.DxcDllHelper.CreateInstance(CLSID_DxcCompiler, &shaderCompiler.compiler);
		if (FAILED(hr))
		{
			ri.Error(ERR_FATAL, "Failed to create DxcCompiler!");
		}

		hr = shaderCompiler.DxcDllHelper.CreateInstance(CLSID_DxcLibrary, &shaderCompiler.library);
		if (FAILED(hr))
		{
			ri.Error(ERR_FATAL, "Failed to create DxcLibrary!");
		}
	}

	void Create_Constant_Buffer(Dx_Instance &d3d, ID3D12Resource** buffer, UINT64 size)
	{
		D3D12BufferCreateInfo bufferInfo((size + 255) & ~255, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
		Create_Buffer(d3d, bufferInfo, buffer);
	}


	void Create_View_CB(Dx_Instance &d3d, DXRGlobal &dxr, Dx_World &world)
	{
		Create_Constant_Buffer(d3d, &world.viewCB, sizeof(ViewCB));

		HRESULT hr = world.viewCB->Map(0, nullptr, reinterpret_cast<void**>(&world.viewCBStart));
		if (FAILED(hr))
		{
			ri.Error(ERR_FATAL, "Error: failed to map View constant buffer!");
		}

		memcpy(world.viewCBStart, &world.viewCBData, sizeof(*world.viewCBData));
	}

	void Create_Material_CB(Dx_Instance &d3d, DXRGlobal &dxr, Dx_World &world)
	{
		Create_Constant_Buffer(d3d, &world.materialCB, sizeof(MaterialCB));

		world.materialCBData->resolution = DirectX::XMFLOAT4(32, 0.f, 0.f, 0.f);

		HRESULT hr = world.materialCB->Map(0, nullptr, reinterpret_cast<void**>(&world.materialCBStart));
		if (FAILED(hr))
		{
			ri.Error(ERR_FATAL, "Error: failed to map Material constant buffer!");
		}

		memcpy(world.materialCBStart, world.materialCBData, sizeof(world.materialCBData));
	}

	void Update_View_CB(Dx_Instance &d3d, DXRGlobal &dxr, Dx_World &world)
	{
		DirectX::XMFLOAT3 eye = DirectX::XMFLOAT3(world.viewOrg[0], world.viewOrg[1], world.viewOrg[2]);
		DirectX::XMFLOAT3 lookAt = DirectX::XMFLOAT3(world.viewaxisForword[0], world.viewaxisForword[1], world.viewaxisForword[2]);
		DirectX::XMFLOAT3 up = DirectX::XMFLOAT3(0.f, 0.f, 1.f);

		float fov = world.viewFov[1] * (DirectX::XM_PI / 180.f);							// convert to radians

		DirectX::XMMATRIX view = DirectX::XMMatrixLookToLH(XMLoadFloat3(&eye), XMLoadFloat3(&lookAt), XMLoadFloat3(&up));
		DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(NULL, view);

		world.viewCBData->view = XMMatrixTranspose(invView);
		world.viewCBData->projMatrixInv = DirectX::XMMatrixInverse(NULL, DirectX::XMMATRIX(world.proj_transform));
		world.viewCBData->viewMatrixInv = DirectX::XMMatrixInverse(NULL, DirectX::XMMATRIX(world.view_transform));


		world.viewCBData->viewOriginAndTanHalfFovY = DirectX::XMFLOAT4(eye.x, eye.y, eye.z, tanf(fov * 0.5f));
		world.viewCBData->resolution = DirectX::XMFLOAT2((float)d3d.width, (float)d3d.height);
		world.viewCBData->light = DirectX::XMFLOAT4(0.25f, 0.15f, 1.25f, 1.0f);

		size_t sizedata = sizeof(*world.viewCBData);
		memcpy(world.viewCBStart, &(*world.viewCBData), sizedata);
	}

	void Create_Index_Vertex_Buffers(Dx_Instance &d3d, DXRGlobal &dxr, Dx_World &world)
	{
		D3D12BufferCreateInfo vectexInfo(
			MEGA_INDEX_VERTEX_BUFFER_SIZE,
			D3D12_HEAP_TYPE_UPLOAD,
			D3D12_RESOURCE_STATE_GENERIC_READ);
		Create_Buffer(d3d, vectexInfo, &d3d.resources->vertexBufferMega);

#if defined(_DEBUG)
		d3d.resources->vertexBufferMega->SetName(L"vertexBufferMega");
#endif

		D3D12BufferCreateInfo indexInfo(
			MEGA_INDEX_VERTEX_BUFFER_SIZE,
			D3D12_HEAP_TYPE_UPLOAD,
			D3D12_RESOURCE_STATE_GENERIC_READ);
		Create_Buffer(d3d, indexInfo, &d3d.resources->indexBufferMega);

#if defined(_DEBUG)
		d3d.resources->indexBufferMega->SetName(L"indexBufferMega");
#endif

		d3d.resources->currentVertexBufferMegaCount = 0;
		d3d.resources->currentIndexBufferMegaCount = 0;
	}

	void Create_Bottom_Level_AS(Dx_Instance &d3d, DXRGlobal &dxr, Dx_World &world)
	{
		dxr_acceleration_structure_manager& drxAsm = d3d.dxr->acceleration_structure_manager;

		if (drxAsm.DirtyCount() < 1)
		{
			return;
		}

		size_t meshCount = drxAsm.MeshCount();

		size_t vertexSizeOf = drxAsm.VertexSize();
		size_t indexSizeOf = sizeof(UINT);

		for (int i = 0; i < meshCount; ++i)
		{
			bool update = drxAsm.IsMeshUpdate(i);
			if (!(drxAsm.IsMeshDirty(i) || update))
			{
				continue;
			}

			// Create the buffer resource from the model's vertices
			{
				UINT meshVertexCount = drxAsm.MeshVertexCount(i);

				// Copy the vertex data to the vertex buffer
				UINT8* pVertexDataBegin;
				D3D12_RANGE readWriteRange = {};
				readWriteRange.Begin = d3d.resources->currentVertexBufferMegaCount *  vertexSizeOf;
				readWriteRange.End = (d3d.resources->currentVertexBufferMegaCount + meshVertexCount) *  vertexSizeOf;

				if (update)
				{
					UINT32 pos = drxAsm.GetVertexBufferMegaPos(i);
					readWriteRange.Begin = pos * vertexSizeOf;
					readWriteRange.End = (pos + meshVertexCount) *  vertexSizeOf;
					assert(readWriteRange.End > readWriteRange.Begin);
				}
				else
				{
					drxAsm.SetVertexBufferMegaPos(i, d3d.resources->currentVertexBufferMegaCount);
				}
				assert(readWriteRange.End > readWriteRange.Begin);
				assert(readWriteRange.End < MEGA_INDEX_VERTEX_BUFFER_SIZE);
				HRESULT hr = d3d.resources->vertexBufferMega->Map(0, &readWriteRange, reinterpret_cast<void**>(&pVertexDataBegin));
				if (FAILED(hr))
				{
					ri.Error(ERR_FATAL, "Error: failed to map vertex buffer!");
				}
				const void* vertexData = drxAsm.GetMeshVertexData(i);
				UINT64 size = meshVertexCount * vertexSizeOf;

				pVertexDataBegin += readWriteRange.Begin;//you would think the Map would give the pointer at the Range Beging?

				memcpy(pVertexDataBegin, vertexData, size);
				d3d.resources->vertexBufferMega->Unmap(0, &readWriteRange);
			}

			// Create the index buffer resource
			if (!update)
			{
				UINT64 size = drxAsm.MeshIndexCount(i) * sizeof(UINT);

				// Copy the index data to the index buffer
				UINT8* pIndexDataBegin;
				D3D12_RANGE readWriteRange = {};
				readWriteRange.Begin = d3d.resources->currentIndexBufferMegaCount *  indexSizeOf;
				readWriteRange.End = (d3d.resources->currentIndexBufferMegaCount + drxAsm.MeshIndexCount(i)) *  indexSizeOf;
				drxAsm.SetIndexBufferMegaPos(i, d3d.resources->currentIndexBufferMegaCount);
				assert(readWriteRange.End < MEGA_INDEX_VERTEX_BUFFER_SIZE);
				HRESULT hr = d3d.resources->indexBufferMega->Map(0, &readWriteRange, reinterpret_cast<void**>(&pIndexDataBegin));
				if (FAILED(hr))
				{
					ri.Error(ERR_FATAL, "Error: failed to map index buffer!");
				}

				const void* indexData = drxAsm.GetMeshIndexData(i);
				pIndexDataBegin += readWriteRange.Begin;;//you would think the Map would give the pointer at the Range Beging?
				memcpy(pIndexDataBegin, indexData, size);
				d3d.resources->indexBufferMega->Unmap(0, &readWriteRange);
			}

			// Describe the geometry that goes in the bottom acceleration structure(s)
			D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc;

			geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
			UINT32 vertexPos = drxAsm.GetVertexBufferMegaPos(i);
			geometryDesc.Triangles.VertexBuffer.StartAddress = d3d.resources->vertexBufferMega->GetGPUVirtualAddress() + vertexPos * vertexSizeOf;
			geometryDesc.Triangles.VertexBuffer.StrideInBytes = (UINT)vertexSizeOf;
			geometryDesc.Triangles.VertexCount = drxAsm.MeshVertexCount(i);
			geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;

			UINT32 indexPos = drxAsm.GetIndexBufferMegaPos(i);
			geometryDesc.Triangles.IndexBuffer = d3d.resources->indexBufferMega->GetGPUVirtualAddress() + indexPos * indexSizeOf;
			geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
			geometryDesc.Triangles.IndexCount = drxAsm.MeshIndexCount(i);
			geometryDesc.Triangles.Transform3x4 = 0;
			geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

			if (dxr_acceleration_structure_manager::DYNAMIC_MESH == drxAsm.GetMeshType(i))
			{
				buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
			}

			// Get the size requirements for the BLAS buffers
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS ASInputs = {};
			ASInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
			ASInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			ASInputs.pGeometryDescs = &geometryDesc;
			ASInputs.NumDescs = 1;
			ASInputs.Flags = buildFlags;

			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO ASPreBuildInfo = {};
			d3d.device->GetRaytracingAccelerationStructurePrebuildInfo(&ASInputs, &ASPreBuildInfo);

			if (update)
			{
				// If this a request for an update, then the BLAS was already used in a DispatchRay() call. We need a UAV barrier to make sure the read operation ends before updating the buffer
				D3D12_RESOURCE_BARRIER uavBarrier = {};
				uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
				AccelerationStructureBuffer& BLAS = dxr.BLASs[i];
				uavBarrier.UAV.pResource = BLAS.pResult;
				d3d.command_list->ResourceBarrier(1, &uavBarrier);
			}
			else
			{

				ASPreBuildInfo.ScratchDataSizeInBytes = ALIGN(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, ASPreBuildInfo.ScratchDataSizeInBytes);
				ASPreBuildInfo.ResultDataMaxSizeInBytes = ALIGN(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, ASPreBuildInfo.ResultDataMaxSizeInBytes);

				AccelerationStructureBuffer BLAS;

				// Create the BLAS scratch buffer
				D3D12BufferCreateInfo bufferInfo(ASPreBuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				bufferInfo.alignment = std::max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
				Create_Buffer(d3d, bufferInfo, &BLAS.pScratch);

				// Create the BLAS buffer
				bufferInfo.size = ASPreBuildInfo.ResultDataMaxSizeInBytes;
				bufferInfo.state = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
				Create_Buffer(d3d, bufferInfo, &BLAS.pResult);

				dxr.BLASs.push_back(BLAS);
			}

			AccelerationStructureBuffer& BLAS = dxr.BLASs[i];
			// Describe and build the bottom level acceleration structure
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
			buildDesc.Inputs = ASInputs;
			buildDesc.ScratchAccelerationStructureData = BLAS.pScratch->GetGPUVirtualAddress();
			buildDesc.DestAccelerationStructureData = BLAS.pResult->GetGPUVirtualAddress();

			if (update)
			{
				buildDesc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
				buildDesc.SourceAccelerationStructureData = BLAS.pResult->GetGPUVirtualAddress();
			}

			d3d.command_list->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

			// Wait for the BLAS build to complete
			D3D12_RESOURCE_BARRIER uavBarrier;
			uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			uavBarrier.UAV.pResource = BLAS.pResult;
			uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			d3d.command_list->ResourceBarrier(1, &uavBarrier);

			if (!update)
			{
				d3d.resources->currentVertexBufferMegaCount += drxAsm.MeshVertexCount(i);
				d3d.resources->currentIndexBufferMegaCount += drxAsm.MeshIndexCount(i);
			}
		}

		drxAsm.ClearDirty();
	}

	void Create_Top_Level_AS(Dx_Instance &d3d, DXRGlobal &dxr)
	{
		UINT meshCount = MAX_TOP_LEVEL_INSTANCE_COUNT;

		// Get the size requirements for the TLAS buffers
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS ASInputs = {};
		ASInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		ASInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		ASInputs.NumDescs = meshCount;

		ASInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO ASPreBuildInfo = {};
		d3d.device->GetRaytracingAccelerationStructurePrebuildInfo(&ASInputs, &ASPreBuildInfo);

		{
			ASPreBuildInfo.ResultDataMaxSizeInBytes = ALIGN(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, ASPreBuildInfo.ResultDataMaxSizeInBytes);
			ASPreBuildInfo.ScratchDataSizeInBytes = ALIGN(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, ASPreBuildInfo.ScratchDataSizeInBytes);

			// Create TLAS scratch buffer
			D3D12BufferCreateInfo bufferInfo(ASPreBuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			bufferInfo.alignment = std::max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
			Create_Buffer(d3d, bufferInfo, &dxr.TLAS.pScratch);

			// Create the TLAS buffer
			bufferInfo.size = ASPreBuildInfo.ResultDataMaxSizeInBytes;
			bufferInfo.state = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
			Create_Buffer(d3d, bufferInfo, &dxr.TLAS.pResult);

			// Create the TLAS instance buffer
			D3D12BufferCreateInfo instanceBufferInfo;
			instanceBufferInfo.size = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * meshCount * 3;//that is this was TOO big?
			instanceBufferInfo.heapType = D3D12_HEAP_TYPE_UPLOAD;
			instanceBufferInfo.flags = D3D12_RESOURCE_FLAG_NONE;
			instanceBufferInfo.state = D3D12_RESOURCE_STATE_GENERIC_READ;

			Create_Buffer(d3d, instanceBufferInfo, &dxr.TLAS.pInstanceDesc);
		}

		// Copy the instance data to the buffer
		UINT8* pData;
		dxr.TLAS.pInstanceDesc->Map(0, nullptr, (void**)&pData);

		for (int i = 0; i < meshCount; ++i)
		{
			// Describe the TLAS geometry instance(s)
			D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
			instanceDesc.InstanceID = i;											// This value will be exposed to the shader via InstanceID()    // This value is exposed to shaders as SV_InstanceID
			instanceDesc.InstanceContributionToHitGroupIndex = 0;
			instanceDesc.InstanceMask = 0;//Don't show!
			memset(&instanceDesc.Transform, 0, sizeof(FLOAT) * 3 * 4);
			instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1;		// identity transform
			instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
			instanceDesc.AccelerationStructure = dxr.BLASs[0].pResult->GetGPUVirtualAddress(); //Just use first mesh (0), got to be a better way

			memcpy(pData, &instanceDesc, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
			pData += sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
		}
		dxr.TLAS.pInstanceDesc->Unmap(0, nullptr);

		// Describe and build the TLAS
		ASInputs.InstanceDescs = dxr.TLAS.pInstanceDesc->GetGPUVirtualAddress();
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
		buildDesc.Inputs = ASInputs;
		buildDesc.ScratchAccelerationStructureData = dxr.TLAS.pScratch->GetGPUVirtualAddress();
		buildDesc.DestAccelerationStructureData = dxr.TLAS.pResult->GetGPUVirtualAddress();

		d3d.command_list->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

		// Wait for the TLAS build to complete
		D3D12_RESOURCE_BARRIER uavBarrier;
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = dxr.TLAS.pResult;
		uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		d3d.command_list->ResourceBarrier(1, &uavBarrier);
	}

	void Update_Top_Level_AS(Dx_Instance &d3d, DXRGlobal &dxr)
	{
		dxr_acceleration_structure_manager& drxAsm = d3d.dxr->acceleration_structure_manager;
		size_t meshCount = drxAsm.GetInstanceCount();

		if (meshCount > MAX_TOP_LEVEL_INSTANCE_COUNT)
		{
			char buffer[512];
			sprintf(buffer, "MAX_TOP_LEVEL_INSTANCE_COUNT hit!, meshCount %d\n", (int)meshCount);
			OutputDebugStringA(buffer);

			meshCount = MAX_TOP_LEVEL_INSTANCE_COUNT;
		}

		// Get the size requirements for the TLAS buffers
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS ASInputs = {};
		ASInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		ASInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		ASInputs.NumDescs = MAX_TOP_LEVEL_INSTANCE_COUNT;
		ASInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO ASPreBuildInfo = {};
		d3d.device->GetRaytracingAccelerationStructurePrebuildInfo(&ASInputs, &ASPreBuildInfo);

		{
			// If this a request for an update, then the TLAS was already used in a DispatchRay() call. We need a UAV barrier to make sure the read operation ends before updating the buffer
			D3D12_RESOURCE_BARRIER uavBarrier = {};
			uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			uavBarrier.UAV.pResource = dxr.TLAS.pResult;
			d3d.command_list->ResourceBarrier(1, &uavBarrier);
		}

		const int* instanceData = drxAsm.GetInstanceData();
		std::vector<dxr_acceleration_structure_manager::InstancesTransform>& instanceDataTransform = drxAsm.GetInstanceTransformData();

		// Copy the instance data to the buffer
		UINT8* pData;
		dxr.TLAS.pInstanceDesc->Map(0, nullptr, (void**)&pData);

		for (int i = 0; i < meshCount; ++i)
		{
			int bottomLevelIndex = instanceData[i];
			dxr_acceleration_structure_manager::InstancesTransform& transfrom = instanceDataTransform[i];

			// Describe the TLAS geometry instance(s)
			D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
			instanceDesc.InstanceID = bottomLevelIndex;											// This value will be exposed to the shader via InstanceID()    // This value is exposed to shaders as SV_InstanceID
			instanceDesc.InstanceContributionToHitGroupIndex = bottomLevelIndex;
			instanceDesc.InstanceMask = 0xFF;

			Com_Memcpy(instanceDesc.Transform, transfrom.data, sizeof(FLOAT) * 3 * 4);

			instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
			instanceDesc.AccelerationStructure = dxr.BLASs[bottomLevelIndex].pResult->GetGPUVirtualAddress();

			memcpy(pData, &instanceDesc, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
			pData += sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
		}

		for (size_t i = meshCount; i < MAX_TOP_LEVEL_INSTANCE_COUNT; ++i)
		{
			D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
			instanceDesc.InstanceMask = 0;
			instanceDesc.InstanceContributionToHitGroupIndex = 0;
			memcpy(pData, &instanceDesc, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
			pData += sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
		}

		dxr.TLAS.pInstanceDesc->Unmap(0, nullptr);

		// Describe and UPDATE the TLAS
		ASInputs.InstanceDescs = dxr.TLAS.pInstanceDesc->GetGPUVirtualAddress();
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
		buildDesc.Inputs = ASInputs;
		buildDesc.ScratchAccelerationStructureData = dxr.TLAS.pScratch->GetGPUVirtualAddress();
		buildDesc.DestAccelerationStructureData = dxr.TLAS.pResult->GetGPUVirtualAddress();
		buildDesc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
		buildDesc.SourceAccelerationStructureData = dxr.TLAS.pResult->GetGPUVirtualAddress();

		d3d.command_list->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

		// Wait for the TLAS build to complete
		D3D12_RESOURCE_BARRIER uavBarrier;
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = dxr.TLAS.pResult;
		uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		d3d.command_list->ResourceBarrier(1, &uavBarrier);
	}

	void UpdateAccelerationStructures(Dx_Instance &d3d, DXRGlobal &dxr, Dx_World &world)
	{
		DXR::Create_Bottom_Level_AS(d3d, *d3d.dxr, world);
		DXR::Update_Top_Level_AS(d3d, *d3d.dxr);
	}

	void Create_Global_RootSignature(Dx_Instance &d3d, DXRGlobal &dxr, D3D12ShaderCompilerInfo &shaderCompiler)
	{// Create an empty root signature	

		dxr.globalSignature = Create_Root_Signature({});
		dxr.globalSignature->SetName(L"Global.pRootSignature");
	}

	void Create_RayGen_Program(Dx_Instance &d3d, DXRGlobal &dxr, D3D12ShaderCompilerInfo &shaderCompiler)
	{
		// Load and compile the ray generation shader
		dxr.rgs = RtProgram(D3D12ShaderInfo(L"shaders\\RayGen.hlsl", L"", L"lib_6_3"));
		D3DShaders::Compile_Shader(shaderCompiler, dxr.rgs);

		// Describe the ray generation root signature
		D3D12_DESCRIPTOR_RANGE ranges[3];

		ranges[0].BaseShaderRegister = 0;
		ranges[0].NumDescriptors = 1;
		ranges[0].RegisterSpace = 0;
		ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		ranges[0].OffsetInDescriptorsFromTableStart = 0;

		ranges[1].BaseShaderRegister = 0;
		ranges[1].NumDescriptors = 1;
		ranges[1].RegisterSpace = 0;
		ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		ranges[1].OffsetInDescriptorsFromTableStart = 2;

		ranges[2].BaseShaderRegister = 0; //register(t0);
		ranges[2].NumDescriptors = 6;
		ranges[2].RegisterSpace = 0; //register(t0,space0)
		ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[2].OffsetInDescriptorsFromTableStart = 3;

		D3D12_ROOT_PARAMETER param0 = {};
		param0.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		param0.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		param0.DescriptorTable.NumDescriptorRanges = _countof(ranges);
		param0.DescriptorTable.pDescriptorRanges = ranges;

		D3D12_ROOT_PARAMETER rootParams[1] = { param0 };

		D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
		rootDesc.NumParameters = _countof(rootParams);
		rootDesc.pParameters = rootParams;
		rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

		// Create the root signature
		dxr.rgs.pRootSignature = Create_Root_Signature(rootDesc);
		dxr.rgs.pRootSignature->SetName(L"rgs.pRootSignature");
	}

	void Create_Miss_Program(Dx_Instance &d3d, DXRGlobal &dxr, D3D12ShaderCompilerInfo &shaderCompiler)
	{
		// Load and compile the miss shader
		dxr.miss = RtProgram(D3D12ShaderInfo(L"shaders\\Miss.hlsl", L"", L"lib_6_3"));
		D3DShaders::Compile_Shader(shaderCompiler, dxr.miss);

		// Create an empty root signature	

		D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
		rootDesc.NumParameters = 0;
		rootDesc.pParameters = 0;
		rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

		dxr.miss.pRootSignature = Create_Root_Signature(rootDesc);
		dxr.miss.pRootSignature->SetName(L"miss.pRootSignatureX");
	}

	void Create_Closest_Hit_Program(Dx_Instance &d3d, DXRGlobal &dxr, D3D12ShaderCompilerInfo &shaderCompiler)
	{
		dxr.hit = HitProgram(L"Hit");
		dxr.hit.chs = RtProgram(D3D12ShaderInfo(L"shaders\\ClosestHit.hlsl", L"", L"lib_6_3"));
		D3DShaders::Compile_Shader(shaderCompiler, dxr.hit.chs);

		D3D12_ROOT_PARAMETER param0 = {};

		param0.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		param0.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		param0.Constants.Num32BitValues = 4;
		param0.Constants.ShaderRegister = 0;
		param0.Constants.RegisterSpace = 3;

		// Describe the ray generation root signature
		D3D12_DESCRIPTOR_RANGE ranges[1];

		//vertex & index buffers
		ranges[0].BaseShaderRegister = 1; //register(t0);
		ranges[0].NumDescriptors = 2;
		ranges[0].RegisterSpace = 0; //register(t1,space0)
		ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[0].OffsetInDescriptorsFromTableStart = 4;

		D3D12_ROOT_PARAMETER param1 = {};
		param1.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		param1.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		param1.DescriptorTable.NumDescriptorRanges = _countof(ranges);
		param1.DescriptorTable.pDescriptorRanges = ranges;

		D3D12_ROOT_PARAMETER rootParams[2] = { param0, param1 };

		D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
		rootDesc.NumParameters = _countof(rootParams);
		rootDesc.pParameters = rootParams;
		rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

		// Create the root signature
		dxr.hit.chs.pRootSignature = Create_Root_Signature(rootDesc);
		dxr.hit.chs.pRootSignature->SetName(L"chs.pRootSignature");
	}

	void Create_Pipeline_State_Object(Dx_Instance &d3d, DXRGlobal &dxr)
	{
		// Need 10 subobjects:
		// (0)1 for RGS program
		// (1)1 for Miss program
		// (2)1 for CHS program
		// (3)1 for Hit Group
		// (4)2 for RayGen Root Signature (root-signature and association)
		// (6)2 for Shader Config (config and association)	//raygen & miss
		// (6)2 for Shader Config (config and association) //miss
		// (6)2 for Shader Config (config and association) //Hit group
		// (8)1 for Global Root Signature
		// (9)1 for Pipeline Config	
		UINT index = 0;
		std::vector<D3D12_STATE_SUBOBJECT> subobjects;
		subobjects.resize(14);

		// Add state subobject for the RGS
		D3D12_EXPORT_DESC rgsExportDesc = {};
		rgsExportDesc.Name = L"RayGen_12";
		rgsExportDesc.ExportToRename = L"RayGen";
		rgsExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;

		D3D12_DXIL_LIBRARY_DESC	rgsLibDesc = {};
		rgsLibDesc.DXILLibrary.BytecodeLength = dxr.rgs.blob->GetBufferSize();
		rgsLibDesc.DXILLibrary.pShaderBytecode = dxr.rgs.blob->GetBufferPointer();
		rgsLibDesc.NumExports = 1;
		rgsLibDesc.pExports = &rgsExportDesc;

		D3D12_STATE_SUBOBJECT rgs = {};
		rgs.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
		rgs.pDesc = &rgsLibDesc;

		subobjects[index++] = rgs;//(0)

		// Add state subobject for the Miss shader
		D3D12_EXPORT_DESC msExportDesc = {};
		msExportDesc.Name = L"Miss_5";
		msExportDesc.ExportToRename = L"Miss";
		msExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;

		D3D12_DXIL_LIBRARY_DESC	msLibDesc = {};
		msLibDesc.DXILLibrary.BytecodeLength = dxr.miss.blob->GetBufferSize();
		msLibDesc.DXILLibrary.pShaderBytecode = dxr.miss.blob->GetBufferPointer();
		msLibDesc.NumExports = 1;
		msLibDesc.pExports = &msExportDesc;

		D3D12_STATE_SUBOBJECT ms = {};
		ms.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
		ms.pDesc = &msLibDesc;

		subobjects[index++] = ms;//(1)

		// Add state subobject for the Closest Hit shader-----------------------------------------
		D3D12_EXPORT_DESC chsExportDesc = {};
		chsExportDesc.Name = L"ClosestHit_76";
		chsExportDesc.ExportToRename = L"ClosestHit";
		chsExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;

		D3D12_DXIL_LIBRARY_DESC	chsLibDesc = {};
		chsLibDesc.DXILLibrary.BytecodeLength = dxr.hit.chs.blob->GetBufferSize();
		chsLibDesc.DXILLibrary.pShaderBytecode = dxr.hit.chs.blob->GetBufferPointer();
		chsLibDesc.NumExports = 1;
		chsLibDesc.pExports = &chsExportDesc;

		D3D12_STATE_SUBOBJECT chs = {};
		chs.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
		chs.pDesc = &chsLibDesc;

		subobjects[index++] = chs; //(2)

		// Add a state subobject for the hit group---------------------------------------------
		D3D12_HIT_GROUP_DESC hitGroupDesc = {};
		hitGroupDesc.ClosestHitShaderImport = L"ClosestHit_76";
		hitGroupDesc.HitGroupExport = L"HitGroup";

		D3D12_STATE_SUBOBJECT hitGroup = {};
		hitGroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
		hitGroup.pDesc = &hitGroupDesc;

		subobjects[index++] = hitGroup;// (3)
		{
			// Add a state subobject for the shader payload configuration
			D3D12_RAYTRACING_SHADER_CONFIG shaderDesc = {};
			shaderDesc.MaxPayloadSizeInBytes = sizeof(float) * 8;	// RGB and HitT
			shaderDesc.MaxAttributeSizeInBytes = D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;

			D3D12_STATE_SUBOBJECT shaderConfigObject = {};
			shaderConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
			shaderConfigObject.pDesc = &shaderDesc;

			subobjects[index++] = shaderConfigObject;//(4)

			// Create a list of the shader export names that use the payload
			const WCHAR* shaderExports[] = { L"RayGen_12", L"Miss_5", L"HitGroup" };

			// Add a state subobject for the association between shaders and the payload
			D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderPayloadAssociation = {};
			shaderPayloadAssociation.NumExports = _countof(shaderExports);
			shaderPayloadAssociation.pExports = shaderExports;
			shaderPayloadAssociation.pSubobjectToAssociate = &subobjects[(index - 1)];//(4)

			D3D12_STATE_SUBOBJECT shaderPayloadAssociationObject = {};
			shaderPayloadAssociationObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
			shaderPayloadAssociationObject.pDesc = &shaderPayloadAssociation;

			subobjects[index++] = shaderPayloadAssociationObject;//(5)
		}

		//-------------------------------------------------
		{
			// Add a state subobject for the shared root signature 
			D3D12_STATE_SUBOBJECT rayGenRootSigObject = {};
			rayGenRootSigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
			rayGenRootSigObject.pDesc = &dxr.rgs.pRootSignature;

			subobjects[index++] = rayGenRootSigObject;//(6)

			// Create a list of the shader export names that use the root signature
			const WCHAR* rootSigExports[] = { L"RayGen_12", };

			// Add a state subobject for the association between the RayGen shader and the local root signature
			D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION rayGenShaderRootSigAssociation = {};
			rayGenShaderRootSigAssociation.NumExports = _countof(rootSigExports);
			rayGenShaderRootSigAssociation.pExports = rootSigExports;
			rayGenShaderRootSigAssociation.pSubobjectToAssociate = &subobjects[(index - 1)];//(6)

			D3D12_STATE_SUBOBJECT rayGenShaderRootSigAssociationObject = {};
			rayGenShaderRootSigAssociationObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
			rayGenShaderRootSigAssociationObject.pDesc = &rayGenShaderRootSigAssociation;

			subobjects[index++] = rayGenShaderRootSigAssociationObject;//(7)
		}
		{
			// Add a state subobject for the shared root signature 
			D3D12_STATE_SUBOBJECT rayGenRootSigObject = {};
			rayGenRootSigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
			rayGenRootSigObject.pDesc = &dxr.miss.pRootSignature;

			subobjects[index++] = rayGenRootSigObject;//(6)

			// Create a list of the shader export names that use the root signature
			const WCHAR* rootSigExports[] = { L"Miss_5", };

			// Add a state subobject for the association between the RayGen shader and the local root signature
			D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION rayGenShaderRootSigAssociation = {};
			rayGenShaderRootSigAssociation.NumExports = _countof(rootSigExports);
			rayGenShaderRootSigAssociation.pExports = rootSigExports;
			rayGenShaderRootSigAssociation.pSubobjectToAssociate = &subobjects[(index - 1)];//(6)

			D3D12_STATE_SUBOBJECT rayGenShaderRootSigAssociationObject = {};
			rayGenShaderRootSigAssociationObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
			rayGenShaderRootSigAssociationObject.pDesc = &rayGenShaderRootSigAssociation;

			subobjects[index++] = rayGenShaderRootSigAssociationObject;//(7)
		}
		//-----------------------------------------------------

		{
			// Add a state subobject for the shared root signature 
			D3D12_STATE_SUBOBJECT rayGenRootSigObject = {};
			rayGenRootSigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
			rayGenRootSigObject.pDesc = &dxr.hit.chs.pRootSignature;

			subobjects[index++] = rayGenRootSigObject;//(6)

			// Create a list of the shader export names that use the root signature
			const WCHAR* rootSigExports[] = { L"HitGroup", };

			// Add a state subobject for the association between the RayGen shader and the local root signature
			D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION rayGenShaderRootSigAssociation = {};
			rayGenShaderRootSigAssociation.NumExports = _countof(rootSigExports);
			rayGenShaderRootSigAssociation.pExports = rootSigExports;
			rayGenShaderRootSigAssociation.pSubobjectToAssociate = &subobjects[(index - 1)];//(6)

			D3D12_STATE_SUBOBJECT rayGenShaderRootSigAssociationObject = {};
			rayGenShaderRootSigAssociationObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
			rayGenShaderRootSigAssociationObject.pDesc = &rayGenShaderRootSigAssociation;

			subobjects[index++] = rayGenShaderRootSigAssociationObject;//(7)
		}
		//-----------------------------------------------------

		D3D12_STATE_SUBOBJECT globalRootSig;
		globalRootSig.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
		globalRootSig.pDesc = &dxr.globalSignature;

		subobjects[index++] = globalRootSig;//(8)

		// Add a state subobject for the ray tracing pipeline config
		D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
		pipelineConfig.MaxTraceRecursionDepth = 1;

		D3D12_STATE_SUBOBJECT pipelineConfigObject = {};
		pipelineConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
		pipelineConfigObject.pDesc = &pipelineConfig;

		subobjects[index++] = pipelineConfigObject;//(9)

		// Describe the Ray Tracing Pipeline State Object
		D3D12_STATE_OBJECT_DESC pipelineDesc = {};
		pipelineDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
		pipelineDesc.NumSubobjects = static_cast<UINT>(subobjects.size());
		pipelineDesc.pSubobjects = subobjects.data();


		// Create the RT Pipeline State Object (RTPSO)
		HRESULT hr = d3d.device->CreateStateObject(&pipelineDesc, IID_PPV_ARGS(&dxr.rtpso));
		if (FAILED(hr))
		{
			ri.Error(ERR_FATAL, "Error: failed to create state object!");
		}

		// Get the RTPSO properties
		hr = dxr.rtpso->QueryInterface(IID_PPV_ARGS(&dxr.rtpsoInfo));

		if (FAILED(hr))
		{
			ri.Error(ERR_FATAL, "Error: failed to get RTPSO info object!");
		}
	}

	void Create_Shader_Table(Dx_Instance &d3d, DXRGlobal &dxr, Dx_World &world)
	{
		/*
		The Shader Table layout is as follows:
		Entry 0 - Ray Generation shader
		Entry 1 - Miss shader
		Entry 2 - Closest Hit shader
		All shader records in the Shader Table must have the same size, so shader record size will be based on the largest required entry.
		The ray generation program requires the largest entry: sizeof(shader identifier) + 8 bytes for a descriptor table.
		The entry size must be aligned up to D3D12_RAYTRACING_SHADER_BINDING_TABLE_RECORD_BYTE_ALIGNMENT
		*/

		dxr_acceleration_structure_manager& drxAsm = d3d.dxr->acceleration_structure_manager;

		//int bottomLevelMeshCount = drxAsm.MeshCount();
		int bottomLevelMeshCount = MAX_BOTTOM_LEVEL_MESH_COUNT; //not sure why i can't use MeshCount, makes me think i have an aligment error

		uint32_t sbtSize = 0;

		dxr.sbtEntrySize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		dxr.sbtEntrySize += 8;					// CBV/SRV/UAV descriptor table  or indexOffSet & vertexOffSet
		dxr.sbtEntrySize += 8;//padab & padab
		dxr.sbtEntrySize = ALIGN(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, dxr.sbtEntrySize);

		sbtSize = (dxr.sbtEntrySize * (3 + bottomLevelMeshCount));		// (3)!! shader records in the table
		sbtSize = ALIGN(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, sbtSize);

		// Create the shader table buffers
		D3D12BufferCreateInfo bufferInfo(sbtSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
		Create_Buffer(d3d, bufferInfo, &dxr.sbt);
	}

	void Update_Shader_Table(Dx_Instance &d3d, DXRGlobal &dxr, Dx_World &world)
	{
		dxr_acceleration_structure_manager& drxAsm = d3d.dxr->acceleration_structure_manager;

		//int bottomLevelMeshCount = drxAsm.MeshCount();
		int bottomLevelMeshCount = MAX_BOTTOM_LEVEL_MESH_COUNT; //not sure why i can't use MeshCount, makes me think i have an aligment error

		// Map the buffer
		uint8_t* pData;
		HRESULT hr = dxr.sbt->Map(0, nullptr, (void**)&pData);
		if (FAILED(hr))
		{
			ri.Error(ERR_FATAL, "Error: failed to map shader table!");
		}
		uint8_t* pDataStart = pData;

		// Entry 0 - Ray Generation program and local root argument data (descriptor table with constant buffer and IB/VB pointers)
		memcpy(pData, dxr.rtpsoInfo->GetShaderIdentifier(L"RayGen_12"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

		// Set the root arguments data. Point to start of descriptor heap
		*reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = d3d.cbvSrvUavRayGenHeaps->GetGPUDescriptorHandleForHeapStart();

		// Entry 1 - Miss program (no local root arguments to set)
		pData += dxr.sbtEntrySize;
		memcpy(pData, dxr.rtpsoInfo->GetShaderIdentifier(L"Miss_5"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);


		for (int i = 0; i < bottomLevelMeshCount; ++i)
		{
			// Entry 2 - Closest Hit program and local root argument data (descriptor table with constant buffer and IB/VB pointers)
			pData += dxr.sbtEntrySize;
			memcpy(pData, dxr.rtpsoInfo->GetShaderIdentifier(L"HitGroup"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

			UINT indexStart = drxAsm.MeshStartIndexIndex(i);
			UINT vertexStart = drxAsm.MeshStartVertexIndex(i);
			*reinterpret_cast<UINT32*>(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + 0) = indexStart;
			*reinterpret_cast<UINT32*>(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + 4) = vertexStart;
			*reinterpret_cast<UINT32*>(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + 8) = 0;
			*reinterpret_cast<UINT32*>(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + 12) = 0;
		}

		// Unmap
		dxr.sbt->Unmap(0, nullptr);
	}

	void Create_CBVSRVUAV_Heap(Dx_Instance &d3d, DXRGlobal &dxr, Dx_World &world)
	{
		// Describe the CBV/SRV/UAV heap
		// Need 7 entries:
		// 1 CBV for the ViewCB
		// 1 CBV for the MaterialCB
		// 1 UAV for the RT output
		// 1 SRV for the Scene BVH

		// 1 SRV for the index buffer
		// 1 SRV for the vertex buffer
		// 1 SRV for the texture
		// 1 SRV for the texture
		// 1 SRV for the depth texture

		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = 9;
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		// Create the descriptor heap
		HRESULT hr = d3d.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&d3d.cbvSrvUavRayGenHeaps));
		if (FAILED(hr))
		{
			ri.Error(ERR_FATAL, "Error: failed to create DXR CBV/SRV/UAV descriptor heap!");
		}

		// Get the descriptor heap handle and increment size
		D3D12_CPU_DESCRIPTOR_HANDLE handle = d3d.cbvSrvUavRayGenHeaps->GetCPUDescriptorHandleForHeapStart();
		UINT handleIncrement = d3d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		// Create the ViewCB CBV
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.SizeInBytes = ALIGN(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, sizeof(*world.viewCBData));
		cbvDesc.BufferLocation = world.viewCB->GetGPUVirtualAddress();

		d3d.device->CreateConstantBufferView(&cbvDesc, handle);

		// Create the MaterialCB CBV
		cbvDesc.SizeInBytes = ALIGN(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, sizeof(*world.materialCBData));
		cbvDesc.BufferLocation = world.materialCB->GetGPUVirtualAddress();

		handle.ptr += handleIncrement;
		d3d.device->CreateConstantBufferView(&cbvDesc, handle);

		// Create the DXR output buffer UAV
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

		handle.ptr += handleIncrement;
		d3d.device->CreateUnorderedAccessView(d3d.DXROutput, nullptr, &uavDesc, handle);

		// Create the DXR Top Level Acceleration Structure SRV
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.RaytracingAccelerationStructure.Location = dxr.TLAS.pResult->GetGPUVirtualAddress();

		handle.ptr += handleIncrement;
		d3d.device->CreateShaderResourceView(nullptr, &srvDesc, handle);

		// Create the index buffer SRV
		D3D12_SHADER_RESOURCE_VIEW_DESC indexSRVDesc;
		indexSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		indexSRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		indexSRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
		indexSRVDesc.Buffer.StructureByteStride = 0;
		indexSRVDesc.Buffer.FirstElement = 0;

		UINT64 indexBuferByts = MEGA_INDEX_VERTEX_BUFFER_SIZE;
		indexSRVDesc.Buffer.NumElements = indexBuferByts / sizeof(float);

		indexSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		handle.ptr += handleIncrement;
		d3d.device->CreateShaderResourceView(d3d.resources->indexBufferMega, &indexSRVDesc, handle);

		// Create the vertex buffer SRV
		D3D12_SHADER_RESOURCE_VIEW_DESC vertexSRVDesc;
		vertexSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		vertexSRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		vertexSRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
		vertexSRVDesc.Buffer.StructureByteStride = 0;
		vertexSRVDesc.Buffer.FirstElement = 0;

		UINT64 vertexBuferByts = MEGA_INDEX_VERTEX_BUFFER_SIZE;
		vertexSRVDesc.Buffer.NumElements = vertexBuferByts / sizeof(float);
		vertexSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		handle.ptr += handleIncrement;
		d3d.device->CreateShaderResourceView(d3d.resources->vertexBufferMega, &vertexSRVDesc, handle);

		// Create the material texture SRV
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
			textureSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			textureSRVDesc.Texture2D.MipLevels = 1;
			textureSRVDesc.Texture2D.MostDetailedMip = 0;
			textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

			handle.ptr += handleIncrement;
			d3d.device->CreateShaderResourceView(d3d.dx_renderTargets->GetRenderTargetTexture(dx_renderTargets::G_BUFFER_ALBEDO_RT), &textureSRVDesc, handle);
		}

		// Create the material texture SRV
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
			textureSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			textureSRVDesc.Texture2D.MipLevels = 1;
			textureSRVDesc.Texture2D.MostDetailedMip = 0;
			textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

			handle.ptr += handleIncrement;
			d3d.device->CreateShaderResourceView(d3d.dx_renderTargets->GetRenderTargetTexture(dx_renderTargets::G_BUFFER_NORMALS_RT), &textureSRVDesc, handle);
		}

		// Create the material depth SRV
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
			textureSRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
			textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			textureSRVDesc.Texture2D.MipLevels = 1;
			textureSRVDesc.Texture2D.MostDetailedMip = 0;
			textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			handle.ptr += handleIncrement;
			d3d.device->CreateShaderResourceView(d3d.dx_renderTargets->GetDepthStencilBuffer(dx_renderTargets::G_BUFFER_DEPTH_RT), &textureSRVDesc, handle);
		}
	}

	void Create_DXR_Output(Dx_Instance &d3d)
	{
		assert(d3d.width > 0);
		assert(d3d.height > 0);
		// Describe the DXR output resource (texture)
		// Dimensions and format should match the swapchain
		// Initialize as a copy source, since we will copy this buffer's contents to the swapchain
		D3D12_RESOURCE_DESC desc = {};
		desc.DepthOrArraySize = 1;
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		desc.Width = d3d.width;
		desc.Height = d3d.height;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;

		static const D3D12_HEAP_PROPERTIES defaultHeapProperties =
		{
			D3D12_HEAP_TYPE_DEFAULT,
			D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			D3D12_MEMORY_POOL_UNKNOWN,
			0, 0
		};

		// Create the buffer resource
		HRESULT hr = d3d.device->CreateCommittedResource(&defaultHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&desc, D3D12_RESOURCE_STATE_COPY_SOURCE,
			nullptr,
			IID_PPV_ARGS(&d3d.DXROutput));
		if (FAILED(hr))
		{
			ri.Error(ERR_FATAL, "Error: failed to create DXR output buffer!");
		}
	}

	void Build_Command_List(Dx_Instance &d3d, DXRGlobal &dxr, Dx_World &world)
	{
		Update_View_CB(d3d, dxr, world);

		D3D12_RESOURCE_BARRIER OutputBarriers = {};

		// Transition the DXR output buffer to a copy source
		OutputBarriers.Transition.pResource = d3d.DXROutput;
		OutputBarriers.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		OutputBarriers.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		OutputBarriers.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		// Wait for the transitions to complete
		d3d.command_list->ResourceBarrier(1, &OutputBarriers);

		// Bind the empty root signature
		d3d.command_list->SetComputeRootSignature(dxr.globalSignature);

		// Set the UAV/SRV/CBV and sampler heaps
		ID3D12DescriptorHeap* ppHeaps[] = { d3d.cbvSrvUavRayGenHeaps };
		UINT ppHeapsCount = _countof(ppHeaps);
		d3d.command_list->SetDescriptorHeaps(ppHeapsCount, ppHeaps);

		DXR::Update_Shader_Table(d3d, *d3d.dxr, world);

		// Dispatch rays
		D3D12_DISPATCH_RAYS_DESC desc = {};
		desc.RayGenerationShaderRecord.StartAddress = dxr.sbt->GetGPUVirtualAddress();
		desc.RayGenerationShaderRecord.SizeInBytes = dxr.sbtEntrySize;

		desc.MissShaderTable.StartAddress = dxr.sbt->GetGPUVirtualAddress() + dxr.sbtEntrySize;
		desc.MissShaderTable.SizeInBytes = dxr.sbtEntrySize;		// Only a single Miss program entry
		desc.MissShaderTable.StrideInBytes = dxr.sbtEntrySize;

		dxr_acceleration_structure_manager& drxAsm = d3d.dxr->acceleration_structure_manager;
		int meshCount = drxAsm.MeshCount();

		desc.HitGroupTable.StartAddress = dxr.sbt->GetGPUVirtualAddress() + (dxr.sbtEntrySize * 2);
		desc.HitGroupTable.SizeInBytes = dxr.sbtEntrySize * meshCount;
		desc.HitGroupTable.StrideInBytes = dxr.sbtEntrySize;

		desc.Width = d3d.width;
		desc.Height = d3d.height;
		desc.Depth = 1;

		d3d.command_list->SetPipelineState1(dxr.rtpso);
		d3d.command_list->DispatchRays(&desc);

		// Transition DXR output to a copy source
		OutputBarriers.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		OutputBarriers.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

		// Wait for the transitions to complete
		d3d.command_list->ResourceBarrier(1, &OutputBarriers);
	}

	void Destroy(Dx_Instance &d3d, DXRGlobal &dxr)
	{
		delete d3d.shaderCompiler;

		SAFE_RELEASE(dxr.TLAS.pScratch);
		SAFE_RELEASE(dxr.TLAS.pResult);
		SAFE_RELEASE(dxr.TLAS.pInstanceDesc);
		for (int i = 0; i < dxr.BLASs.size(); ++i)
		{
			SAFE_RELEASE(dxr.BLASs[i].pScratch);
			SAFE_RELEASE(dxr.BLASs[i].pResult);
			SAFE_RELEASE(dxr.BLASs[i].pInstanceDesc);

		}
		dxr.BLASs.clear();
		SAFE_RELEASE(dxr.sbt);
		//SAFE_RELEASE(dxr.rgs.blob);
		//SAFE_RELEASE(dxr.miss.blob);
		//SAFE_RELEASE(dxr.hit.chs.blob);
		SAFE_RELEASE(dxr.rtpso);
		SAFE_RELEASE(dxr.rtpsoInfo);
	}

}
