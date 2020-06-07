
#include "Shared.hlsl"

#define DISPLAYGAMMA 2.2

Texture2D<float4> LightTex			: register(t0);
Texture2D<float4> Albedo		: register(t1);

RWTexture2D<float4> Result				: register(u0);

cbuffer cb0 : register(b0)
{
	float2 resolution;
	float2 resolutionInv;
	float near;
	float far;
}

cbuffer ViewCB : register(b1)
{
	matrix projMatrix;
	matrix projMatrixInv;
	matrix viewMatrix;
	matrix viewMatrixInv;	
};

cbuffer ViewCBLast : register(b2)
{
	matrix viewMatrixLast;
	matrix viewMatrixLastInv;
};

[numthreads(8, 8, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID)
{
	float4 albedoLinner = pow(Albedo[DTid.xy], DISPLAYGAMMA);

	Result[DTid.xy] = LightTex[DTid.xy] * albedoLinner;

	Result[DTid.xy] = pow(Result[DTid.xy], 1.0 / DISPLAYGAMMA);
}
