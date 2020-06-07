#pragma once

#include "D3d12.h"

#include "dx_postProcess.h"

#include <algorithm>
#include "dx_shaders.h"
#include "dx_renderTargets.h"

struct ID3D12Device5;
struct Dx_Instance;

class dx_postProcessTemporalReproject: public dx_postProcess
{

public:
	dx_postProcessTemporalReproject();
	~dx_postProcessTemporalReproject();


	void Init_CBVSRVUAV_Heap() override;
	void Render() override;

private:

	void InitShader(D3DShaders::D3D12ShaderCompilerInfo &shaderCompiler) override;
	void InitPipeline() override;

	
};

