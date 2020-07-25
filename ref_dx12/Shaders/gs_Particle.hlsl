cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldViewProj;
    float4 gCameraYaw;
    float4 gCameraPitch;
    float4 gCameraRoll;
    float4 gCameraOrigin;
};

struct VertexOut
{
    float4 Pos : POSITION;
    float4 Col : COLOR;
};

struct GeomVertOut
{
    float4 Pos : SV_POSITION;
    nointerpolation float4 Center : POSITION0;
    float4 ModelPos : POSITION1;
    float4 Col : COLOR;
};

// v1 +--------+ v2
//    |        |
//    |        |
//    |        |
// v0 +--------+ v3

[maxvertexcount(4)]
void main(point VertexOut vIn[1], inout TriangleStream<GeomVertOut> triStream)
{
    // hack a scale up to keep particles from disapearing
    float scale = dot(vIn[0].Pos - gCameraOrigin,  gCameraRoll);
    scale = scale < 20.0f ? 1.0f : 1.0f + scale * 0.004f;
    scale *= 1.5f;
 
    
    float4 center = (gCameraYaw + gCameraPitch) * 0.5;
    
    // Create quad
    // Remember we create triangle strip, that's why order is a bit weird 
    
    GeomVertOut v0;
    v0.ModelPos = 0.0;
    v0.Pos = mul(gWorldViewProj, vIn[0].Pos + v0.ModelPos);
    v0.Col = vIn[0].Col;
    v0.Center = center;
    triStream.Append(v0);
   
    GeomVertOut v1;
    v1.ModelPos = gCameraYaw;
    v1.Pos = mul(gWorldViewProj, vIn[0].Pos + mul(v1.ModelPos, scale));
    v1.Col = vIn[0].Col;
    v1.Center = center;
    triStream.Append(v1); 
    
    GeomVertOut v2;
    v2.ModelPos = gCameraPitch;
    v2.Pos = mul(gWorldViewProj, vIn[0].Pos + mul(v2.ModelPos, scale));
    v2.Col = vIn[0].Col;
    v2.Center = center;
    triStream.Append(v2);
   
    GeomVertOut v3;
    v3.ModelPos = gCameraYaw + gCameraPitch;
    v3.Pos = mul(gWorldViewProj, vIn[0].Pos + mul(v3.ModelPos, scale));
    v3.Col = vIn[0].Col;
    v3.Center = center;
    triStream.Append(v3);
}