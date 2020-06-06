
//#DEBUG alignment should be done when I will allocate memory for this constant in C++(256)
cbuffer cbPerObject : register(b0)
{
	float4x4 gWorldViewProj;
    float4 gAnimMove;
    float4 gFrontLerp;
    float4 gBackLerp;
};


struct VertexIn
{
	float4 vPos0 : POSITION0;
    float4 vPos1 : POSITION1;
	float2 vTex : TEXCOORD;
};


struct VertexOut
{
	float4 pPos : SV_POSITION;
	float2 pTex : TEXCOORD;
};


VertexOut main(VertexIn vIn)
{
    float4 interpolatedPos = gAnimMove + vIn.vPos0 * gBackLerp + vIn.vPos1 * gFrontLerp;
    //#DEBUG do I need this?
    interpolatedPos.w = 1.0f;

	VertexOut vOut;
    // Funny enought, by default matrices are packed as column major.
    vOut.pPos = mul(gWorldViewProj, interpolatedPos);
    
	vOut.pTex = vIn.vTex;

	return vOut;
}