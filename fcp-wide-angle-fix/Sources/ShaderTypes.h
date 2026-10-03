#ifndef ShaderTypes_h
#define ShaderTypes_h
#include <simd/simd.h>

typedef struct {
    vector_float4 position;
    vector_float2 textureCoordinate;
} Vertex2D;

enum { kVertexInputIndex_Vertices = 0, kVertexInputIndex_ViewportSize = 1 };
enum { kTextureIndex_Source = 0 };
enum { kFragmentIndex_Params = 0 };

typedef struct {
    vector_float2 imageSize;      // full source image, pixels
    vector_float2 tileOrigin;     // destination tile origin inside the image, pixels
    float center;                 // 0..1 centre line (x)
    float zone[2];                // [left,right] zone width, fraction of frame width
    float feather[2];             // [left,right] ramp length, fraction of frame width
    float amount[2];              // [left,right] squeeze, + = slimmer sides
    int   fill;                   // 1 = keep every source pixel in frame (slight centre zoom)
    int   showZones;              // 1 = tint zones
    float mix;                    // 0..1
} WideAngleParams;

#endif
