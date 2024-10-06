#ifndef CONSTANT_BUFFERS_H
#define CONSTANT_BUFFERS_H

#ifdef HLSL
#include "HlslCompat.h"
#else
#include <DirectXMath.h>
using namespace DirectX;
#endif

struct SceneConstantBuffer
{
    XMMATRIX projection_to_world;
    XMVECTOR camera_position;
    XMVECTOR light_position;
    XMVECTOR light_ambient_color;
    XMVECTOR light_diffuse_color;
};

struct CubeConstantBuffer
{
    XMFLOAT4 albedo;
};

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT3 normal;
};

#endif
