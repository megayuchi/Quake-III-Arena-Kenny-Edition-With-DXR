
/*
float4 RGBMEncode(float3 color) {
	float4 rgbm;
	color *= 1.0 / 6.0;
	rgbm.a = saturate(max(max(color.r, color.g), max(color.b, 1e-6)));
	rgbm.a = ceil(rgbm.a * 255.0) / 255.0;
	rgbm.rgb = color / rgbm.a;
	return rgbm;
}

float3 RGBMDecode(float4 rgbm) {
	return 6.0 * rgbm.rgb * rgbm.a;
}
*/

// M matrix, for encoding
const static float3x3 M = float3x3(
	0.2209, 0.3390, 0.4184,
	0.1138, 0.6780, 0.7319,
	0.0102, 0.1130, 0.2969);

// Inverse M matrix, for decoding
const static float3x3 InverseM = float3x3(
	6.0014, -2.7008, -1.7996,
	-1.3320, 3.1029, -5.7721,
	0.3008, -1.0882, 5.6268);

float4 RGBMEncode(in float3 vRGB) {
	float4 vResult;
	float3 Xp_Y_XYZp = mul(vRGB, M);
	Xp_Y_XYZp = max(Xp_Y_XYZp, float3(1e-6, 1e-6, 1e-6));
	vResult.xy = Xp_Y_XYZp.xy / Xp_Y_XYZp.z;
	float Le = 2 * log2(Xp_Y_XYZp.y) + 127;
	vResult.w = frac(Le);
	vResult.z = (Le - (floor(vResult.w*255.0f)) / 255.0f) / 255.0f;
	return vResult;
}

float3 RGBMDecode(in float4 vLogLuv) {
	float Le = vLogLuv.z * 255 + vLogLuv.w;
	float3 Xp_Y_XYZp;
	Xp_Y_XYZp.y = exp2((Le - 127) / 2);
	Xp_Y_XYZp.z = Xp_Y_XYZp.y / vLogLuv.y;
	Xp_Y_XYZp.x = vLogLuv.x * Xp_Y_XYZp.z;
	float3 vRGB = mul(Xp_Y_XYZp, InverseM);
	return max(vRGB, 0);
}