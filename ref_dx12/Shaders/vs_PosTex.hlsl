
cbuffer cbPerObject : register(b0)
{
	float4x4 gWorldViewProj;
};


struct VertexIn
{
	float4 Pos : POSITION;
	float2 Tex : TEXCOORD;
};


struct VertexOut
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};


VertexOut main(VertexIn vIn)
{
	VertexOut vOut;
    // Funny enought, by default matrices are packed as column major.
    vOut.Pos = mul(gWorldViewProj, vIn.Pos);
    
	vOut.Tex = vIn.Tex;

	return vOut;
}