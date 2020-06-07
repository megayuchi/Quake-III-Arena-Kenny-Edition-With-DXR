//NOTE run compile_hlsl.bat

struct Single_Texture_PS_Data {
	float4 position : SV_POSITION;
	float4 normal : NORMAL;
	float4 color  : COLOR;
	float2 uv0 : TEXCOORD;
	float4 positionThis : TEXCOORD1;
	float4 positionLast : TEXCOORD2;
};

struct Multi_Texture_PS_Data {
	float4 position : SV_POSITION;
	float4 normal : NORMAL;
	float4 color  : COLOR;
	float2 uv0 : TEXCOORD0;
	float2 uv1 : TEXCOORD1;
	float4 positionThis : TEXCOORD2;
	float4 positionLast : TEXCOORD3;
};

cbuffer Constants : register(b0) {
	float4x4 world_xform;
	float4x4 worldLast_xform;
	float4x4 worldViewProj_xform;
	float flags;
	//float4x3 eye_space_xform;
	//float4 clipping_plane; // in eye space
};

cbuffer Constants : register(b1) {
	float4x4 view_xform;//not used
	float4x4 viewLast_xform;
	float4x4 projection_xform;
};

struct Pixel_Output {
	float4 Albedo;
	float4 Normal;
	float2 Velocity;
};

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

Texture2D texture1 : register(t1);
SamplerState sampler1 : register(s1);

float4 ScreenPosition(float4 position, float4x4 world, float4x4 view)
{
	float4 positionWorld = mul(world, position);
	float4 positionView = mul(view, positionWorld);
	return mul(projection_xform, positionView);
}

Single_Texture_PS_Data single_texture_vs(
	float4 position : POSITION,
	float4 normal : NORMAL,
	float4 color : COLOR,
	float2 uv0 : TEXCOORD)
{
	Single_Texture_PS_Data ps_data;
	ps_data.position = mul(worldViewProj_xform, position);
	ps_data.positionThis = ps_data.position;
	ps_data.positionLast = ScreenPosition(position, worldLast_xform, viewLast_xform);

	ps_data.position = ps_data.position;

	ps_data.normal = mul(world_xform, float4(normal.xyz, 0));
	ps_data.normal.w = 1.0f;

	ps_data.color = color;
	ps_data.uv0 = uv0;
	return ps_data;
}

Single_Texture_PS_Data single_texture_clipping_plane_vs(
	float4 position : POSITION,
	float4 normal : NORMAL,
	float4 color : COLOR,
	float2 uv0 : TEXCOORD,
	out float clip : SV_ClipDistance)
{
	/*clip = dot(clipping_plane.xyz, mul(position, eye_space_xform)) + clipping_plane.w;

	Single_Texture_PS_Data ps_data;
	ps_data.position = ScreenPosition(position, world_xform, view_xform);
	ps_data.positionThis = ps_data.position;
	ps_data.positionLast = ScreenPosition(position, worldLast_xform, viewLast_xform);

	ps_data.normal = mul(world_xform, float4(normal.xyz, 0));
	ps_data.normal.w = 1.0f;

	ps_data.color = color;
	ps_data.uv0 = uv0;
	return ps_data;*/
	clip = 1.0f;
	Single_Texture_PS_Data ps_data;
	ps_data.position = mul(worldViewProj_xform, position);// ScreenPosition(position, world_xform, view_xform);
	ps_data.positionThis = ps_data.position;
	ps_data.positionLast = ScreenPosition(position, worldLast_xform, viewLast_xform);

	ps_data.normal = mul(world_xform, float4(normal.xyz, 0));
	ps_data.normal.w = 1.0f;

	ps_data.position = ps_data.position;

	ps_data.color = color;
	ps_data.uv0 = uv0;
	return ps_data;
}

Multi_Texture_PS_Data multi_texture_vs(
	in uint VertID : SV_VertexID,
	float4 position : POSITION,
	float4 normal : NORMAL,
	float4 color : COLOR,
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1)
{
	Multi_Texture_PS_Data ps_data;
	ps_data.position = mul(worldViewProj_xform, position);// ScreenPosition(position, world_xform, view_xform);
	ps_data.positionThis = ps_data.position;
	ps_data.positionLast = ScreenPosition(position, worldLast_xform, viewLast_xform);

	ps_data.normal = mul(world_xform, float4(normal.xyz, 0));
	ps_data.normal.w = 1.0f;

	ps_data.position = ps_data.position;

	ps_data.color = color;
	ps_data.uv0 = uv0;
	ps_data.uv1 = uv1;
	return ps_data;
}

Multi_Texture_PS_Data multi_texture_clipping_plane_vs(
	float4 position : POSITION,
	float4 normal : NORMAL,
	float4 color : COLOR,
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1,
	out float clip : SV_ClipDistance)
{
	/*clip = dot(clipping_plane.xyz, mul(position, eye_space_xform)) + clipping_plane.w;

	Multi_Texture_PS_Data ps_data;
	ps_data.position = ScreenPosition(position, world_xform, view_xform);
	ps_data.positionThis = ps_data.position;
	ps_data.positionLast = ScreenPosition(position, worldLast_xform, viewLast_xform);

	ps_data.normal = mul(world_xform, float4(normal.xyz, 0));
	ps_data.normal.w = 1.0f;

	ps_data.color = color;
	ps_data.uv0 = uv0;
	ps_data.uv1 = uv1;
	return ps_data;*/
	clip = 1.0f;
	Multi_Texture_PS_Data ps_data;
	ps_data.position = mul(worldViewProj_xform, position);// ScreenPosition(position, world_xform, view_xform);
	ps_data.positionThis = ps_data.position;
	ps_data.positionLast = ScreenPosition(position, worldLast_xform, viewLast_xform);

	ps_data.position = ps_data.position;

	ps_data.normal = mul(world_xform, float4(normal.xyz, 0));
	ps_data.normal.w = 1.0f;

	ps_data.color = color;
	ps_data.uv0 = uv0;
	ps_data.uv1 = uv1;
	return ps_data;
}

//-----------------------------------------

float2 CompteVelocity(float4 position, float4 positionLast)
{
	float2 pos = ((position.xy / position.w) + 1.0) * 0.5;
	float2 posLast = ((positionLast.xy / positionLast.w) + 1.0f) * 0.5;

	float2 dif = (posLast - pos);
	return dif;
}

Pixel_Output single_texture_ps(Single_Texture_PS_Data data) : SV_TARGET{
	//vertex lit world

	float4 out_color = data.color * texture0.Sample(sampler0, data.uv0);
	float4 normal = (data.normal + float4(1, 1, 1, 0)) * float4(0.5f, 0.5f, 0.5f, 1); //show normals

#if defined(ALPHA_TEST_GT0)
	if (out_color.a == 0.0f) discard;
#elif defined(ALPHA_TEST_LT80)
	if (out_color.a >= 0.5f) discard;
#elif defined(ALPHA_TEST_GE80)
	if (out_color.a < 0.5f) discard;
#endif

	normal.a = flags;//pack materialId in noraml.w

	Pixel_Output output;
	output.Albedo = out_color;
	output.Normal = normal;
	output.Velocity = CompteVelocity(data.positionThis, data.positionLast);

	return output;
}

Pixel_Output multi_texture_mul_ps(Multi_Texture_PS_Data data) : SV_TARGET
{
	//texture0 == defuse
	//texture1 == light map
	//float4 out_color = data.color * texture0.Sample(sampler0, data.uv0) *  texture1.Sample(sampler1, data.uv1);
	float4 out_color = data.color * texture0.Sample(sampler0, data.uv0);

	if (view_xform[0][0])
	{
		out_color = data.color * texture1.Sample(sampler0, data.uv1);
	}
	
	float4 normal = (data.normal + float4(1, 1, 1, 0)) * float4(0.5f, 0.5f, 0.5f, 1);//show normals	
	normal.a = flags;//pack materialId in noraml.w

#if defined(ALPHA_TEST_GT0)
	if (out_color.a == 0.0f) discard;
#elif defined(ALPHA_TEST_LT80)
	if (out_color.a >= 0.5f) discard;
#elif defined(ALPHA_TEST_GE80)
	if (out_color.a < 0.5f) discard;
#endif

	Pixel_Output output;
	output.Albedo = out_color;
	output.Normal = normal;
	output.Velocity = CompteVelocity(data.positionThis, data.positionLast);

	return output;
}

Pixel_Output multi_texture_add_ps(Multi_Texture_PS_Data data) : SV_TARGET{
	float4 color_a = data.color * texture0.Sample(sampler0, data.uv0);
	float4 color_b = texture1.Sample(sampler1, data.uv1);

	float4 out_color = float4(
		color_a.r + color_b.r,
		color_a.g + color_b.g,
		color_a.b + color_b.b,
		color_a.a * color_b.a);

#if defined(ALPHA_TEST_GT0)
	if (out_color.a == 0.0f) discard;
#elif defined(ALPHA_TEST_LT80)
	if (out_color.a >= 0.5f) discard;
#elif defined(ALPHA_TEST_GE80)
	if (out_color.a < 0.5f) discard;
#endif

	float4 normal = (data.normal + float4(1, 1, 1, 0)) * float4(0.5f, 0.5f, 0.5f, 1);//show normals	
	
	normal.a = flags;//pack materialId in noraml.w

	Pixel_Output output;
	output.Albedo = out_color * float4(1.0f, 0.0, 0.0, 1.0f);
	output.Normal = normal;
	output.Velocity = CompteVelocity(data.positionThis, data.positionLast);

	return output;
}
