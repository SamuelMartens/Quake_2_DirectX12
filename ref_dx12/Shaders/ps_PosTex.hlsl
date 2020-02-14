SamplerState gSamLinearWrap	: register(s0);

Texture2D gDiffuseMap : register(t0);

struct VertexOut
{
	float4 pPos : SV_POSITION;
	float2 pTex : TEXCOORD;
};

float4 main(VertexOut vOut) : SV_Target
{
	float4 color = gDiffuseMap.Sample(gSamLinearWrap, vOut.pTex);
	return color;
}