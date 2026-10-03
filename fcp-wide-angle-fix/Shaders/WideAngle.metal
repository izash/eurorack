#include <metal_stdlib>
#include "../Sources/ShaderTypes.h"
using namespace metal;

struct RasterizerData {
    float4 position [[position]];
    float2 textureCoordinate;
};

vertex RasterizerData wideAngleVertex(uint vid [[vertex_id]],
                                      constant Vertex2D *vertices [[buffer(kVertexInputIndex_Vertices)]],
                                      constant uint2 *viewportSize [[buffer(kVertexInputIndex_ViewportSize)]])
{
    RasterizerData out;
    float2 pos = vertices[vid].position.xy;
    float2 vp  = float2(*viewportSize) / 2.0;
    out.position = float4(pos / vp, 0.0, 1.0);
    out.textureCoordinate = vertices[vid].textureCoordinate;
    return out;
}

// Integral of a smoothstep weight (0 before e0, ramps over r, then 1).
static float G(float d, float e0, float r)
{
    r = max(r, 1e-6);
    float t = clamp((d - e0) / r, 0.0, 1.0);
    float ramp = r * (t*t*t - 0.5*t*t*t*t);
    return (d <= e0 + r) ? ramp : (r * 0.5 + (d - e0 - r));
}

static float sideMap(float d, float zone, float feather, float amount, float halfW, bool fill)
{
    float e0 = max(0.0, 1.0 - zone / halfW);
    float r  = feather / halfW;
    float D  = d + amount * G(d, e0, r);
    if (fill) D /= (1.0 + amount * G(1.0, e0, r));
    return D;
}

fragment float4 wideAngleFragment(RasterizerData in [[stage_in]],
                                  texture2d<float> src [[texture(kTextureIndex_Source)]],
                                  constant WideAngleParams &p [[buffer(kFragmentIndex_Params)]])
{
    constexpr sampler s(coord::normalized, address::clamp_to_edge, filter::linear);

    // Position of this output pixel in the full image, 0..1.
    float2 px = in.position.xy;                       // tile pixel coords (+0.5 centred)
    float x = (p.tileOrigin.x + px.x) / p.imageSize.x;
    float v = (p.tileOrigin.y + px.y) / p.imageSize.y;

    float c = clamp(p.center, 0.05, 0.95);
    bool  left = x < c;
    int   i = left ? 0 : 1;
    float halfW = left ? c : 1.0 - c;
    float d = left ? (c - x) / halfW : (x - c) / halfW;

    float D = sideMap(d, p.zone[i], p.feather[i], p.amount[i], halfW, p.fill != 0);
    D = abs(D);                                       // mirror if unfilled mode reads past centre
    float sx = left ? c - halfW * D : c + halfW * D;
    if (sx < 0.0) sx = -sx;                           // mirror-fold if it reads past the frame edge
    if (sx > 1.0) sx = 2.0 - sx;

    float4 warped   = src.sample(s, float2(sx, v));
    float4 original = src.sample(s, float2(x, v));
    float4 color = mix(original, warped, p.mix);

    if (p.showZones != 0) {
        float e0 = max(0.0, 1.0 - p.zone[i] / halfW);
        float w = smoothstep(e0, e0 + max(p.feather[i] / halfW, 1e-4), d);
        float3 tint = left ? float3(1.0, 0.2, 0.2) : float3(0.2, 0.5, 1.0);
        color.rgb = mix(color.rgb, tint, 0.35 * w * color.a);
        if (fabs(x - c) < 0.0008) color.rgb = float3(1.0, 1.0, 0.0);
    }
    return color;
}
