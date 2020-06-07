
#include "Common.hlsl"
#include "Shared.hlsl"

#define DISPLAYGAMMA 2.2

#define DEBUGOUTPUTS 0

float4 randomRandText(int2 index)
{
	int3 index3D = int3(index.xy, frame);
	int3 screenPos = fmod(index3D, int3(256, 256, 64));//Hardcoded, not good
	
	return (NoiseTex[screenPos] * 2.0) - 1.0;
}

float3 randomHemisphereDir(float3 dir, int i)
{
	uint2 LaunchIndex = DispatchRaysIndex().xy;
	float3 v = normalize(randomRandText(LaunchIndex.xy + uint2(i*100,i*1000)).xyz);

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

float3 WorldPosFromDepth(uint2 screenIndex)
{
	float2 texCoord = screenIndex.xy / resolution.xy;

	float depthDist = depth.Load(int3(screenIndex.xy, 0));

	float z = depthDist;

	texCoord.y = 1.0f - texCoord.y;

	float4 clipSpacePosition = float4(texCoord * 2.0 - 1.0, z, 1.0);
	float4 viewSpacePosition = mul(projMatrixInv, clipSpacePosition);

	// Perspective division
	viewSpacePosition /= viewSpacePosition.w;

	float4 worldSpacePosition = mul(viewMatrixInv, viewSpacePosition);

	return worldSpacePosition.xyz;
}

HitInfo GetViewWorldHit_RayTrace()
{
	//this came from https://github.com/acmarrs/IntroToDXR originally 
	uint2 LaunchIndex = DispatchRaysIndex().xy;
	uint2 LaunchDimensions = DispatchRaysDimensions().xy;
	LaunchIndex.x = LaunchDimensions.x - LaunchIndex.x;

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

	return payload;
}

HitInfo GetViewWorldHit_G_Buffer()
{
	HitInfo payload;

	uint2 LaunchIndex = DispatchRaysIndex().xy;

	//normal
	float4 normal0_1 = normalTex.Load(int3(LaunchIndex.xy, 0));
	payload.HitNormal = normal0_1 * float4(2.0f, 2.0f, 2.0f, 1.0f) - float4(1.0f, 1.0f, 1.0f, 0.0f);//NOTE nonlite flag is packed into normal alpha

	//pos
	float3 worldPos = WorldPosFromDepth(LaunchIndex.xy);

	worldPos += payload.HitNormal.xyz * 1.0f;

	payload.HitPos = float4(worldPos, 1.0f);

	return payload;
}

float4 WorldPosToAlbedo(float4 worldPos, float4 failColor)
{
	int2 LaunchDimensions = DispatchRaysDimensions().xy;

	float4 positionView = mul(viewMatrix, worldPos);
	float4 screenPos = mul(projMatrix, positionView);

	float3 pos = screenPos.xyz / screenPos.w;
	float2 pos0_1 = (pos.xy + 1.0f) * 0.5f;

	pos0_1.y = 1.0 - pos0_1.y;

	float4 texColor = albedoTex.Load(int3(pos0_1.xy * LaunchDimensions, 0)).rgba;
	texColor.rgb = pow(texColor.rgb, DISPLAYGAMMA);

	uint2 LaunchIndex = DispatchRaysIndex().xy;
	float depthDist = depth.Load(int3(LaunchIndex.xy, 0));
	float diff = abs(depthDist - pos.z);

	if ((depthDist + 0.001f) < pos.z || (depthDist - 0.01f) > pos.z)
	{
		texColor = failColor;
	}
	return texColor;
}

float4 LightAtPoint(float3 toLight, float3 hitPos, float3 normal)
{
	float lightAngle = dot(toLight, normal);

	float3 toLightBend = normalize(toLight + (0.01f * randomHemisphereDir(toLight, 0)));

	float inshadow = ShadowTest(hitPos, toLightBend);
	
	return lightAngle * inshadow;
}

float4 LightFromPoint(float3 fromPoint, float3 origin, float3 normal)
{
	float destSq = distanceSq(fromPoint, origin);
	float lightAttenuation = clamp(2000.0f / destSq, 0, 1);
	float3 toFromPoint = normalize(origin - fromPoint);
	float lightAngle = dot(toFromPoint, normal);

	float4 bounceColor = WorldPosToAlbedo(float4(fromPoint, 1.0f), float4(0, 0, 0, 1));
	
	return lightAttenuation * bounceColor /** lightAngle*/;
}

[shader("raygeneration")]
void RayGen()
{
	uint2 LaunchIndex = DispatchRaysIndex().xy;
	uint2 LaunchDimensions = DispatchRaysDimensions().xy;

	//HitInfo payload = GetViewWorldHit_RayTrace();
	HitInfo payload = GetViewWorldHit_G_Buffer();	

	RTOutput[LaunchIndex.xy] = float4(0.0f, 0.0, 0.0f, 1.0f);
#if DEBUGOUTPUTS
	if (2 == debug)//show normals
	{
		payload.HitNormal.w = 1.0f;
		float4 normal0_1 = (payload.HitNormal + float4(1, 1, 1, 0)) * float4(0.5f, 0.5f, 0.5f, 1);
		RTOutput[LaunchIndex.xy] = normal0_1;
		return;
	}
#endif	
	if (payload.HitPos.w != 0.0f && payload.HitNormal.a > 0.0f)
	{
#if DEBUGOUTPUTS
		if (3 == debug)//show normals
		{
			float3 worldPos = WorldPosFromDepth(LaunchIndex.xy);
			worldPos = frac(worldPos / 100.0f);
			RTOutput[LaunchIndex.xy] = float4(worldPos, 1);
			return;
		}
#endif		

		float3 toLight = normalize(light.xyz);
		RTOutput[LaunchIndex.xy] = float4(0, 0, 0, 1);
		float directColorMask = LightAtPoint(toLight, payload.HitPos.xyz, payload.HitNormal.xyz).r;
	
		//1st bounce
		{
			float3 origin = payload.HitPos.xyz;
			float3 normal = payload.HitNormal.xyz;			
			const int nbIte = 2;
			const float nbIteInv = 1.0f / float(nbIte);
			float max = 250.0f;

			for (int i = 0; i < nbIte; i++)
			{
				float3 normalBend = normalize(normal + randomHemisphereDir(normal, i));

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

					//second bounce			
					float4 lightAtSecondPoint = 0.0f;
					for (int j = 0; j < nbIte; j++)
					{
						float3 normalBend2 = normalize(normal2 + randomHemisphereDir(normal2, nbIte +j));

						HitInfo payloadBounce2;
						payloadBounce2.HitNormal = float4(0.f, 0.f, 0.f, 0.f);
						RayDesc rayBounce2;
						rayBounce2.Origin = origin2;
						rayBounce2.Direction = normalBend2;
						rayBounce2.TMin = 1.1f;
						rayBounce2.TMax = max;

						TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, rayBounce2, payloadBounce2);

						if (payloadBounce2.HitPos.w != 0.0f)
						{
							//whats the light at the hit spot
							float3 origin3 = payloadBounce2.HitPos.xyz;
							float3 normal3 = payloadBounce2.HitNormal.xyz;

							float4 lightAtHit = 100.0f * LightAtPoint(toLight, origin3, normal3);
							float4 lightFromHit = LightFromPoint(origin3, origin3, normal);
							float4 secondLight = lightFromHit * lightAtHit * nbIteInv*nbIteInv;
#if DEBUGOUTPUTS
							if (4 == debug|| 5 == debug || 6 == debug)//show bounce
							{
								secondLight = float4(0, 0, 0, 1);//cancel out direct light
							}
#endif
							RTOutput[LaunchIndex.xy] += secondLight;
						}
					}

					float4 lightAtHit = 100.0f * LightAtPoint(toLight, origin2, normal2);
					float4 lightFromHit = LightFromPoint(origin2, origin, normal);
					float4 firstLight = lightFromHit * lightAtHit * nbIteInv;
#if DEBUGOUTPUTS
					if (5 == debug || 6 == debug)//show bounce
					{
						firstLight = float4(0, 0, 0, 1);//cancel out direct light
					}
#endif
					
					RTOutput[LaunchIndex.xy] += firstLight;					
				}
			}
		}

#if DEBUGOUTPUTS
		if (6 == debug)//show bounce
		{
			directColorMask = 0;
		}
#endif			

		RTOutput[LaunchIndex.xy].rgb = RTOutput[LaunchIndex.xy].rgb + directColorMask;
		RTOutput[LaunchIndex.xy].a = directColorMask;
	}
	else
	{
		RTOutput[LaunchIndex.xy] = 1.0f;//for now just sky gets here
	}
}
