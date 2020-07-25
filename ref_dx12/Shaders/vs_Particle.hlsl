
struct VertexIn
{
    float4 Pos : POSITION;
    float4 Col : COLOR;
};


struct VertexOut
{
    float4 Pos : POSITION;
    float4 Col : COLOR;
};


VertexOut main(VertexIn vIn)
{
    VertexOut vOut;
    
    vOut.Pos = vIn.Pos;
    vOut.Col = vIn.Col;

    return vOut;
}