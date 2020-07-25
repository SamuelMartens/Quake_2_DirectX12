Texture2D gDiffuseMap : register(t0);

SamplerState gSamLinearWrap : register(s0);


struct VertexOut
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

float4 main(VertexOut vOut) : SV_Target
{
	float4 color = gDiffuseMap.Sample(gSamLinearWrap, vOut.Tex);
    
    if (color.a == 0)
    {
        discard;
    }
    
    return color;
}