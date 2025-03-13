#include "NRICompatibility.hlsli"

struct PSInput 
{
    float4 position : SV_Position; // Equivalent to gl_Position
    float3 dir      : TEXCOORD0;        // Output direction
};

struct PushConstants
{
    float4 params;
};
NRI_ROOT_CONSTANTS( PushConstants, g_PushConstants, 1, 0 );

NRI_RESOURCE( cbuffer, CommonConstants, b, 0, 0 )
{
    float4x4 modelMat;
	float4x4 viewMat;
	float4x4 projectMat;
};


NRI_RESOURCE( Texture2D, g_DiffuseTexture, t, 0, 1 );
NRI_RESOURCE( SamplerState, g_Sampler, s, 0, 1 );

#define M_PI 3.1415926535897932384626433832795

float4 main(PSInput input) : SV_Target
{
    float3 cube_normal = 0.0;
    float2 uv_interp = input.dir.xy;
    cube_normal.z = -1.0;
    cube_normal.x = (cube_normal.z * (-uv_interp.x - g_PushConstants.params.x)) / g_PushConstants.params.y;
	cube_normal.y = -(cube_normal.z * (uv_interp.y - g_PushConstants.params.z)) / g_PushConstants.params.w;
    // cube_normal = mul(viewMat, float4(cube_normal, 1.0)).xyz;
    
	cube_normal = normalize(cube_normal);
    float2 panorama_coords = float2(atan2(cube_normal.x, -cube_normal.z), acos(cube_normal.y));

	// if (panorama_coords.x < 0.0) {
	// 	panorama_coords.x += M_PI * 2.0;  
	// }

	panorama_coords /= float2(M_PI * 2.0, M_PI);
    float4 color = g_DiffuseTexture.Sample(g_Sampler, panorama_coords);
    // float4 color = float4(input.dir.xy, 0.0, 1.0);
    return color;
}