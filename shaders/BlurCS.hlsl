//Mostly from...
//https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingRealTimeDenoisedAmbientOcclusion

#include "Shared.hlsl"

#define DEBUGOUTPUTS 0

Texture2D<float4> LightTex : register(t0);
Texture2D<float4> LastLightTex : register(t1);
Texture2D<float4> NormalTex : register(t2);
Texture2D<float4> NormalLastTex : register(t3);
Texture2D<float> g_inDepth	: register(t4, space0);
Texture2D<float> DepthLast	: register(t5, space0);
Texture2DArray<float4> NoiseTex : register(t6);
Texture2D<uint> SamplesPerPixelTex : register(t7);

RWTexture2D<float4> g_inOutValue : register(u0);

SamplerState sampler0 : register(s0);

cbuffer cb0 : register(b0)
{
	float2 resolution;
	float2 resolutionInv;
	float debug;
	float step;
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

#define DISPLAYGAMMA 2.2
#define HitDistanceOnMiss 0

namespace FilterKernel
{
	static const unsigned int Radius = 1;
	static const unsigned int Width = 1 + 2 * Radius;
	static const float Kernel1D[Width] = { 0.27901, 0.44198, 0.27901 };
	static const float Kernel[Width][Width] =
	{
		{ Kernel1D[0] * Kernel1D[0], Kernel1D[0] * Kernel1D[1], Kernel1D[0] * Kernel1D[2] },
		{ Kernel1D[1] * Kernel1D[0], Kernel1D[1] * Kernel1D[1], Kernel1D[1] * Kernel1D[2] },
		{ Kernel1D[2] * Kernel1D[0], Kernel1D[2] * Kernel1D[1], Kernel1D[2] * Kernel1D[2] },
	};
}

// Group shared memory cache for the row aggregated results.
static const uint NumValuesToLoadPerRowOrColumn = 8 + (FilterKernel::Width - 1);
groupshared float4 PackedValueDepthCache[NumValuesToLoadPerRowOrColumn][8];   // 16bit float value, depth.
groupshared float4 FilteredResultCache[NumValuesToLoadPerRowOrColumn][8];    // 32 bit float filteredValue.


bool IsWithinBounds(in int2 index, in int2 dimensions)
{
	return index.x >= 0 && index.y >= 0 && index.x < dimensions.x && index.y < dimensions.y;
}

namespace RTAO {
	//static const float RayHitDistanceOnMiss = 0;
	static const float InvalidAOCoefficientValue = -1;
	/*bool HasAORayHitAnyGeometry(in float tHit)
	{
		return tHit != RayHitDistanceOnMiss;
	}*/
}

bool IsInRange(in float val, in float min, in float max)
{
	return (val >= min && val <= max);
}

uint Float2ToHalf(in float2 val)
{
	uint result = 0;
	result = f32tof16(val.x);
	result |= f32tof16(val.y) << 16;
	return result;
}

float2 HalfToFloat2(in uint val)
{
	float2 result;
	result.x = f16tof32(val);
	result.y = f16tof32(val >> 16);
	return result;
}


//-------------------------------------------
// Find a DTID with steps in between the group threads and groups interleaved to cover all pixels.
uint2 GetPixelIndex(in uint2 Gid, in uint2 GTid)
{
	uint2 GroupDim = uint2(8, 8);
	uint2 groupBase = (Gid / step) * GroupDim * step + Gid % step;
	uint2 groupThreadOffset = GTid * step;
	uint2 sDTid = groupBase + groupThreadOffset;

	return sDTid;
}

float LinnerDepth(float z)
{
	float4 clipSpacePosition = float4(0, 0, z, 1.0);
	float4 viewSpacePosition = mul(projMatrixInv, clipSpacePosition);

	// Perspective division
	viewSpacePosition.xyz /= viewSpacePosition.w;

	return viewSpacePosition.z;
}

// Load up to 16x16 pixels and filter them horizontally.
// The output is cached in Shared Memory and contains NumRows x 8 results.
void FilterHorizontally(in uint2 Gid, in uint GI)
{
	const uint2 GroupDim = uint2(8, 8);

	// Processes the thread group as row-major 4x16, where each sub group of 16 threads processes one row.
	// Each thread loads up to 4 values, with the sub groups loading rows interleaved.
	// Loads up to 4x16x4 == 256 input values.
	uint2 GTid4x16_row0 = uint2(GI % 16, GI / 16);
	int2 GroupKernelBasePixel = GetPixelIndex(Gid, 0) - int(FilterKernel::Radius * step);
	const uint NumRowsToLoadPerThread = 4;
	const uint Row_BaseWaveLaneIndex = (WaveGetLaneIndex() / 16) * 16;

	[unroll]
	for (uint i = 0; i < NumRowsToLoadPerThread; i++)
	{
		uint2 GTid4x16 = GTid4x16_row0 + uint2(0, i * 4);
		if (GTid4x16.y >= NumValuesToLoadPerRowOrColumn)
		{
			break;
		}

		// Load all the contributing columns for each row.
		int2 pixel = GroupKernelBasePixel + GTid4x16 * step;
		float4 value = RTAO::InvalidAOCoefficientValue;
		float depth = 0;

		// The lane is out of bounds of the GroupDim + kernel, 
		// but could be within bounds of the input texture,
		// so don't read it from the texture.
		// However, we need to keep it as an active lane for a below split sum.

		if (GTid4x16.x < NumValuesToLoadPerRowOrColumn && IsWithinBounds(pixel, resolution))
		{
			value = g_inOutValue[pixel];
			//depth = LinnerDepth(g_inDepth[pixel]) * NormalTex[pixel].w;//GUI flag in normal.w
			depth = NormalTex[pixel].w;//GUI flag in normal.w
		}

		// Cache the kernel center values.
		if (IsInRange(GTid4x16.x, FilterKernel::Radius, FilterKernel::Radius + GroupDim.x - 1))
		{
			PackedValueDepthCache[GTid4x16.y][GTid4x16.x - FilterKernel::Radius] = float4(value.xyz, depth);
		}

		// Filter the values for the first GroupDim columns.
		{
			// Accumulate for the whole kernel width.
			float4 weightedValueSum = 0;
			float weightSum = 0;
			float4 gaussianWeightedValueSum = 0;
			float gaussianWeightedSum = 0;

			// Since a row uses 16 lanes, but we only need to calculate the aggregate for the first half (8) lanes,
			// split the kernel wide aggregation among the first 8 and the second 8 lanes, and then combine them.


			// Get the lane index that has the first value for a kernel in this lane.
			uint Row_KernelStartLaneIndex =
				(Row_BaseWaveLaneIndex + GTid4x16.x)
				- (GTid4x16.x < GroupDim.x
					? 0
					: GroupDim.x);

			// Get values for the kernel center.
			uint kcLaneIndex = Row_KernelStartLaneIndex + FilterKernel::Radius;
			float4 kcValue = WaveReadLaneAt(value, kcLaneIndex);
			float kcDepth = WaveReadLaneAt(depth, kcLaneIndex);

			// Initialize the first 8 lanes to the center cell contribution of the kernel. 
			// This covers the remainder of 1 in FilterKernel::Width / 2 used in the loop below. 
			if (GTid4x16.x < GroupDim.x && kcValue.r != RTAO::InvalidAOCoefficientValue && kcDepth != HitDistanceOnMiss)
			{
				float w_h = FilterKernel::Kernel1D[FilterKernel::Radius];
				gaussianWeightedValueSum = w_h * kcValue;
				gaussianWeightedSum = w_h;
				weightedValueSum = gaussianWeightedValueSum;
				weightSum = w_h;
			}

			// Second 8 lanes start just past the kernel center.
			uint KernelCellIndexOffset =
				GTid4x16.x < GroupDim.x
				? 0
				: (FilterKernel::Radius + 1); // Skip over the already accumulated center cell of the kernel.


			// For all columns in the kernel.
			for (uint c = 0; c < FilterKernel::Radius; c++)
			{
				uint kernelCellIndex = KernelCellIndexOffset + c;

				uint laneToReadFrom = Row_KernelStartLaneIndex + kernelCellIndex;
				float4 cValue = WaveReadLaneAt(value, laneToReadFrom);
				float cDepth = WaveReadLaneAt(depth, laneToReadFrom);

				if (cValue.r != RTAO::InvalidAOCoefficientValue && kcDepth != HitDistanceOnMiss && cDepth != HitDistanceOnMiss)
				{
					float w_h = FilterKernel::Kernel1D[kernelCellIndex];

					// Simple depth test with tolerance growing as the kernel radius increases.
					// Goal is to prevent values too far apart to blend together, while having 
					// the test being relaxed enough to get a strong blurring result.
					float depthThreshold = 0.05 + step * 0.001 * abs(int(FilterKernel::Radius) - c);
					//float w_d = abs(kcDepth - cDepth) <= depthThreshold * kcDepth;
					//float w_d = abs(kcDepth - cDepth) < 75.0f;

					float w_d = kcDepth == cDepth;


					float w = w_h * w_d;

					weightedValueSum += w * cValue;
					weightSum += w;
					gaussianWeightedValueSum += w_h * cValue;
					gaussianWeightedSum += w_h;
				}
			}

			// Combine the sub-results.
			uint laneToReadFrom = min(WaveGetLaneCount() - 1, Row_BaseWaveLaneIndex + GTid4x16.x + GroupDim.x);
			weightedValueSum += WaveReadLaneAt(weightedValueSum, laneToReadFrom);
			weightSum += WaveReadLaneAt(weightSum, laneToReadFrom);
			gaussianWeightedValueSum += WaveReadLaneAt(gaussianWeightedValueSum, laneToReadFrom);
			gaussianWeightedSum += WaveReadLaneAt(gaussianWeightedSum, laneToReadFrom);

			// Store only the valid results, i.e. first GroupDim columns.
			if (GTid4x16.x < GroupDim.x)
			{
				float4 gaussianFilteredValue = gaussianWeightedSum > 1e-6 ? gaussianWeightedValueSum / gaussianWeightedSum : RTAO::InvalidAOCoefficientValue;
				float4 filteredValue = weightSum > 1e-6 ? weightedValueSum / weightSum : gaussianFilteredValue;

				FilteredResultCache[GTid4x16.y][GTid4x16.x] = filteredValue;
			}
		}
	}
}

void FilterVertically(uint2 DTid, in uint2 GTid, in float blurStrength)
{
	// Kernel center values.
	float4 kcValueDepth = PackedValueDepthCache[GTid.y + FilterKernel::Radius][GTid.x];
	float4 kcValue = float4(kcValueDepth.xyz, 1.0f);
	float kcDepth = kcValueDepth.w;

	float4 filteredValue = kcValue;
	if (blurStrength >= 0.01 && kcDepth != HitDistanceOnMiss)
	{
		float4 weightedValueSum = 0;
		float weightSum = 0;
		float4 gaussianWeightedValueSum = 0;
		float gaussianWeightSum = 0;

		// For all rows in the kernel.
		[unroll]
		for (uint r = 0; r < FilterKernel::Width; r++)
		{
			uint rowID = GTid.y + r;

			float4 rUnpackedValueDepth = PackedValueDepthCache[rowID][GTid.x];
			float rDepth = rUnpackedValueDepth.w;
			float4 rFilteredValue = FilteredResultCache[rowID][GTid.x];

			if (rDepth != HitDistanceOnMiss && rFilteredValue.r != RTAO::InvalidAOCoefficientValue)
			{
				float w_h = FilterKernel::Kernel1D[r];

				// Simple depth test with tolerance growing as the kernel radius increases.
				// Goal is to prevent values too far apart to blend together, while having 
				// the test being relaxed enough to get a strong blurring result.
				float depthThreshold = 0.05 + step * 0.001 * abs(int(FilterKernel::Radius) - int(r));
				//float w_d = abs(kcDepth - rDepth) <= depthThreshold * kcDepth;
				//float w_d = abs(kcDepth - rDepth) < 75.0f;
				float w_d = kcDepth == rDepth;
				float w = w_h * w_d;

				weightedValueSum += w * rFilteredValue;
				weightSum += w;
				gaussianWeightedValueSum += w_h * rFilteredValue;
				gaussianWeightSum += w_h;
			}
		}
		float4 gaussianFilteredValue = gaussianWeightSum > 1e-6 ? gaussianWeightedValueSum / gaussianWeightSum : RTAO::InvalidAOCoefficientValue;
		filteredValue = weightSum > 1e-6 ? weightedValueSum / weightSum : gaussianFilteredValue;
		filteredValue = filteredValue != RTAO::InvalidAOCoefficientValue ? lerp(kcValue, filteredValue, blurStrength) : filteredValue;
	}
	g_inOutValue[DTid] = filteredValue;
}

[numthreads(8, 8, 1)]
void main(uint2 Gid : SV_GroupID, uint2 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
	uint2 sDTid = GetPixelIndex(Gid, GTid);
	// Pass through if all pixels have 0 blur strength set.
	float blurStrength = 0.0f;
	{
		if (GI == 0)
			FilteredResultCache[0][0] = 0;
		GroupMemoryBarrierWithGroupSync();

		float TsppRatio = min(SamplesPerPixelTex[sDTid.xy], 12) / 12.0f;
		//TsppRatio = 0.0f;
		blurStrength = pow(1 - TsppRatio, 1);

		float MinBlurStrength = 0.01;
		bool valueNeedsFiltering = blurStrength >= MinBlurStrength;
		if (valueNeedsFiltering)
			FilteredResultCache[0][0] = 1;

		GroupMemoryBarrierWithGroupSync();

		if (FilteredResultCache[0][0].r == 0)
		{
			return;
		}
	}

	FilterHorizontally(Gid, GI);
	GroupMemoryBarrierWithGroupSync();

	FilterVertically(sDTid, GTid, blurStrength);

#if DEBUGOUTPUTS
	if (1 == debug)
	{
		if (step == 4)
		{
			g_inOutValue[sDTid] = float4(blurStrength, 1.0, 0.0, 1.0);
		}
	}

	if (2 == debug)
	{
		if (SamplesPerPixelTex[sDTid.xy] > 1)
		{
			g_inOutValue[sDTid] = float4(0.0, 1.0, 0.0, 1.0);
		}
	}
#endif
}
