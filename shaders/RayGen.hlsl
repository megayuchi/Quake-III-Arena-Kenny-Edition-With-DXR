/* Copyright (c) 2018-2019, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Common.hlsl"

//random ray from https://gist.github.com/andrewzimmer906/b0e05920c7b947604bd9a5a50cfc2155

// ---[ Ray Generation Shader ]---
#define PI 3.14159265359
//Random number [0:1] without sine
#define HASHSCALE1 0.1031

#define DISPLAYGAMMA 2.2

float hash(float p)
{
	float3 p3 = frac(float3(p, p, p) * HASHSCALE1);
	p3 += dot(p3, p3.yzx + 19.19);
	return frac((p3.x + p3.y) * p3.z);
}

float3 randomSphereDir(float2 rnd)
{
	float s = rnd.x*PI*2.0;
	float t = rnd.y*2. - 1.;
	return float3(sin(s), cos(s), t) / sqrt(1.0 + t * t);
}

float3 randomHemisphereDir(float3 dir, float i)
{
	float3 v = randomSphereDir(float2(hash(i + 1.0), hash(i + 200.0)));
	return v * sign(dot(v, dir));
}

float ShadowTest(float3 origin, float3 direction)
{
	HitInfo payload;
	RayDesc rayShadow;
	rayShadow.Origin = origin;
	rayShadow.Direction = direction;

	rayShadow.TMin = 0.1f;
	rayShadow.TMax = 1000.f;

	TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, rayShadow, payload);
	
	float inshadow = payload.HitPos.w != 0.0 ? 0.0f : 1.0f;

	return inshadow;
}

float pow2(float x)
{
	return x * x;
}

float distanceSq(float3 A, float3 B)
{
	return pow2(B.x - A.x) + pow2(B.y - A.y) + pow2(B.z - A.z);
}

float4 doLight(float3 toLight, float3 hitPos, float3 normal, float3 camPos)
{
	float lightAngle = dot(toLight, normal);

	float l = hash((hitPos.x + hitPos.y*200.0f + hitPos.z*2000.0f));
	float3 toLightBend = normalize(toLight + (0.01f * randomHemisphereDir(toLight, l)));
	

	float inshadow = ShadowTest(hitPos, toLightBend);

	//spec
	float3 viewDir = camPos - hitPos;
	viewDir = normalize(viewDir);
	float3 H = normalize(toLight + viewDir);

	float NdotH = dot(normal, H);
	float spec = pow(saturate(NdotH), 64);
	
	float light = (lightAngle + spec) * inshadow;

	return float4(light, light, light, 1.0f);
}

[shader("raygeneration")]
void RayGen()
{
	//this came from https://github.com/acmarrs/IntroToDXR originally 
	uint2 LaunchIndex = DispatchRaysIndex().xy;
	uint2 LaunchDimensions = DispatchRaysDimensions().xy;

	float2 d = (((LaunchIndex.xy + 0.5f) / resolution.xy) * 2.f - 1.f);
	float aspectRatio = (resolution.x / resolution.y);

	// Setup the ray
	RayDesc ray;
	ray.Origin = viewOriginAndTanHalfFovY.xyz;
	ray.Direction = normalize(
		(d.x * view[0].xyz * viewOriginAndTanHalfFovY.w * aspectRatio)
		- 
		(d.y * view[1].xyz * viewOriginAndTanHalfFovY.w) + view[2].xyz);

	ray.TMin = 0.1f;
	ray.TMax = 10000.f;	

	// Trace the ray
	HitInfo payload;
	payload.HitNormal = float4(0.f, 0.f, 0.f, 0.f);
	TraceRay(
		SceneBVH,
		RAY_FLAG_NONE,
		0xFF,//InstanceInclusionMask
		0,//RayContributionToHitGroupIndex
		0,//MultiplierForGeometryContributionToHitGroupIndex (I can't get this to work, has to use InstanceContributionToHitGroupIndex. Makes me think there is something wrong with my shader table)
		0,//MissShaderIndex
		ray,
		payload);

	LaunchIndex.x = LaunchDimensions.x - LaunchIndex.x;
	RTOutput[LaunchIndex.xy] = float4(0.0f, 0.0, 0.0f, 1.0f);

	if (0)
	{
		payload.HitNormal.w = 1.0f;
		float4 normal0_1 = (payload.HitNormal + float4(1, 1, 1, 0)) * float4(0.5f, 0.5f, 0.5f, 1);
		//RTOutput[LaunchIndex.xy] = ;//show normals

		float3 color = albedo.Load(int3(LaunchIndex.xy, 0)).rgb;
		RTOutput[LaunchIndex.xy] = float4(color, 1.0f);
	}
	
	if (payload.HitPos.w != 0.0f)
	{
		float4 texColor = albedo.Load(int3(LaunchIndex.xy, 0)).rgba;

		
		texColor.rgb = pow(texColor.rgb, DISPLAYGAMMA);
		

		float3 toLight = normalize(light.xyz);
		RTOutput[LaunchIndex.xy] = texColor * doLight(toLight, payload.HitPos.xyz, payload.HitNormal.xyz, ray.Origin.xyz);

		//1st bounce
		{
			float3 origin = payload.HitPos.xyz;
			float3 normal = payload.HitNormal.xyz;
			const int nbIte = 16;
			const float nbIteInv = 1.0f / float(nbIte);
			float max = 500.0f;

			for (int i = 0; i < nbIte; i++)
			{
				float l = hash(float(i) + (origin.x + origin.y*200.0f + origin.z*2000.0f)); //Try and make a hash thats "stable" in world space
				float3 normalBend = normalize(normal + randomHemisphereDir(normal, l));

				HitInfo payloadBounce;
				payloadBounce.HitNormal = float4(0.f, 0.f, 0.f, 0.f);
				RayDesc rayBounce;
				rayBounce.Origin = payload.HitPos.xyz;
				rayBounce.Direction = normalBend;

				rayBounce.TMin = 0.1f;
				rayBounce.TMax = max;

				TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, rayBounce, payloadBounce);

				if (payloadBounce.HitPos.w != 0.0f)
				{
					//whats the light at the hit spot
					float3 origin2 = payloadBounce.HitPos.xyz;
					float3 normal2 = payloadBounce.HitNormal.xyz;
					
					float destSq = distanceSq(origin, origin2);
					float lightAttenuation = clamp(5000.0f / destSq, 0, 1);

					RTOutput[LaunchIndex.xy] += texColor * lightAttenuation * nbIteInv * doLight(toLight, origin2, normal2, ray.Origin.xyz);
				}				
			}
		}
		RTOutput[LaunchIndex.xy].rgb = pow(RTOutput[LaunchIndex.xy].rgb, 1.0 / DISPLAYGAMMA);
	}	

	//output green dots at the sky box, just used to make sure my raygen shader is getting called
	RTOutput[LaunchIndex.xy] = (payload.HitPos.w == 0.0 && 0 == LaunchIndex.x % 20 && 0 == LaunchIndex.y % 20) ? float4(0.25, 0.75, 0.5, 1.f) : RTOutput[LaunchIndex.xy];	
}
