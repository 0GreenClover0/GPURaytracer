#pragma once

#include "ConstantBuffers.h"

namespace GlobalRootSignature::Slot
{

enum Enum
{
    OutputView = 0,
    AccelerationStructure,
    SceneConstant,
    AABBAttributeBuffer,
    VertexBuffers,
    Count
};

}

namespace LocalRootSignature::Type
{

enum Enum
{
    Triangle = 0,
    AABB,
    Count
};

}

namespace LocalRootSignature::Triangle
{

namespace Slot
{
enum Enum
{
    MaterialConstant = 0,
    Count
};

}

struct RootArguments
{
    PrimitiveConstantBuffer material_cb;
};

}

namespace LocalRootSignature::AABB
{

namespace Slot
{

enum Enum
{
    MaterialConstant = 0,
    GeometryIndex,
    Count
};

}

struct RootArguments
{
    PrimitiveConstantBuffer material_cb;
    PrimitiveInstanceConstantBuffer aabb_cb;
};

}

namespace LocalRootSignature
{

inline UINT max_root_arguments_size()
{
    return std::max(sizeof(Triangle::RootArguments), sizeof(AABB::RootArguments));
}

}

namespace GeometryType
{

enum Enum
{
    Triangle = 0,
    AABB, // Procedural geometry with an application provided AABB.
    Count
};

}

namespace GpuTimers
{

enum Enum
{
    Raytracing = 0,
    Count
};

}

// Bottom-level acceleration structures (BottomLevelASType).
// We use two BottomLevelASType, one for AABB and one for Triangle geometry.
// Mixing of geometry types within a BLAS is not supported.
namespace BottomLevelASType = GeometryType;

namespace IntersectionShaderType
{

enum Enum
{
    AnalyticPrimitive = 0,
    VolumetricPrimitive,
    SignedDistancePrimitive,
    Count
};

inline UINT per_primitive_type_count(Enum const type)
{
    switch (type)
    {
    case AnalyticPrimitive:
        return AnalyticPrimitive::Count;
    case VolumetricPrimitive:
        return VolumetricPrimitive::Count;
    case SignedDistancePrimitive:
        return SignedDistancePrimitive::Count;
    default:
        break;
    }
    return 0;
}

//static const UINT MAX_PER_PRIMITIVE_TYPE_COUNT =
//    std::max(AnalyticPrimitive::Count, std::max(VolumetricPrimitive::Count, SignedDistancePrimitive::Count));
static constexpr UINT TOTAL_PRIMITIVE_COUNT = AnalyticPrimitive::Count + VolumetricPrimitive::Count + SignedDistancePrimitive::Count;
}
