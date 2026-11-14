cbuffer PixelColor : register(b0) { float4 Color; };
float4 VS(uint id : SV_VertexID) : SV_POSITION
{
    float2 p[3] = { float2(-1,-1), float2(-1,3), float2(3,-1) };
    return float4(p[id],0,1);
}
float4 PS() : SV_TARGET { return Color; }
