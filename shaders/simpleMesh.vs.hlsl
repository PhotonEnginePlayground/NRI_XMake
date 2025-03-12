// © 2021 NVIDIA Corporation

#include "NRICompatibility.hlsli"

NRI_RESOURCE( cbuffer, CommonConstants, b, 0, 0 )
{
    float4x4 modelMat;
	float4x4 viewMat;
	float4x4 projectMat;
};

struct inputVS
{
    float3 in_position : POSITION0;
    float2 in_texcoord : TEXCOORD0;
    float3 in_normal : NORMAL;
};

struct outputVS
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float3 normal : NORMAL;
};

outputVS main(inputVS input)
{
    outputVS output;
    float4x4 vpMat = mul(viewMat, modelMat);
	float4x4 mvpMat = mul(projectMat, vpMat);
	output.position = mul(mvpMat, float4(input.in_position.xyz, 1.0));
    output.texCoord = input.in_texcoord;
    // output.normal = input.in_normal;
    return output;
}
