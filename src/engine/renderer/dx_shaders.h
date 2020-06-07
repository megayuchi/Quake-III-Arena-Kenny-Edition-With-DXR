#pragma once

#include "D3d12.h"
#include "dxc/dxcapi.h"
#include "dxc/dxcapi.use.h"


namespace D3DShaders
{
	struct D3D12ShaderCompilerInfo
	{
		dxc::DxcDllSupport		DxcDllHelper;
		IDxcCompiler*			compiler;
		IDxcLibrary*			library;

		D3D12ShaderCompilerInfo()
		{
			compiler = nullptr;
			library = nullptr;
		}
	};

	struct D3D12ShaderInfo
	{
		LPCWSTR		filename;
		LPCWSTR		entryPoint;
		LPCWSTR		targetProfile;

		D3D12ShaderInfo(LPCWSTR inFilename, LPCWSTR inEntryPoint, LPCWSTR inProfile)
		{
			filename = inFilename;
			entryPoint = inEntryPoint;
			targetProfile = inProfile;
		}

		D3D12ShaderInfo()
		{
			filename = NULL;
			entryPoint = NULL;
			targetProfile = NULL;
		}
	};


	struct RtProgram
	{
		RtProgram(D3D12ShaderInfo shaderInfo)
		{
			info = shaderInfo;
			blob = nullptr;
			subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
			exportName = shaderInfo.entryPoint;
			exportDesc.ExportToRename = nullptr;
			exportDesc.Flags = D3D12_EXPORT_FLAG_NONE;

			pRootSignature = nullptr;
		}

		void SetBytecode()
		{
			exportDesc.Name = exportName.c_str();

			dxilLibDesc.NumExports = 1;
			dxilLibDesc.pExports = &exportDesc;
			dxilLibDesc.DXILLibrary.BytecodeLength = blob->GetBufferSize();
			dxilLibDesc.DXILLibrary.pShaderBytecode = blob->GetBufferPointer();

			subobject.pDesc = &dxilLibDesc;
		}

		RtProgram()
		{
			blob = nullptr;
			exportDesc.ExportToRename = nullptr;
			pRootSignature = nullptr;
		}

		D3D12ShaderInfo			info = {};
		IDxcBlob*				blob;

		ID3D12RootSignature*	pRootSignature = nullptr;
		D3D12_DXIL_LIBRARY_DESC	dxilLibDesc;
		D3D12_EXPORT_DESC		exportDesc;
		D3D12_STATE_SUBOBJECT	subobject;
		std::wstring			exportName;
	};


	void Init_Shader_Compiler(D3D12ShaderCompilerInfo &shaderCompiler);
	void Compile_Shader(D3D12ShaderCompilerInfo &compilerInfo, RtProgram &program);
	void Compile_Shader(D3D12ShaderCompilerInfo &compilerInfo, D3D12ShaderInfo &info, IDxcBlob** blob);
	void Destroy(D3D12ShaderCompilerInfo &shaderCompiler);
}

