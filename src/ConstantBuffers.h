#ifndef CONSTANT_BUFFERS_H
#define CONSTANT_BUFFERS_H

#ifdef HLSL
#include "HlslCompat.h"
#else
#include <DirectXMath.h>
using namespace DirectX;
#endif

// Number of metaballs to use within an AABB.
#define METABALLS_COUNT 3

// Limitting calculations only to metaballs a ray intersects can speed up raytracing
// dramatically particularly when there is a higher number of metaballs used.
// Use of dynamic loops can have detrimental effects to performance for low iteration counts
// and outweighing any potential gains from avoiding redundant calculations.
// Requires: USE_DYNAMIC_LOOPS set to 1 to take effect.
#if METABALLS_COUNT >= 5
#define USE_DYNAMIC_LOOPS 1
#define LIMIT_TO_ACTIVE_METABALLS 1
#else
#define USE_DYNAMIC_LOOPS 0
#define LIMIT_TO_ACTIVE_METABALLS 0
#endif

#define FRACTAL_ITERATIONS_COUNT 4

// NOTE: Set max recursion depth as low as needed
// as drivers may apply optimization strategies for low recursion depths.
#define MAX_RAY_RECURSION_DEPTH 3 // ~ primary rays + reflections + shadow rays from reflected geometry.

struct ProceduralPrimitiveAttributes
{
    XMFLOAT3 normal;
};

struct RayPayload
{
    XMFLOAT4 color;
    UINT recursion_depth;
};

struct ShadowRayPayload
{
    bool hit;
};

struct SceneConstantBuffer
{
    XMMATRIX projection_to_world;
    XMVECTOR camera_position;
    XMVECTOR light_position;
    XMVECTOR light_ambient_color;
    XMVECTOR light_diffuse_color;
    float reflectance;
    float elapsed_time;
};

// Attributes per primitive type.
struct PrimitiveConstantBuffer
{
    XMFLOAT4 albedo;
    float reflectance_coefficient;
    float diffuse_coefficient;
    float specular_coefficient;
    float specular_power;
    float step_scale; // Step scale for ray marching of signed distance primitives.
        // - Some object transformations don't preserve the distances and thus require shorter steps.
    XMFLOAT3 padding;
};

// Attributes per primitive instance.
struct PrimitiveInstanceConstantBuffer
{
    UINT instance_index;
    UINT primitive_type; // Procedural primitive type
};

// Dynamic attributes per primitive instance.
struct PrimitiveInstancePerFrameBuffer
{
    XMMATRIX local_space_to_bottom_level_as; // Matrix from local primitive space to bottom-level object space.
    XMMATRIX bottom_level_as_to_local_space; // Matrix from bottom-level object space to local primitive space.
};

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT3 normal;
};

// Ray types traced in this sample.
namespace RayType
{

enum Enum
{
    Radiance = 0, // ~ Primary, reflected camera/view rays calculating color for each hit.
    Shadow, // ~ Shadow/visibility rays, only testing for occlusion
    Count
};

}

namespace TraceRayParameters
{
static const UINT INSTANCE_MASK = ~0; // Everything is visible.

namespace HitGroup
{

static const UINT OFFSET[RayType::Count] = {
    0, // Radiance ray
    1 // Shadow ray
};

static const UINT GEOMETRY_STRIDE = RayType::Count;

}

namespace MissShader
{

static const UINT OFFSET[RayType::Count] = {
    0, // Radiance ray
    1 // Shadow ray
};

}

}

// From: http://blog.selfshadow.com/publications/s2015-shading-course/hoffman/s2015_pbs_physics_math_slides.pdf
static const XMFLOAT4 CHROMIUM_REFLECTANCE = XMFLOAT4(0.549f, 0.556f, 0.554f, 1.0f);

static const XMFLOAT4 BACKGROUND_COLOR = XMFLOAT4(0.8f, 0.9f, 1.0f, 1.0f);
static float const IN_SHADOW_RADIANCE = 0.35f;

namespace AnalyticPrimitive
{

enum Enum
{
    AABB = 0,
    Spheres,
    Count
};

}

namespace VolumetricPrimitive
{

enum Enum
{
    Metaballs = 0,
    Count
};

}

namespace SignedDistancePrimitive
{

enum Enum
{
    MiniSpheres = 0,
    IntersectedRoundCube,
    SquareTorus,
    TwistedTorus,
    Cog,
    Cylinder,
    FractalPyramid,
    Count
};

}

#endif
