#include "tr_local.h"

#include "D3d12.h"

#include "dxr.h"
#include "dx_shaders.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN			// Exclude rarely-used items from Windows headers.
#endif

#pragma warning(push)
#pragma warning(disable:4191)
#include <Windows.h>
#include <wrl.h>
#include <atlcomcli.h>

#pragma warning(pop)


namespace D3DShaders
{
	void Compile_Shader(D3D12ShaderCompilerInfo &compilerInfo, D3D12ShaderInfo &info, IDxcBlob** blob)
	{
		HRESULT hr;
		UINT32 codePage(0);
		IDxcBlobEncoding* pShaderText(nullptr);

		// Load and encode the shader file
		hr = compilerInfo.library->CreateBlobFromFile(info.filename, &codePage, &pShaderText);

		if (FAILED(hr))
		{
			ri.Error(ERR_FATAL, "Error: failed to create blob from shader file!");
		}

		// Create the compiler include handler
		CComPtr<IDxcIncludeHandler> dxcIncludeHandler;
		hr = compilerInfo.library->CreateIncludeHandler(&dxcIncludeHandler);
		if (FAILED(hr))
		{
			ri.Error(ERR_FATAL, "Error: failed to create include handler");
		}

		// Compile the shader
		IDxcOperationResult* result;
		hr = compilerInfo.compiler->Compile(pShaderText, info.filename, L"", info.targetProfile, nullptr, 0, nullptr, 0, dxcIncludeHandler, &result);
		if (FAILED(hr))
		{
			ri.Error(ERR_FATAL, "Error: failed to compile shader!");
		}

		// Verify the result
		result->GetStatus(&hr);
		if (FAILED(hr))
		{
			IDxcBlobEncoding* error;
			hr = result->GetErrorBuffer(&error);
			if (FAILED(hr))
			{
				ri.Error(ERR_FATAL, "Error: failed to get shader compiler error buffer!");
			}

			// Convert error blob to a string
			std::vector<char> infoLog(error->GetBufferSize() + 1);
			memcpy(infoLog.data(), error->GetBufferPointer(), error->GetBufferSize());
			infoLog[error->GetBufferSize()] = 0;

			std::string errorMsg = "Shader Compiler Error:\n";
			errorMsg.append(infoLog.data());

			MessageBoxA(nullptr, errorMsg.c_str(), "Error!", MB_OK);
			return;
		}

		hr = result->GetResult(blob);
		if (FAILED(hr))
		{
			ri.Error(ERR_FATAL, "Error: failed to get shader blob result!");
		}
	}

	void Compile_Shader(D3D12ShaderCompilerInfo &compilerInfo, RtProgram &program)
	{
		Compile_Shader(compilerInfo, program.info, &program.blob);
		program.SetBytecode();
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

	void Destroy(D3D12ShaderCompilerInfo &shaderCompiler)
	{
		SAFE_RELEASE(shaderCompiler.compiler);
		SAFE_RELEASE(shaderCompiler.library);
		shaderCompiler.DxcDllHelper.Cleanup();
	}
}