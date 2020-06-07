//Mostly from...
//https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingRealTimeDenoisedAmbientOcclusion

#include "Shared.hlsl"

#define DEBUGOUTPUTS 0

Texture2D<float4> LightTex			: register(t0);
Texture2D<float4> LastLightTex		: register(t1);
Texture2D<float4> NormalTex			: register(t2);
Texture2D<float4> NormalLastTex		: register(t3);
Texture2D<float> Depth				: register(t4, space0);
Texture2D<float> DepthLast			: register(t5, space0);
Texture2D<float2> velocityTex		: register(t6, space0);
Texture2D<uint> SamplesPerPixelTex	: register(t7);

RWTexture2D<float4> Result				: register(u0);
RWTexture2D<uint> SamplesPerPixelResult	: register(u1);

SamplerState sampler0 : register(s0);

cbuffer cb0 : register(b0)
{
	float2 resolution;
	float2 resolutionInv;
	float debug;
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

float4 ComputeCachedValue(float2 adjustedCacheFrameTexturePos, float4 Weights);

float LinnerDepth(float z)
{
	return z;
	float4 clipSpacePosition = float4(0, 0, z, 1.0);
	float4 viewSpacePosition = mul(projMatrixInv, clipSpacePosition);

	// Perspective division
	viewSpacePosition.xyz /= viewSpacePosition.w;

	return viewSpacePosition.z;
}

float4 GetWeights(in float2 TargetOffset)
{
	float4 bilinearWeights =
		float4(
		(1 - TargetOffset.x) * (1 - TargetOffset.y),
			TargetOffset.x * (1 - TargetOffset.y),
			(1 - TargetOffset.x) * TargetOffset.y,
			TargetOffset.x * TargetOffset.y);

	return bilinearWeights;
}

float2 ComputeAdjustedCacheFrameTexture(float2 cacheFrameTexturePos)
{
	int2 topLeftCacheFrameIndex = floor(cacheFrameTexturePos * resolution - 0.5);
	float2 adjustedCacheFrameTexturePos = (topLeftCacheFrameIndex + 0.5) * resolutionInv;

	return adjustedCacheFrameTexturePos;
}

float4 GetWeightsNormal(
	in float3 TargetNormal,
	in float3 SampleNormals[4]
)
{
	float4 NdotSampleN = float4(
		dot(TargetNormal, SampleNormals[0]),
		dot(TargetNormal, SampleNormals[1]),
		dot(TargetNormal, SampleNormals[2]),
		dot(TargetNormal, SampleNormals[3]));

	const float Sigma = 1;      // Bump the sigma a bit to add tolerance for slight geometry misalignments and/or format precision limitations.
	const float SigmaExponent = 8;

	NdotSampleN *= Sigma;

	float4 normalWeights = pow(saturate(NdotSampleN), SigmaExponent);

	return normalWeights;
}

float4 ComputeWeights(float2 cacheFrameTexturePos, float2 adjustedCacheFrameTexturePos, float4 currentNormal, float4 currentDepth, uint3 DTid)
{
	float4 NorX = NormalLastTex.GatherRed(sampler0, adjustedCacheFrameTexturePos).wzxy;
	float4 NorY = NormalLastTex.GatherGreen(sampler0, adjustedCacheFrameTexturePos).wzxy;
	float4 NorZ = NormalLastTex.GatherBlue(sampler0, adjustedCacheFrameTexturePos).wzxy;
	float4 NorW = NormalLastTex.GatherAlpha(sampler0, adjustedCacheFrameTexturePos).wzxy;

	float3 normalsSam[4] = {
		(float3(NorX[0], NorY[0], NorZ[0]) *2.0f - 1.0f),
		(float3(NorX[1], NorY[1], NorZ[1]) *2.0f - 1.0f),
		(float3(NorX[2], NorY[2], NorZ[2]) *2.0f - 1.0f),
		(float3(NorX[3], NorY[3], NorZ[3]) *2.0f - 1.0f)
	};

	int2 topLeftCacheFrameIndex = floor(cacheFrameTexturePos * resolution - 0.5);
	float2 cachePixelOffset = cacheFrameTexturePos * resolution - 0.5f - topLeftCacheFrameIndex;

	float4 WeightsNormal = GetWeightsNormal(currentNormal.xyz, normalsSam);

	float4 v = DepthLast.GatherRed(sampler0, adjustedCacheFrameTexturePos).wzxy;

	float depthTest = 0.0050f;
	float4 WeightsDepth;
	WeightsDepth[0] = abs(LinnerDepth(v[0]) - currentDepth) < depthTest;
	WeightsDepth[1] = abs(LinnerDepth(v[1]) - currentDepth) < depthTest;
	WeightsDepth[2] = abs(LinnerDepth(v[2]) - currentDepth) < depthTest;
	WeightsDepth[3] = abs(LinnerDepth(v[3]) - currentDepth) < depthTest;

	float4 WeightsGui;
	WeightsGui[0] = NorW[0] == currentNormal.w;
	WeightsGui[1] = NorW[1] == currentNormal.w;
	WeightsGui[2] = NorW[2] == currentNormal.w;
	WeightsGui[3] = NorW[3] == currentNormal.w;

	float4 Weights = GetWeights(cachePixelOffset);
#if DEBUGOUTPUTS
	if (2 == debug)
	{
		SamplesPerPixelResult[DTid.xy] = 33;
		Result[DTid.xy] = WeightsGui;
	}

	if (3 == debug)
	{
		SamplesPerPixelResult[DTid.xy] = 33;
		Result[DTid.xy] = WeightsNormal;
	}
	if (4 == debug)
	{
		SamplesPerPixelResult[DTid.xy] = 33;
		Result[DTid.xy] = WeightsDepth;
	}

	if (5 == debug)
	{
		SamplesPerPixelResult[DTid.xy] = 33;

		Weights *= WeightsNormal;
		Weights *= WeightsGui;
		Weights *= WeightsDepth;

		Result[DTid.xy] = Weights;
	}
#endif
	
	Weights *= WeightsNormal;
	Weights *= WeightsGui;
	Weights *= WeightsDepth;

	return Weights;
}

float4 ComputeOnScreenWeights(float2 uv)
{
	int2 topLeftCacheFrameIndex = floor(uv * resolution - 0.5);

	const int2 srcIndexOffsets[4] = { {0, 0}, {1, 0}, {0, 1}, {1, 1} };

	int2 cacheIndices[4] = {
		topLeftCacheFrameIndex + srcIndexOffsets[0],
		topLeftCacheFrameIndex + srcIndexOffsets[1],
		topLeftCacheFrameIndex + srcIndexOffsets[2],
		topLeftCacheFrameIndex + srcIndexOffsets[3] };

	return float4((cacheIndices[0].x >= 0 && cacheIndices[0].y >= 0 && cacheIndices[0].x < resolution.x && cacheIndices[0].y < resolution.y),
		(cacheIndices[1].x >= 0 && cacheIndices[1].y >= 0 && cacheIndices[1].x < resolution.x && cacheIndices[1].y < resolution.y),
		(cacheIndices[2].x >= 0 && cacheIndices[2].y >= 0 && cacheIndices[2].x < resolution.x && cacheIndices[2].y < resolution.y),
		(cacheIndices[3].x >= 0 && cacheIndices[3].y >= 0 && cacheIndices[3].x < resolution.x && cacheIndices[3].y < resolution.y));
}

float4 ComputeCachedValue(float2 adjustedCacheFrameTexturePos, float4 Weights)
{
	float4 colR = LastLightTex.GatherRed(sampler0, adjustedCacheFrameTexturePos).wzxy;
	float4 colG = LastLightTex.GatherGreen(sampler0, adjustedCacheFrameTexturePos).wzxy;
	float4 colB = LastLightTex.GatherBlue(sampler0, adjustedCacheFrameTexturePos).wzxy;
	float4 colA = LastLightTex.GatherAlpha(sampler0, adjustedCacheFrameTexturePos).wzxy;
	
	float4 r0 = float4(colR[0], colG[0], colB[0], colA[0]);
	float4 r1 = float4(colR[1], colG[1], colB[1], colA[1]);
	float4 r2 = float4(colR[2], colG[2], colB[2], colA[2]);
	float4 r3 = float4(colR[3], colG[3], colB[3], colA[3]);

	float4 output = r0 * Weights[0] +
		r1 * Weights[1] +
		r2 * Weights[2] +
		r3 * Weights[3];

	return output;
}

uint4 ComputeCachedTspp(float2 adjustedCacheFrameTexturePos)
{
	uint4 col = SamplesPerPixelTex.GatherRed(sampler0, adjustedCacheFrameTexturePos).wzxy;
	return col;
}

void BlendOutput(uint3 DTid, uint Tspp, float4 cachedValue)
{
	float4 value = LightTex[DTid.xy];

	if (Tspp > 0)
	{
		uint maxTspp = 33;
		Tspp = min(Tspp + 1, maxTspp);

		float invTspp = 1.f / Tspp;
		float a = max(invTspp, 0.0303030303030303f);
		float MaxSmoothingFactor = 1;
		a = min(a, MaxSmoothingFactor);

		// Value.
		value = lerp(cachedValue, value, a);
	}
	else
	{
		Tspp = 1;
		value = value;
	}
	SamplesPerPixelResult[DTid.xy] = Tspp;
	Result[DTid.xy] = value;
}

[numthreads(8, 8, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID)
{
	float2 velocity = velocityTex[DTid.xy].rg;
	velocity.y = -velocity.y;
	
	float2 texCoord = float2(DTid.xy + 0.5) / resolution.xy;//sample the center of the pixel
	float2 cacheFrameTexturePos = texCoord + velocity;
	float currentDepth = Depth[DTid.xy].r;

	float4 currentNormal = NormalTex[DTid.xy] * float4(2.0f, 2.0f, 2.0f, 1.0f) - float4(1.0f, 1.0f, 1.0f, 0.0f);
	float2 adjustedCacheFrameTexturePos = ComputeAdjustedCacheFrameTexture(cacheFrameTexturePos);

	float4 weights = ComputeWeights(cacheFrameTexturePos, adjustedCacheFrameTexturePos, currentNormal, LinnerDepth(currentDepth), DTid);
	weights *= ComputeOnScreenWeights(cacheFrameTexturePos);

#if DEBUGOUTPUTS
	if (5 == debug)
	{
		weights = float4(1, 1, 1, 1);
	}
#endif

	float weightSum = dot(1, weights);
	float4 cachedValue = float4(0, 0, 0, 0);

	uint tspp = 0;
	bool areCacheValuesValid = weightSum > 1e-3f;
	if (areCacheValuesValid)
	{
		uint4 vCachedTspp = ComputeCachedTspp(adjustedCacheFrameTexturePos);
		vCachedTspp = max(1, vCachedTspp);

		float4 nWeights = weights / weightSum;   // Normalize the weights.

		float cachedTspp = dot(nWeights, vCachedTspp);
		tspp = round(cachedTspp);

		if (tspp > 0)
		{
			cachedValue = ComputeCachedValue(adjustedCacheFrameTexturePos, nWeights);
		}
	}

#if DEBUGOUTPUTS
	if (debug < 2)
	{
		BlendOutput(DTid, tspp, cachedValue);
	}
#else
	BlendOutput(DTid, tspp, cachedValue);
#endif

	SamplesPerPixelResult[DTid.xy] = currentNormal.w == 0.0f ? 33 : SamplesPerPixelResult[DTid.xy]; //skybox

#if DEBUGOUTPUTS
	if (1 == debug)
	{
		Result[DTid.xy] = LightTex[DTid.xy];
		SamplesPerPixelResult[DTid.xy] = 33;
	}

	if (6 == debug)
	{
		Result[DTid.xy] = LightTex[DTid.xy];
		SamplesPerPixelResult[DTid.xy] = 1;
	}
#endif
}
