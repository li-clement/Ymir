#include "vdp2_common_params.hlsli"
#include "vdp2_compose_params.hlsli"

cbuffer CommonRenderParams : register(b0) {
    CommonRenderParams g_renderParams;
}

StructuredBuffer<ComposeParams> composeParams : register(t1);
Texture2DArray<uint4> bgIn : register(t2);

RWTexture2D<float4> textureOut : register(u0);

// -----------------------------------------------------------------------------

uint3 Compose(uint2 basePos) {
    return bgIn[uint3(basePos.xy, 0)].rgb;
}

[numthreads(32, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint2 drawCoord = uint2(id.x, id.y + g_renderParams.startY /*ScaleUp(g_renderParams.startY)*/);
    const uint2 outCoord = uint2(drawCoord.x, drawCoord.y /*GetOutputY(drawCoord.y)*/);
    const uint3 outColor = Compose(drawCoord);
    textureOut[outCoord] = float4(outColor / 255.0, 1.0f);
}
