cbuffer cbPerObject : register(b0)
{
	float4x4 gWorldViewProj;
    float4 gAnimMove;
    float4 gFrontLerp;
    float4 gBackLerp;
};


struct VertexIn
{
	float4 Pos0 : POSITION0;
    float4 Pos1 : POSITION1;
	float2 Tex : TEXCOORD;
};


struct VertexOut
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};


VertexOut main(VertexIn vIn)
{
    float4 interpolatedPos = gAnimMove + vIn.Pos0 * gBackLerp + vIn.Pos1 * gFrontLerp;
    interpolatedPos.w = 1.0f;

	VertexOut vOut;
    // Funny enought, by default matrices are packed as column major.
    vOut.Pos = mul(gWorldViewProj, interpolatedPos);
    
	vOut.Tex = vIn.Tex;

	return vOut;
}