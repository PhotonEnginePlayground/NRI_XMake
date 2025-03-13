#include "NRICompatibility.hlsli"

NRI_RESOURCE( cbuffer, CommonConstants, b, 0, 0 )
{
    float4x4 modelMat;
	float4x4 viewMat;
	float4x4 projectMat;
};

struct VSOutput {
    float4 position : SV_Position; // Equivalent to gl_Position
    float3 dir : TEXCOORD0;        // Output direction
}; 

static const float2 posArray[] = {float2(-3.0, 1.0), float2(1.0, 1.0), float2(1.0, -3.0)};

VSOutput main(uint vertexId : SV_VertexID) {
    VSOutput output;
	output.position = float4(posArray[vertexId], 0.0, 1.0);
    output.dir = float3(posArray[vertexId], 0.0);
    return output;
} 