#include "NRICompatibility.hlsli"

struct InputPS
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
};

NRI_RESOURCE( Texture2D, g_DiffuseTexture, t, 0, 1 );
NRI_RESOURCE( SamplerState, g_Sampler, s, 0, 1 );
NRI_RESOURCE( Texture2D, g_cubeTexture, t, 1, 1 );


float4 main(InputPS input) : SV_Target
{
    float2 newUV = input.uv;
    newUV.y = 1.0 - newUV.y;
    float4 color = g_DiffuseTexture.Sample( g_Sampler, newUV);
    color += g_cubeTexture.Sample( g_Sampler, float2(0.0, 0.0)) * 0.001;  
    return color;
}
