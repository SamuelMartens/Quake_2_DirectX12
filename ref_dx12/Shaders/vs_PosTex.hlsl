cbuffer cbPerObject : register(b0)
{
	float4x4 gWorldViewProj;
};


struct VertexIn
{
	float4 vPos : POSITION;
	float2 vTex : TEXCOORD;
};


struct VertexOut
{
	float4 pPos : SV_POSITION;
	float2 pTex : TEXCOORD;
};


VertexOut main(VertexIn vIn)
{
	VertexOut vOut;
    vOut.pPos = mul(gWorldViewProj, vIn.vPos);
    
	vOut.pTex = vIn.vTex;

	return vOut;
}