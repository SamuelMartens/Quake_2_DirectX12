Texture2D gDiffuseMap : register(t0);

SamplerState gSamLinearWrap : register(s0);


struct VertexOut
{
	float4 pPos : SV_POSITION;
	float2 pTex : TEXCOORD;
};

float4 main(VertexOut vOut) : SV_Target
{
	float4 color = gDiffuseMap.Sample(gSamLinearWrap, vOut.pTex);
    
    if (color.a == 0)
    {
        discard;
    }
    //#DEBUG don't need this shader probably
    //
    return color;
}