
struct GeomVertOut
{
    float4 Pos : SV_POSITION;
    nointerpolation float4 Center : POSITION0;
    float4 ModelPos : POSITION1;
    float4 Col : COLOR;
};
float4 main(GeomVertOut vOut) : SV_Target
{
    // We deal with quad, so everything outside 0.5 doesnt belong to
    // the square. 
    float len = min(length(vOut.Center - vOut.ModelPos), 0.5) * 2.0;
    float fade = lerp(1.0, 0.0, len);
    
    float4 colorOut = vOut.Col * fade;
    
    
    return colorOut;
}