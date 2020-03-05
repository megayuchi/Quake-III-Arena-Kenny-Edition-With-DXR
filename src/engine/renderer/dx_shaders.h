#pragma once


struct D3D12ShaderCompilerInfo;
struct RtProgram;
struct D3D12ShaderInfo;

namespace D3DShaders
{
	void Init_Shader_Compiler(D3D12ShaderCompilerInfo &shaderCompiler);
	void Compile_Shader(D3D12ShaderCompilerInfo &compilerInfo, RtProgram &program);
	void Compile_Shader(D3D12ShaderCompilerInfo &compilerInfo, D3D12ShaderInfo &info, IDxcBlob** blob);
	void Destroy(D3D12ShaderCompilerInfo &shaderCompiler);
}

