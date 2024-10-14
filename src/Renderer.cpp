#include "stdafx.h"

#include "Renderer.h"

#include "DeviceResources.h"
#include "Window.h"

#include "../res/compiled/Raytracing.hlsl.h"

#include <d3dcommon.h>

#include <iostream>

#define SIZE_OF_IN_UINT32(obj) ((sizeof(obj) - 1) / sizeof(UINT32) + 1)

#include "RendererRaytracingHelper.h"

#include <DirectXMath.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// Shader entry points.
wchar_t const* Renderer::raygen_shader_name = L"MyRaygenShader";
wchar_t const* Renderer::intersection_shader_names[] = {
    L"MyIntersectionShader_AnalyticPrimitive",
    L"MyIntersectionShader_VolumetricPrimitive",
    L"MyIntersectionShader_SignedDistancePrimitive",
};
wchar_t const* Renderer::closest_hit_shader_names[] = {
    L"MyClosestHitShader_Triangle",
    L"MyClosestHitShader_AABB",
};
wchar_t const* Renderer::miss_shader_names[] = {L"MyMissShader", L"MyMissShader_ShadowRay"};

// Hit groups.
wchar_t const* Renderer::hit_group_names_triangle_geometry[] = {L"MyHitGroup_Triangle", L"MyHitGroup_Triangle_ShadowRay"};
wchar_t const* Renderer::hit_group_names_aabb_geometry[][RayType::Count] = {
    {L"MyHitGroup_AABB_AnalyticPrimitive", L"MyHitGroup_AABB_AnalyticPrimitive_ShadowRay"},
    {L"MyHitGroup_AABB_VolumetricPrimitive", L"MyHitGroup_AABB_VolumetricPrimitive_ShadowRay"},
    {L"MyHitGroup_AABB_SignedDistancePrimitive", L"MyHitGroup_AABB_SignedDistancePrimitive_ShadowRay"},
};

Renderer::Renderer(u32 const width, u32 const height, std::wstring const& name)
{
    m_animate_geometry = true;
    m_window = std::make_unique<Window>(this, width, height, name);
    Window::set_instance(m_window.get());
}

Renderer::~Renderer()
{
    DestroyWindow(m_window->get_hwnd());
}

void Renderer::on_init()
{
    m_window->on_size_changed.attach(&Renderer::on_size_changed, shared_from_this());

    m_device_resources =
        std::make_unique<DeviceResources>(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN, frame_count, D3D_FEATURE_LEVEL_11_0,
                                          DeviceResources::require_tearing_support, m_adapter_id_override);

    m_device_resources->register_device_notify(this);
    m_device_resources->set_window(m_window->get_hwnd(), static_cast<i32>(m_window->get_width()), static_cast<i32>(m_window->get_height()));
    m_device_resources->initialize_dxgi_adapter();

    assert(IsDirectXRaytracingSupported(m_device_resources->get_adapter()));

    m_device_resources->create_device_resources();
    m_device_resources->create_window_size_dependent_resources();

    initialize_scene();

    create_device_dependent_resources();
    create_window_size_dependent_resources();

    ShowWindow(m_window->get_hwnd(), SW_SHOWDEFAULT);
    UpdateWindow(m_window->get_hwnd());
}

void Renderer::on_update()
{
    m_timer.tick();
    calculate_frame_stats();

    float const elapsed_time = static_cast<float>(m_timer.get_elapsed_seconds());
    u32 const frame_index = m_device_resources->get_current_frame_index();
    u32 const previous_frame_index = m_device_resources->get_previous_frame_index();

    // Rotate the camera around Y axis.
    if (m_animate_camera)
    {
        float constexpr seconds_to_rotate_around = 48.0f;
        float const angle_to_rotate_by = 360.0f * (elapsed_time / seconds_to_rotate_around);
        XMMATRIX const rotate = XMMatrixRotationY(XMConvertToRadians(angle_to_rotate_by));
        m_eye = XMVector3Transform(m_eye, rotate);
        m_up = XMVector3Transform(m_up, rotate);
        m_at = XMVector3Transform(m_at, rotate);
        update_camera_matrices();
    }

    // Rotate the second light around Y axis.
    if (m_animate_light)
    {
        float constexpr seconds_to_rotate_around = 8.0f;
        float const angle_to_rotate_by = -360.0f * (elapsed_time / seconds_to_rotate_around);
        XMMATRIX const rotate = XMMatrixRotationY(XMConvertToRadians(angle_to_rotate_by));
        m_scene_cb->light_position = XMVector3Transform(m_scene_cb->light_position, rotate);
    }

    // Transform the procedular geometry.
    if (m_animate_geometry)
    {
        m_animate_geometry_time += elapsed_time;
    }

    update_aabb_primitive_attributes(m_animate_geometry_time);
    m_scene_cb->elapsed_time = m_animate_geometry_time;
}

void Renderer::on_render()
{
    if (!m_device_resources->is_window_visible())
    {
        return;
    }

    m_device_resources->prepare();

    auto const command_list = m_device_resources->get_command_list();

    for (auto& gpu_timer : m_gpu_timers)
    {
        gpu_timer.BeginFrame(command_list);
    }

    do_raytracing();

    copy_raytracing_output_to_backbuffer();

    for (auto& gpu_timer : m_gpu_timers)
    {
        gpu_timer.EndFrame(command_list);
    }

    m_device_resources->present(D3D12_RESOURCE_STATE_PRESENT);
}

void Renderer::on_size_changed(u32 const width, u32 const height, bool const minimized)
{
    if (!m_device_resources->window_size_changed(static_cast<i32>(width), static_cast<i32>(height), minimized))
    {
        return;
    }

    m_window->set_window_size(width, height);

    release_window_size_dependent_resources();
    create_window_size_dependent_resources();
}

void Renderer::on_destroy()
{
    m_device_resources->wait_for_gpu();
    on_device_lost();
}

Window* Renderer::get_window() const
{
    return m_window.get();
}

DeviceResources* Renderer::get_device_resources() const
{
    return m_device_resources.get();
}

u32 Renderer::get_frames_in_flight()
{
    return frame_count;
}

void Renderer::on_device_lost()
{
    // Release all device dependent resouces when a device is lost.
    release_window_size_dependent_resources();
    release_device_dependent_resources();
}

void Renderer::on_device_restored()
{
    create_device_dependent_resources();
    create_window_size_dependent_resources();
}

void Renderer::initialize_scene()
{
    u32 const frame_index = m_device_resources->get_current_frame_index();

    // Setup materials.
    {
        auto set_attributes = [&](u32 primitive_index, XMFLOAT4 const& albedo, float reflectance_coefficient = 0.0f,
                                  float diffuse_coefficient = 0.9f, float specular_coefficient = 0.7f, float specular_power = 50.0f,
                                  float step_scale = 1.0f) {
            auto& attributes = m_aabb_material_cb[primitive_index];
            attributes.albedo = albedo;
            attributes.reflectance_coefficient = reflectance_coefficient;
            attributes.diffuse_coefficient = diffuse_coefficient;
            attributes.specular_coefficient = specular_coefficient;
            attributes.specular_power = specular_power;
            attributes.step_scale = step_scale;
        };

        m_plane_material_cb = {XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f), 0.25f, 1, 0.4f, 50, 1};

        // Albedos
        auto constexpr green = XMFLOAT4(0.1f, 1.0f, 0.5f, 1.0f);
        auto constexpr red = XMFLOAT4(1.0f, 0.5f, 0.5f, 1.0f);
        auto constexpr yellow = XMFLOAT4(1.0f, 1.0f, 0.5f, 1.0f);

        u32 offset = 0;

        // Analytic primitives.
        {
            using namespace AnalyticPrimitive;
            set_attributes(offset + AABB, red);
            set_attributes(offset + Spheres, CHROMIUM_REFLECTANCE, 1);
            offset += AnalyticPrimitive::Count;
        }

        // Volumetric primitives.
        {
            using namespace VolumetricPrimitive;
            set_attributes(offset + Metaballs, CHROMIUM_REFLECTANCE, 1);
            offset += VolumetricPrimitive::Count;
        }

        // Signed distance primitives.
        {
            using namespace SignedDistancePrimitive;
            set_attributes(offset + MiniSpheres, green);
            set_attributes(offset + IntersectedRoundCube, green);
            set_attributes(offset + SquareTorus, CHROMIUM_REFLECTANCE, 1);
            set_attributes(offset + TwistedTorus, yellow, 0, 1.0f, 0.7f, 50, 0.5f);
            set_attributes(offset + Cog, yellow, 0, 1.0f, 0.1f, 2);
            set_attributes(offset + Cylinder, red);
            set_attributes(offset + FractalPyramid, green, 0, 1, 0.1f, 4, 0.8f);
        }
    }

    // Setup camera.
    {
        // Initialize the view and projection inverse matrices.
        m_eye = {0.0f, 5.3f, -17.0f, 1.0f};
        m_at = {0.0f, 0.0f, 0.0f, 1.0f};
        XMVECTOR constexpr right = {1.0f, 0.0f, 0.0f, 0.0f};

        XMVECTOR const direction = XMVector4Normalize(m_at - m_eye);
        m_up = XMVector3Normalize(XMVector3Cross(direction, right));

        // Rotate camera around Y axis.
        XMMATRIX const rotate = XMMatrixRotationY(XMConvertToRadians(45.0f));
        m_eye = XMVector3Transform(m_eye, rotate);
        m_up = XMVector3Transform(m_up, rotate);

        update_camera_matrices();
    }

    // Setup lights.
    {
        // Initialize the lighting parameters.
        XMFLOAT4 light_position;
        XMFLOAT4 light_ambient_color;
        XMFLOAT4 light_diffuse_color;

        light_position = {0.0f, 18.0f, -20.0f, 0.0f};
        m_scene_cb->light_position = XMLoadFloat4(&light_position);

        light_ambient_color = {0.25f, 0.25f, 0.25f, 1.0f};
        m_scene_cb->light_ambient_color = XMLoadFloat4(&light_ambient_color);

        float constexpr d = 0.6f;
        light_diffuse_color = {d, d, d, 1.0f};
        m_scene_cb->light_diffuse_color = XMLoadFloat4(&light_diffuse_color);
    }
}

void Renderer::update_camera_matrices()
{
    auto const frame_index = m_device_resources->get_current_frame_index();

    m_scene_cb->camera_position = m_eye;
    float constexpr fov_angle_y = 45.0f;
    XMMATRIX const view = XMMatrixLookAtLH(m_eye, m_at, m_up);
    XMMATRIX const proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(fov_angle_y), m_window->get_aspect_ratio(), 0.01f, 125.0f);
    XMMATRIX const view_proj = view * proj;

    m_scene_cb->projection_to_world = XMMatrixInverse(nullptr, view_proj);
}

void Renderer::update_aabb_primitive_attributes(float const animation_time)
{
    XMMATRIX const m_identity = XMMatrixIdentity();

    XMMATRIX const m_scale_15_y = XMMatrixScaling(1.0f, 1.5f, 1.0f);
    XMMATRIX const m_scale_15 = XMMatrixScaling(1.5f, 1.5f, 1.5f);

    XMMATRIX const m_scale_3 = XMMatrixScaling(3.0f, 3.0f, 3.0f);

    XMMATRIX const m_rotation = XMMatrixRotationY(-2.0f * animation_time);

    // Apply scale, rotation and translation transforms.
    // The intersection shader tests in this sample work with local space, so here
    // we apply the BLAS object space translation that was passed to geometry descs.
    auto set_transform_for_aabb = [&](u32 const primitive_index, XMMATRIX const& m_scale, XMMATRIX const& m_rotation) {
        XMVECTOR const v_translation = 0.5f
                                     * (XMLoadFloat3(reinterpret_cast<XMFLOAT3*>(&m_aabbs[primitive_index].MinX))
                                        + XMLoadFloat3(reinterpret_cast<XMFLOAT3*>(&m_aabbs[primitive_index].MaxX)));
        XMMATRIX const m_translation = XMMatrixTranslationFromVector(v_translation);

        XMMATRIX const m_transform = m_scale * m_rotation * m_translation;
        m_aabb_primitive_attribute_buffer[primitive_index].local_space_to_bottom_level_as = m_transform;
        m_aabb_primitive_attribute_buffer[primitive_index].bottom_level_as_to_local_space = XMMatrixInverse(nullptr, m_transform);
    };

    u32 offset = 0;

    // Analytic primitives.
    {
        using namespace AnalyticPrimitive;
        set_transform_for_aabb(offset + AABB, m_scale_15_y, m_identity);
        set_transform_for_aabb(offset + Spheres, m_scale_15, m_rotation);
        offset += AnalyticPrimitive::Count;
    }

    // Volumetric primitives.
    {
        using namespace VolumetricPrimitive;
        set_transform_for_aabb(offset + Metaballs, m_scale_15, m_rotation);
        offset += VolumetricPrimitive::Count;
    }

    // Signed distance primitives.
    {
        using namespace SignedDistancePrimitive;

        set_transform_for_aabb(offset + MiniSpheres, m_identity, m_identity);
        set_transform_for_aabb(offset + IntersectedRoundCube, m_identity, m_identity);
        set_transform_for_aabb(offset + SquareTorus, m_scale_15, m_identity);
        set_transform_for_aabb(offset + TwistedTorus, m_identity, m_rotation);
        set_transform_for_aabb(offset + Cog, m_identity, m_rotation);
        set_transform_for_aabb(offset + Cylinder, m_scale_15_y, m_identity);
        set_transform_for_aabb(offset + FractalPyramid, m_scale_3, m_identity);
    }
}

void Renderer::create_constant_buffers()
{
    auto const device = m_device_resources->get_d3d_device();
    u32 const frame_count = m_device_resources->get_back_buffer_count();

    m_scene_cb.Create(device, frame_count, L"Scene Constant Buffer");
}

void Renderer::create_aabb_primitive_attributes_buffers()
{
    auto const device = m_device_resources->get_d3d_device();
    u32 const frame_count = m_device_resources->get_back_buffer_count();
    m_aabb_primitive_attribute_buffer.Create(device, IntersectionShaderType::TOTAL_PRIMITIVE_COUNT, frame_count,
                                             L"AABB primitive attributes");
}

// Compute the average frames per second and million rays per second.
void Renderer::calculate_frame_stats() const
{
    static int frame_count = 0;
    static double elapsed_time = 0.0f;
    double const total_time = m_timer.get_total_seconds();
    frame_count++;

    // Compute averages over one second period.
    if (total_time - elapsed_time >= 1.0f)
    {
        float const diff = static_cast<float>(total_time - elapsed_time);
        float const fps = static_cast<float>(frame_count) / diff; // Normalize to an exact second.

        frame_count = 0;
        elapsed_time = total_time;

        float const m_rays_per_second = (m_window->get_width() * m_window->get_height() * fps) / static_cast<float>(1e6);

        std::wstringstream window_text;

        window_text << std::setprecision(2) << std::fixed << L"    fps: " << fps << L"     ~Million Primary Rays/s: " << m_rays_per_second
                    << L"    GPU[" << m_device_resources->get_adapter_id() << L"]: " << m_device_resources->get_adapter_description();
        m_window->set_custom_window_text(window_text.str().c_str());
    }
}

void Renderer::do_raytracing()
{
    auto const command_list = m_device_resources->get_command_list();

    auto dispatch_rays = [&](auto* dxr_command_list, auto* state_object, auto* dispatch_desc) {
        dispatch_desc->HitGroupTable.StartAddress = m_hit_group_shader_table->GetGPUVirtualAddress();
        dispatch_desc->HitGroupTable.SizeInBytes = m_hit_group_shader_table->GetDesc().Width;
        dispatch_desc->HitGroupTable.StrideInBytes = m_hit_group_shader_table_stride_in_bytes;
        dispatch_desc->MissShaderTable.StartAddress = m_miss_shader_table->GetGPUVirtualAddress();
        dispatch_desc->MissShaderTable.SizeInBytes = m_miss_shader_table->GetDesc().Width;
        dispatch_desc->MissShaderTable.StrideInBytes = m_miss_shader_table_stride_in_bytes;
        dispatch_desc->RayGenerationShaderRecord.StartAddress = m_ray_gen_shader_table->GetGPUVirtualAddress();
        dispatch_desc->RayGenerationShaderRecord.SizeInBytes = m_ray_gen_shader_table->GetDesc().Width;
        dispatch_desc->Width = m_window->get_width();
        dispatch_desc->Height = m_window->get_height();
        dispatch_desc->Depth = 1;
        dxr_command_list->SetPipelineState1(state_object);

        m_gpu_timers[GpuTimers::Raytracing].Start(command_list);
        dxr_command_list->DispatchRays(dispatch_desc);
        m_gpu_timers[GpuTimers::Raytracing].Stop(command_list);
    };

    auto set_common_pipeline_state = [&](auto* descriptor_set_command_list) {
        descriptor_set_command_list->SetDescriptorHeaps(1, m_descriptor_heap.GetAddressOf());

        // Set index and successive vertex buffer descriptor tables
        command_list->SetComputeRootDescriptorTable(GlobalRootSignature::Slot::VertexBuffers, m_index_buffer.gpu_descriptor_handle);
        command_list->SetComputeRootDescriptorTable(GlobalRootSignature::Slot::OutputView, m_raytracing_output_resource_uav_gpu_descriptor);
    };

    command_list->SetComputeRootSignature(m_raytracing_global_root_signature.Get());

    u32 const frame_index = m_device_resources->get_current_frame_index();

    // Copy dynamic buffers to GPU.
    {
        m_scene_cb.CopyStagingToGpu(frame_index);
        command_list->SetComputeRootConstantBufferView(GlobalRootSignature::Slot::SceneConstant, m_scene_cb.GpuVirtualAddress(frame_index));

        m_aabb_primitive_attribute_buffer.CopyStagingToGpu(frame_index);
        command_list->SetComputeRootShaderResourceView(GlobalRootSignature::Slot::AABBAttributeBuffer,
                                                       m_aabb_primitive_attribute_buffer.GpuVirtualAddress(frame_index));
    }

    // Bind the heaps, acceleration structure and dispatch rays.
    D3D12_DISPATCH_RAYS_DESC dispatch_desc = {};
    set_common_pipeline_state(command_list);
    command_list->SetComputeRootShaderResourceView(GlobalRootSignature::Slot::AccelerationStructure,
                                                   m_top_level_as->GetGPUVirtualAddress());
    dispatch_rays(m_dxr_command_list.Get(), m_dxr_state_object.Get(), &dispatch_desc);
}

void Renderer::copy_raytracing_output_to_backbuffer() const
{
    auto const command_list = m_device_resources->get_command_list();
    auto const render_target = m_device_resources->get_render_target();

    std::array<D3D12_RESOURCE_BARRIER, 2> const pre_copy_barriers = {
        CD3DX12_RESOURCE_BARRIER::Transition(render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST),
        CD3DX12_RESOURCE_BARRIER::Transition(m_raytracing_output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                             D3D12_RESOURCE_STATE_COPY_SOURCE),
    };

    command_list->ResourceBarrier(pre_copy_barriers.size(), pre_copy_barriers.data());

    command_list->CopyResource(render_target, m_raytracing_output.Get());

    std::array<D3D12_RESOURCE_BARRIER, 2> const post_copy_barriers = {
        CD3DX12_RESOURCE_BARRIER::Transition(render_target, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT),
        CD3DX12_RESOURCE_BARRIER::Transition(m_raytracing_output.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
    };

    command_list->ResourceBarrier(post_copy_barriers.size(), post_copy_barriers.data());
}

void Renderer::build_geometry()
{
    build_procedural_geometry_aabbs();
    build_plane_geometry();
}

void Renderer::build_procedural_geometry_aabbs()
{
    auto const device = m_device_resources->get_d3d_device();

    // Set up AABBs on a grid.
    {
        auto constexpr aabb_grid = XMINT3(4, 1, 4);
        XMFLOAT3 constexpr base_position = {
            -(aabb_grid.x * aabb_width + (aabb_grid.x - 1) * aabb_distance) / 2.0f,
            -(aabb_grid.y * aabb_width + (aabb_grid.y - 1) * aabb_distance) / 2.0f,
            -(aabb_grid.z * aabb_width + (aabb_grid.z - 1) * aabb_distance) / 2.0f,
        };

        auto stride = XMFLOAT3(aabb_width + aabb_distance, aabb_width + aabb_distance, aabb_width + aabb_distance);
        auto initialize_aabb = [&](auto const& offset_index, auto const& size) {
            return D3D12_RAYTRACING_AABB {
                base_position.x + offset_index.x * stride.x,          base_position.y + offset_index.y * stride.y,
                base_position.z + offset_index.z * stride.z,          base_position.x + offset_index.x * stride.x + size.x,
                base_position.y + offset_index.y * stride.y + size.y, base_position.z + offset_index.z * stride.z + size.z,
            };
        };
        m_aabbs.resize(IntersectionShaderType::TOTAL_PRIMITIVE_COUNT);
        u32 offset = 0;

        // Analytic primitives.
        {
            using namespace AnalyticPrimitive;
            m_aabbs[offset + AABB] = initialize_aabb(XMINT3(3, 0, 0), XMFLOAT3(2, 3, 2));
            m_aabbs[offset + Spheres] = initialize_aabb(XMFLOAT3(2.25f, 0, 0.75f), XMFLOAT3(3, 3, 3));
            offset += AnalyticPrimitive::Count;
        }

        // Volumetric primitives.
        {
            using namespace VolumetricPrimitive;
            m_aabbs[offset + Metaballs] = initialize_aabb(XMINT3(0, 0, 0), XMFLOAT3(3, 3, 3));
            offset += VolumetricPrimitive::Count;
        }

        // Signed distance primitives.
        {
            using namespace SignedDistancePrimitive;
            m_aabbs[offset + MiniSpheres] = initialize_aabb(XMINT3(2, 0, 0), XMFLOAT3(2, 2, 2));
            m_aabbs[offset + TwistedTorus] = initialize_aabb(XMINT3(0, 0, 1), XMFLOAT3(2, 2, 2));
            m_aabbs[offset + IntersectedRoundCube] = initialize_aabb(XMINT3(0, 0, 2), XMFLOAT3(2, 2, 2));
            m_aabbs[offset + SquareTorus] = initialize_aabb(XMFLOAT3(0.75f, -0.1f, 2.25f), XMFLOAT3(3, 3, 3));
            m_aabbs[offset + Cog] = initialize_aabb(XMINT3(1, 0, 0), XMFLOAT3(2, 2, 2));
            m_aabbs[offset + Cylinder] = initialize_aabb(XMINT3(0, 0, 3), XMFLOAT3(2, 3, 2));
            m_aabbs[offset + FractalPyramid] = initialize_aabb(XMINT3(2, 0, 2), XMFLOAT3(6, 6, 6));
        }

        AllocateUploadBuffer(device, m_aabbs.data(), m_aabbs.size() * sizeof(m_aabbs[0]), &m_aabb_buffer.resource);
    }
}

void Renderer::build_plane_geometry()
{
    auto const device = m_device_resources->get_d3d_device();

    // Plane indices.
    Index indices[] = {
        3, 1, 0, 2, 1, 3,
    };

    // Cube vertices positions and corresponding triangle normals.
    std::array<Vertex, 4> vertices = {
        Vertex {XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f)},
        {XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f)},
        {XMFLOAT3(1.0f, 0.0f, 1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f)},
        {XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f)},
    };

    AllocateUploadBuffer(device, indices, sizeof(indices), &m_index_buffer.resource);
    AllocateUploadBuffer(device, vertices.data(), sizeof(vertices), &m_vertex_buffer.resource);

    // Vertex buffer is passed to the shader along with index buffer as a descriptor range.
    u32 const descriptor_index_ib = create_buffer_srv(&m_index_buffer, sizeof(indices) / 4, 0);
    u32 const descriptor_index_vb = create_buffer_srv(&m_vertex_buffer, vertices.size(), sizeof(vertices[0]));

    // Vertex Buffer descriptor index must follow that of Index Buffer descriptor index.
    assert(descriptor_index_vb == descriptor_index_ib + 1);
}

void Renderer::build_geometry_descs_for_bottom_level_as(
    std::array<std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>, BottomLevelASType::Count>& geometry_descs)
{
    // Mark the geometry as opaque.
    // PERFORMANCE TIP: Mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
    // NOTE: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
    D3D12_RAYTRACING_GEOMETRY_FLAGS constexpr geometry_flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    // Triangle geometry desc
    {
        // Triangle bottom-level AS contains a single plane geometry.
        geometry_descs[BottomLevelASType::Triangle].resize(1);

        // Plane geometry
        auto& geometry_desc = geometry_descs[BottomLevelASType::Triangle][0];
        geometry_desc = {};
        geometry_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometry_desc.Triangles.IndexBuffer = m_index_buffer.resource->GetGPUVirtualAddress();
        geometry_desc.Triangles.IndexCount = static_cast<UINT>(m_index_buffer.resource->GetDesc().Width) / sizeof(Index);
        geometry_desc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
        geometry_desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geometry_desc.Triangles.VertexCount = static_cast<UINT>(m_vertex_buffer.resource->GetDesc().Width) / sizeof(Vertex);
        geometry_desc.Triangles.VertexBuffer.StartAddress = m_vertex_buffer.resource->GetGPUVirtualAddress();
        geometry_desc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
        geometry_desc.Flags = geometry_flags;
    }

    // AABB geometry desc
    {
        D3D12_RAYTRACING_GEOMETRY_DESC aabb_desc_template = {};
        aabb_desc_template.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
        aabb_desc_template.AABBs.AABBCount = 1;
        aabb_desc_template.AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);
        aabb_desc_template.Flags = geometry_flags;

        // One AABB primitive per geometry.
        geometry_descs[BottomLevelASType::AABB].resize(IntersectionShaderType::TOTAL_PRIMITIVE_COUNT, aabb_desc_template);

        // Create AABB geometries.
        // Having separate geometries allows of separate shader record binding per geometry.
        // In this sample, this lets us specify custom hit groups per AABB geometry.
        for (u32 i = 0; i < IntersectionShaderType::TOTAL_PRIMITIVE_COUNT; i++)
        {
            auto& geometry_desc = geometry_descs[BottomLevelASType::AABB][i];
            geometry_desc.AABBs.AABBs.StartAddress = m_aabb_buffer.resource->GetGPUVirtualAddress() + i * sizeof(D3D12_RAYTRACING_AABB);
        }
    }
}

AccelerationStructureBuffers Renderer::build_bottom_level_as(std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> const& geometry_descs,
                                                             D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS build_flags) const
{
    auto const device = m_device_resources->get_d3d_device();
    ComPtr<ID3D12Resource> scratch = {};
    ComPtr<ID3D12Resource> bottom_level_as = {};

    // Get the size requirements for the scratch and AS buffers.
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottom_level_build_desc = {};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& bottom_level_inputs = bottom_level_build_desc.Inputs;
    bottom_level_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bottom_level_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    bottom_level_inputs.Flags = build_flags;
    bottom_level_inputs.NumDescs = static_cast<u32>(geometry_descs.size());
    bottom_level_inputs.pGeometryDescs = geometry_descs.data();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottom_level_prebuild_info = {};
    m_dxr_device->GetRaytracingAccelerationStructurePrebuildInfo(&bottom_level_inputs, &bottom_level_prebuild_info);
    assert(bottom_level_prebuild_info.ResultDataMaxSizeInBytes > 0);

    // Create a scratch buffer.
    AllocateUAVBuffer(device, bottom_level_prebuild_info.ScratchDataSizeInBytes, &scratch, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                      L"ScratchResource");

    // Allocate resources for acceleration structures.
    // Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent).
    // Default heap is OK since the application doesn't need CPU read/write access to them.
    // The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
    // and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both:
    //  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
    //  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
    {
        D3D12_RESOURCE_STATES constexpr initial_resource_state = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        AllocateUAVBuffer(device, bottom_level_prebuild_info.ResultDataMaxSizeInBytes, &bottom_level_as, initial_resource_state,
                          L"BottomLevelAccelerationStructure");
    }

    // Bottom-level AS desc.
    {
        bottom_level_build_desc.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
        bottom_level_build_desc.DestAccelerationStructureData = bottom_level_as->GetGPUVirtualAddress();
    }

    // Build the acceleration structure.
    m_dxr_command_list->BuildRaytracingAccelerationStructure(&bottom_level_build_desc, 0, nullptr);

    AccelerationStructureBuffers bottom_level_as_buffers;
    bottom_level_as_buffers.accelerationStructure = bottom_level_as;
    bottom_level_as_buffers.scratch = scratch;
    bottom_level_as_buffers.ResultDataMaxSizeInBytes = bottom_level_prebuild_info.ResultDataMaxSizeInBytes;
    return bottom_level_as_buffers;
}

AccelerationStructureBuffers Renderer::build_top_level_as(AccelerationStructureBuffers bottom_level_as[2],
                                                          D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS build_flags)
{
    auto const device = m_device_resources->get_d3d_device();
    ComPtr<ID3D12Resource> scratch = {};
    ComPtr<ID3D12Resource> top_level_as = {};

    // Get required sizes for an acceleration structure.
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top_level_build_desc = {};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& top_level_inputs = top_level_build_desc.Inputs;
    top_level_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    top_level_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    top_level_inputs.Flags = build_flags;
    top_level_inputs.NumDescs = num_blas;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO top_level_prebuild_info = {};
    m_dxr_device->GetRaytracingAccelerationStructurePrebuildInfo(&top_level_inputs, &top_level_prebuild_info);
    ThrowIfFalse(top_level_prebuild_info.ResultDataMaxSizeInBytes > 0);

    AllocateUAVBuffer(device, top_level_prebuild_info.ScratchDataSizeInBytes, &scratch, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                      L"ScratchResource");

    // Allocate resources for acceleration structures.
    // Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent).
    // Default heap is OK since the application doesn't need CPU read/write access to them.
    // The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
    // and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both:
    //  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
    //  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
    {
        D3D12_RESOURCE_STATES constexpr initial_resource_state = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        AllocateUAVBuffer(device, top_level_prebuild_info.ResultDataMaxSizeInBytes, &top_level_as, initial_resource_state,
                          L"TopLevelAccelerationStructure");
    }

    // Create instance descs for the bottom-level acceleration structures.
    ComPtr<ID3D12Resource> instance_descs_resource = {};
    {
        D3D12_GPU_VIRTUAL_ADDRESS bottom_level_a_saddresses[BottomLevelASType::Count] = {
            bottom_level_as[0].accelerationStructure->GetGPUVirtualAddress(),
            bottom_level_as[1].accelerationStructure->GetGPUVirtualAddress(),
        };
        build_bottom_level_as_instance_descs<D3D12_RAYTRACING_INSTANCE_DESC>(bottom_level_a_saddresses, &instance_descs_resource);
    }

    // Top-level AS desc
    {
        top_level_build_desc.DestAccelerationStructureData = top_level_as->GetGPUVirtualAddress();
        top_level_inputs.InstanceDescs = instance_descs_resource->GetGPUVirtualAddress();
        top_level_build_desc.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
    }

    // Build acceleration structure.
    m_dxr_command_list->BuildRaytracingAccelerationStructure(&top_level_build_desc, 0, nullptr);

    AccelerationStructureBuffers top_level_as_buffers;
    top_level_as_buffers.accelerationStructure = top_level_as;
    top_level_as_buffers.instanceDesc = instance_descs_resource;
    top_level_as_buffers.scratch = scratch;
    top_level_as_buffers.ResultDataMaxSizeInBytes = top_level_prebuild_info.ResultDataMaxSizeInBytes;
    return top_level_as_buffers;
}

template<class InstanceDescType, class BLASPtrType>
void Renderer::build_bottom_level_as_instance_descs(BLASPtrType* bottom_level_as_addresses, ComPtr<ID3D12Resource>* instance_descs_resource)
{
    auto const device = m_device_resources->get_d3d_device();

    std::vector<InstanceDescType> instance_descs = {};
    instance_descs.resize(num_blas);

    // Width of a bottom-level AS geometry.
    // Make the plane a little larger than the actual number of primitives in each dimension.
    auto constexpr num_aabb = XMUINT3(700, 1, 700);
    auto constexpr f_width =
        XMFLOAT3(num_aabb.x * aabb_width + (num_aabb.x - 1) * aabb_distance, num_aabb.y * aabb_width + (num_aabb.y - 1) * aabb_distance,
                 num_aabb.z * aabb_width + (num_aabb.z - 1) * aabb_distance);
    const XMVECTOR v_width = XMLoadFloat3(&f_width);

    // Bottom-level AS with a single plane.
    {
        auto& instance_desc = instance_descs[BottomLevelASType::Triangle];
        instance_desc = {};
        instance_desc.InstanceMask = 1;
        instance_desc.InstanceContributionToHitGroupIndex = 0;
        instance_desc.AccelerationStructure = bottom_level_as_addresses[BottomLevelASType::Triangle];

        // Calculate transformation matrix.
        auto constexpr base_position = XMFLOAT3(-0.35f, 0.0f, -0.35f);
        XMVECTOR const v_base_position = v_width * XMLoadFloat3(&base_position);

        // Scale in XZ dimensions.
        XMMATRIX const m_scale = XMMatrixScaling(f_width.x, f_width.y, f_width.z);
        XMMATRIX const m_translation = XMMatrixTranslationFromVector(v_base_position);
        XMMATRIX const m_transform = m_scale * m_translation;
        XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(instance_desc.Transform), m_transform);
    }

    // Create instanced bottom-level AS with procedural geometry AABBs.
    // Instances share all the data, except for a transform.
    {
        auto& instance_desc = instance_descs[BottomLevelASType::AABB];
        instance_desc = {};
        instance_desc.InstanceMask = 1;

        // Set hit group offset to beyond the shader records for the triangle AABB.
        instance_desc.InstanceContributionToHitGroupIndex = BottomLevelASType::AABB * RayType::Count;
        instance_desc.AccelerationStructure = bottom_level_as_addresses[BottomLevelASType::AABB];

        // Move all AABBS above the ground plane.
        auto constexpr y_translate = XMFLOAT3(0.0f, aabb_width / 2.0f, 0.0f);
        XMMATRIX const m_translation = XMMatrixTranslationFromVector(XMLoadFloat3(&y_translate));
        XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(instance_desc.Transform), m_translation);
    }

    u64 const buffer_size = static_cast<u64>(instance_descs.size() * sizeof(instance_descs[0]));
    AllocateUploadBuffer(device, instance_descs.data(), buffer_size, &(*instance_descs_resource), L"InstanceDescs");
}

void Renderer::build_acceleration_structures()
{
    auto const device = m_device_resources->get_d3d_device();
    auto const command_list = m_device_resources->get_command_list();
    auto const command_allocator = m_device_resources->get_command_allocator();

    // Reset the command list for the acceleration structure construction.
    HRESULT const hr = command_list->Reset(command_allocator, nullptr);
    assert(SUCCEEDED(hr));

    // Build bottom-level AS.
    std::array<AccelerationStructureBuffers, BottomLevelASType::Count> bottom_level_as = {};
    std::array<std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>, BottomLevelASType::Count> geometry_descs = {};
    {
        build_geometry_descs_for_bottom_level_as(geometry_descs);

        // Build all bottom-level AS.
        for (u32 i = 0; i < BottomLevelASType::Count; i++)
        {
            bottom_level_as[i] = build_bottom_level_as(geometry_descs[i]);
        }
    }

    // Batch all resource barriers for bottom-level AS builds.
    std::array<D3D12_RESOURCE_BARRIER, BottomLevelASType::Count> resource_barriers = {};
    for (u32 i = 0; i < BottomLevelASType::Count; i++)
    {
        resource_barriers[i] = CD3DX12_RESOURCE_BARRIER::UAV(bottom_level_as[i].accelerationStructure.Get());
    }

    command_list->ResourceBarrier(BottomLevelASType::Count, resource_barriers.data());

    // Build top-level AS.
    AccelerationStructureBuffers const top_level_as = build_top_level_as(bottom_level_as.data());

    // Kick off acceleration structure construction.
    m_device_resources->execute_command_list();

    // Wait for GPU to finish as the locally created temporary GPU resources will get released once we go out of scope.
    m_device_resources->wait_for_gpu();

    // Store the AS buffers. The rest of the buffers will be released once we exit the function.
    for (u32 i = 0; i < BottomLevelASType::Count; i++)
    {
        m_bottom_level_as[i] = bottom_level_as[i].accelerationStructure;
    }
    m_top_level_as = top_level_as.accelerationStructure;
}

// Build shader tables.
// This encapsulates all shader records - shaders and the arguments for their local root signatures.
void Renderer::build_shader_tables()
{
    auto const device = m_device_resources->get_d3d_device();

    void* ray_gen_shader_identifier = nullptr;
    std::array<void*, RayType::Count> miss_shader_identifiers = {};
    std::array<void*, RayType::Count> hit_group_shader_identifiers_triangle_geometry = {};
    void* hit_group_shader_identifiers_aabb_geometry[IntersectionShaderType::Count][RayType::Count];

    // A shader name look-up table for shader table debug print out.
    std::unordered_map<void*, std::wstring> shader_id_to_string_map = {};

    auto get_shader_identifiers = [&](auto* state_object_properties) {
        ray_gen_shader_identifier = state_object_properties->GetShaderIdentifier(raygen_shader_name);
        shader_id_to_string_map[ray_gen_shader_identifier] = raygen_shader_name;

        for (u32 i = 0; i < RayType::Count; i++)
        {
            miss_shader_identifiers[i] = state_object_properties->GetShaderIdentifier(miss_shader_names[i]);
            shader_id_to_string_map[miss_shader_identifiers[i]] = miss_shader_names[i];
        }

        for (u32 i = 0; i < RayType::Count; i++)
        {
            hit_group_shader_identifiers_triangle_geometry[i] =
                state_object_properties->GetShaderIdentifier(hit_group_names_triangle_geometry[i]);
            shader_id_to_string_map[hit_group_shader_identifiers_triangle_geometry[i]] = hit_group_names_triangle_geometry[i];
        }

        for (u32 r = 0; r < IntersectionShaderType::Count; r++)
        {
            for (u32 c = 0; c < RayType::Count; c++)
            {
                hit_group_shader_identifiers_aabb_geometry[r][c] =
                    state_object_properties->GetShaderIdentifier(hit_group_names_aabb_geometry[r][c]);
                shader_id_to_string_map[hit_group_shader_identifiers_aabb_geometry[r][c]] = hit_group_names_aabb_geometry[r][c];
            }
        }
    };

    // Get shader identifiers.
    u32 shader_identifier_size;
    {
        ComPtr<ID3D12StateObjectProperties> state_object_properties = {};
        HRESULT const hr = m_dxr_state_object.As(&state_object_properties);
        assert(SUCCEEDED(hr));

        get_shader_identifiers(state_object_properties.Get());
        shader_identifier_size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    }

    /*************--------- Shader table layout -------*******************
    | --------------------------------------------------------------------
    | Shader table - HitGroupShaderTable: 
    | [0] : MyHitGroup_Triangle
    | [1] : MyHitGroup_Triangle_ShadowRay
    | [2] : MyHitGroup_AABB_AnalyticPrimitive
    | [3] : MyHitGroup_AABB_AnalyticPrimitive_ShadowRay 
    | ...
    | [6] : MyHitGroup_AABB_VolumetricPrimitive
    | [7] : MyHitGroup_AABB_VolumetricPrimitive_ShadowRay
    | [8] : MyHitGroup_AABB_SignedDistancePrimitive 
    | [9] : MyHitGroup_AABB_SignedDistancePrimitive_ShadowRay,
    | ...
    | [20] : MyHitGroup_AABB_SignedDistancePrimitive
    | [21] : MyHitGroup_AABB_SignedDistancePrimitive_ShadowRay
    | --------------------------------------------------------------------
    **********************************************************************/

    // Ray gen shader table
    {
        u32 constexpr num_shader_records = 1;
        u32 const shader_record_size = shader_identifier_size;

        ShaderTable ray_gen_shader_table(device, num_shader_records, shader_record_size, L"RayGenShaderTable");
        ray_gen_shader_table.push_back(ShaderRecord(ray_gen_shader_identifier, shader_record_size, nullptr, 0));
        ray_gen_shader_table.DebugPrint(shader_id_to_string_map);
        m_ray_gen_shader_table = ray_gen_shader_table.GetResource();
    }

    // Miss shader table
    {
        u32 constexpr num_shader_records = RayType::Count;
        u32 const shader_record_size = shader_identifier_size;

        ShaderTable miss_shader_table(device, num_shader_records, shader_record_size, L"MissShaderTable");

        for (u32 i = 0; i < RayType::Count; i++)
        {
            miss_shader_table.push_back(ShaderRecord(miss_shader_identifiers[i], shader_identifier_size, nullptr, 0));
        }

        miss_shader_table.DebugPrint(shader_id_to_string_map);
        m_miss_shader_table_stride_in_bytes = miss_shader_table.GetShaderRecordSize();
        m_miss_shader_table = miss_shader_table.GetResource();
    }

    // Hit group shader table
    {
        u32 constexpr num_shader_records = RayType::Count + IntersectionShaderType::TOTAL_PRIMITIVE_COUNT * RayType::Count;
        u32 const shader_record_size = shader_identifier_size + LocalRootSignature::max_root_arguments_size();
        ShaderTable hit_group_shader_table(device, num_shader_records, shader_record_size, L"HitGroupShaderTable");

        // Triangle geometry hit groups.
        {
            LocalRootSignature::Triangle::RootArguments root_args = {};
            root_args.material_cb = m_plane_material_cb;

            for (auto& hit_group_shader_id : hit_group_shader_identifiers_triangle_geometry)
            {
                hit_group_shader_table.push_back(ShaderRecord(hit_group_shader_id, shader_identifier_size, &root_args, sizeof(root_args)));
            }
        }

        // AABB geometry hit groups.
        {
            LocalRootSignature::AABB::RootArguments root_args = {};

            // Create a shader record for each primitive.
            for (u32 i_shader = 0, instance_index = 0; i_shader < IntersectionShaderType::Count; i_shader++)
            {
                u32 num_primitive_types =
                    IntersectionShaderType::per_primitive_type_count(static_cast<IntersectionShaderType::Enum>(i_shader));

                // Primitives for each intersection shader.
                for (u32 primitive_index = 0; primitive_index < num_primitive_types; primitive_index++, instance_index++)
                {
                    root_args.material_cb = m_aabb_material_cb[instance_index];
                    root_args.aabb_cb.instance_index = instance_index;
                    root_args.aabb_cb.primitive_type = primitive_index;

                    // Ray types.
                    for (UINT r = 0; r < RayType::Count; r++)
                    {
                        auto& hit_group_shader_id = hit_group_shader_identifiers_aabb_geometry[i_shader][r];
                        hit_group_shader_table.push_back(
                            ShaderRecord(hit_group_shader_id, shader_identifier_size, &root_args, sizeof(root_args)));
                    }
                }
            }
        }

        hit_group_shader_table.DebugPrint(shader_id_to_string_map);
        m_hit_group_shader_table_stride_in_bytes = hit_group_shader_table.GetShaderRecordSize();
        m_hit_group_shader_table = hit_group_shader_table.GetResource();
    }
}

void Renderer::create_device_dependent_resources()
{
    create_auxilary_device_resources();

    // Initialize raytracing pipeline.

    // Create raytracing interfaces: raytracing device and command list.
    create_raytracing_interfaces();

    // Create root signatures for the shaders.
    create_root_signatures();

    // Create a raytracing pipeline state object which defines the binding of shaders, state and resources to be used during raytracing.
    create_raytracing_pipeline_state_object();

    // Create a heap for descriptors.
    create_descriptor_heap();

    build_geometry();

    // Build raytracing acceleration structures from the generated geometry.
    build_acceleration_structures();

    // Create constant buffers for the geometry and the scene.
    create_constant_buffers();

    // Create AABB primitive attribute buffers.
    create_aabb_primitive_attributes_buffers();

    // Build shader tables, which define shaders and their local root arguments.
    build_shader_tables();

    // Create an output 2D texture to store the raytracing result to.
    create_raytracing_output_resource();
}

void Renderer::create_raytracing_interfaces()
{
    auto const device = m_device_resources->get_d3d_device();
    auto const command_list = m_device_resources->get_command_list();

    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&m_dxr_device));

    if (FAILED(hr))
    {
        std::cerr << "Couldn't get DirectX Raytracing interface for the device.\n";
        assert(false);
    }

    hr = command_list->QueryInterface(IID_PPV_ARGS(&m_dxr_command_list));

    if (FAILED(hr))
    {
        std::cerr << "Couldn't get DirectX Raytracing interface for the command list.\n";
        assert(false);
    }
}

void Renderer::create_root_signatures()
{
    // Global Root Signature
    // This is a root signature that is shared across all raytracing shaders invoked during a dispatch_rays() call.
    {
        // Perfomance TIP: Order from most frequent to least frequent.
        std::array<CD3DX12_DESCRIPTOR_RANGE, 2> ranges = {};
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // 1 output texture
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1); // 2 static index and vertex buffers

        std::array<CD3DX12_ROOT_PARAMETER, GlobalRootSignature::Slot::Count> root_parameters = {};
        root_parameters[GlobalRootSignature::Slot::OutputView].InitAsDescriptorTable(1, &ranges[0]);
        root_parameters[GlobalRootSignature::Slot::AccelerationStructure].InitAsShaderResourceView(0);
        root_parameters[GlobalRootSignature::Slot::SceneConstant].InitAsConstantBufferView(0);
        root_parameters[GlobalRootSignature::Slot::AABBAttributeBuffer].InitAsShaderResourceView(3);
        root_parameters[GlobalRootSignature::Slot::VertexBuffers].InitAsDescriptorTable(1, &ranges[1]);

        CD3DX12_ROOT_SIGNATURE_DESC const global_root_signature_desc(root_parameters.size(), root_parameters.data());
        serialize_and_create_raytracing_root_signature(global_root_signature_desc, &m_raytracing_global_root_signature);
    }

    // Local Root Signature
    // This is a root signature that enables a shader to have unique arguments that come from shader tables.
    {
        // Triangle geometry
        {
            namespace RootSignatureSlots = LocalRootSignature::Triangle::Slot;
            std::array<CD3DX12_ROOT_PARAMETER, RootSignatureSlots::Count> root_parameters = {};
            root_parameters[RootSignatureSlots::MaterialConstant].InitAsConstants(SIZE_OF_IN_UINT32(PrimitiveConstantBuffer), 1);

            CD3DX12_ROOT_SIGNATURE_DESC local_root_signature_desc(root_parameters.size(), root_parameters.data());
            local_root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
            serialize_and_create_raytracing_root_signature(local_root_signature_desc,
                                                           &m_raytracing_local_root_signature[LocalRootSignature::Type::Triangle]);
        }

        // AABB geometry
        {
            namespace RootSignatureSlots = LocalRootSignature::AABB::Slot;
            std::array<CD3DX12_ROOT_PARAMETER, RootSignatureSlots::Count> root_parameters = {};
            root_parameters[RootSignatureSlots::MaterialConstant].InitAsConstants(SIZE_OF_IN_UINT32(PrimitiveConstantBuffer), 1);
            root_parameters[RootSignatureSlots::GeometryIndex].InitAsConstants(SIZE_OF_IN_UINT32(PrimitiveInstanceConstantBuffer), 2);

            CD3DX12_ROOT_SIGNATURE_DESC local_root_signature_desc(root_parameters.size(), root_parameters.data());
            local_root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
            serialize_and_create_raytracing_root_signature(local_root_signature_desc,
                                                           &m_raytracing_local_root_signature[LocalRootSignature::Type::AABB]);
        }
    }
}

// DXIL library
// This contains the shaders and their entrypoints for the state object.
// Since shaders are not considered a subobject, they need to be passed in via DXIL library subobjects.
void Renderer::create_dxil_library_subobject(CD3DX12_STATE_OBJECT_DESC* raytracing_pipeline)
{
    auto const library = raytracing_pipeline->CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE library_dxil = CD3DX12_SHADER_BYTECODE((void*)g_pRaytracing, ARRAYSIZE(g_pRaytracing));
    library->SetDXILLibrary(&library_dxil);
    // Use default shader exports for a DXIL library/collection subobject ~ surface all shaders.
}

void Renderer::create_hit_group_subobjects(CD3DX12_STATE_OBJECT_DESC* raytracing_pipeline)
{
    // Triangle geometry hit groups
    {
        for (u32 ray_type = 0; ray_type < RayType::Count; ray_type++)
        {
            auto const hit_group = raytracing_pipeline->CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();

            if (ray_type == RayType::Radiance)
            {
                hit_group->SetClosestHitShaderImport(closest_hit_shader_names[GeometryType::Triangle]);
            }

            hit_group->SetHitGroupExport(hit_group_names_triangle_geometry[ray_type]);
            hit_group->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
        }
    }

    // AABB geometry hit groups
    {
        // Create hit groups for each intersection shader.
        for (u32 t = 0; t < IntersectionShaderType::Count; t++)
        {
            for (UINT ray_type = 0; ray_type < RayType::Count; ray_type++)
            {
                auto const hit_group = raytracing_pipeline->CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();

                hit_group->SetIntersectionShaderImport(intersection_shader_names[t]);

                if (ray_type == RayType::Radiance)
                {
                    hit_group->SetClosestHitShaderImport(closest_hit_shader_names[GeometryType::AABB]);
                }

                hit_group->SetHitGroupExport(hit_group_names_aabb_geometry[t][ray_type]);
                hit_group->SetHitGroupType(D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE);
            }
        }
    }
}

// Create a raytracing pipeline state object (RTPSO).
// An RTPSO represents a full set of shaders reachable by a dispatch_rays() call,
// with all configuration options resolved, such as local signatures and other state.
void Renderer::create_raytracing_pipeline_state_object()
{
    // Create 18 subobjects that combine into a RTPSO:
    // Subobjects need to be associated with DXIL exports (i.e. shaders) either by way of default or explicit associations.
    // Default association applies to every exported shader entrypoint that doesn't have any of the same type of subobject associated with it.
    // This simple sample utilizes default shader association except for local root signature subobject
    // which has an explicit association specified purely for demonstration purposes.
    // 1 - DXIL library
    // 8 - Hit group types - 4 geometries (1 triangle, 3 aabb) x 2 ray types (ray, shadowRay)
    // 1 - Shader config
    // 6 - 3 x Local root signature and association
    // 1 - Global root signature
    // 1 - Pipeline config
    CD3DX12_STATE_OBJECT_DESC raytracing_pipeline = {D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};

    // Create DXIL library
    create_dxil_library_subobject(&raytracing_pipeline);

    // Hit groups
    create_hit_group_subobjects(&raytracing_pipeline);

    // Shader config
    // Defines the maximum sizes in bytes for the ray payload and attribute structure.
    auto const shader_config = raytracing_pipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    u32 constexpr payload_size = std::max(sizeof(RayPayload), sizeof(ShadowRayPayload));
    u32 constexpr attribute_size = sizeof(struct ProceduralPrimitiveAttributes);
    shader_config->Config(payload_size, attribute_size);

    // Local root signature and shader association
    // This is a root signature that enables a shader to have unique arguments that come from shader tables.
    create_local_root_signature_subobjects(&raytracing_pipeline);

    // Global root signature
    // This is a root signature that is shared across all raytracing shaders invoked during a dispatch_rays() call.
    auto const global_root_signature = raytracing_pipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    global_root_signature->SetRootSignature(m_raytracing_global_root_signature.Get());

    // Pipeline config
    // Defines the maximum trace_ray() recursion depth.
    auto const pipeline_config = raytracing_pipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();

    // Set max recursion depth as low as needed as drivers may apply optimization strategies for low recursion depths.
    u32 constexpr max_recursion_depth = MAX_RAY_RECURSION_DEPTH;
    pipeline_config->Config(max_recursion_depth);

#if _DEBUG
    print_state_object_desc(raytracing_pipeline);
#endif

    HRESULT const hr = m_dxr_device->CreateStateObject(raytracing_pipeline, IID_PPV_ARGS(&m_dxr_state_object));
    assert(SUCCEEDED(hr));
}

void Renderer::create_auxilary_device_resources()
{
    auto const device = m_device_resources->get_d3d_device();
    auto const command_queue = m_device_resources->get_command_queue();

    for (auto& gpu_timer : m_gpu_timers)
    {
        gpu_timer.RestoreDevice(device, command_queue, frame_count);
    }
}

// Local root signature and shader association
// This is a root signature that enables a shader to have unique arguments that come from shader tables.
void Renderer::create_local_root_signature_subobjects(CD3DX12_STATE_OBJECT_DESC* raytracing_pipeline) const
{
    // TODO: Sample specific: Hit group and miss shaders in this sample are not using a local root signature and thus one is not associated with them.

    // Hit groups
    // Triangle geometry
    {
        auto const local_root_signature = raytracing_pipeline->CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
        local_root_signature->SetRootSignature(m_raytracing_local_root_signature[LocalRootSignature::Type::Triangle].Get());

        // Shader association
        auto const root_signature_association = raytracing_pipeline->CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
        root_signature_association->SetSubobjectToAssociate(*local_root_signature);
        root_signature_association->AddExports(hit_group_names_triangle_geometry);
    }

    // AABB geometry
    {
        auto const local_root_signature = raytracing_pipeline->CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
        local_root_signature->SetRootSignature(m_raytracing_local_root_signature[LocalRootSignature::Type::AABB].Get());

        // Shader association
        auto const root_signature_association = raytracing_pipeline->CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
        root_signature_association->SetSubobjectToAssociate(*local_root_signature);

        for (auto& hit_groups_for_intersection_shader_type : hit_group_names_aabb_geometry)
        {
            root_signature_association->AddExports(hit_groups_for_intersection_shader_type);
        }
    }
}

void Renderer::create_descriptor_heap()
{
    auto const device = m_device_resources->get_d3d_device();

    // Allocate a heap for 3 descriptors:
    // 2 - vertex and index buffer SRVs
    // 1 - raytracing output texture SRV
    D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {};
    descriptor_heap_desc.NumDescriptors = 3;
    descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    descriptor_heap_desc.NodeMask = 0;

    HRESULT const hr = device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&m_descriptor_heap));
    assert(SUCCEEDED(hr));

    NAME_D3D12_OBJECT(m_descriptor_heap);

    m_descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

// Create 2D output texture for raytracing.
void Renderer::create_raytracing_output_resource()
{
    auto const device = m_device_resources->get_d3d_device();
    auto const back_buffer_format = m_device_resources->get_back_buffer_format();

    // Create the output resource. The dimensions and format should match the swap chain.
    auto const uav_resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(back_buffer_format, m_window->get_width(), m_window->get_height(), 1, 1, 1,
                                                                0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    auto const deafult_heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    HRESULT const hr = device->CreateCommittedResource(&deafult_heap_properties, D3D12_HEAP_FLAG_NONE, &uav_resource_desc,
                                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_raytracing_output));
    assert(SUCCEEDED(hr));

    NAME_D3D12_OBJECT(m_raytracing_output);

    D3D12_CPU_DESCRIPTOR_HANDLE uav_descriptor_handle = {};
    m_raytracing_output_resource_uav_descriptor_heap_index =
        allocate_descriptor(&uav_descriptor_handle, m_raytracing_output_resource_uav_descriptor_heap_index);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(m_raytracing_output.Get(), nullptr, &uav_desc, uav_descriptor_handle);
    m_raytracing_output_resource_uav_gpu_descriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(
        m_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), m_raytracing_output_resource_uav_descriptor_heap_index, m_descriptor_size);
}

// Create resources that are dependent on the size of the main window.
void Renderer::create_window_size_dependent_resources()
{
    create_raytracing_output_resource();

    update_camera_matrices();
}

void Renderer::serialize_and_create_raytracing_root_signature(D3D12_ROOT_SIGNATURE_DESC const& desc,
                                                              ComPtr<ID3D12RootSignature>* root_signature) const
{
    auto const device = m_device_resources->get_d3d_device();
    ComPtr<ID3DBlob> blob = {};
    ComPtr<ID3DBlob> error = {};

    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);

    if (FAILED(hr))
    {
        std::cerr << "Error while trying to create a root signature.\n";

        assert(false);
    }

    hr = device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&(*root_signature)));
    assert(SUCCEEDED(hr));
}

// Allocate a descriptor and return its index.
// If the passed descriptorIndexToUse is valid, it will be used instead of allocating a new one.
u32 Renderer::allocate_descriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpu_descriptor, u32 descriptor_index_to_use)
{
    auto const descriptor_heap_cpu_base = m_descriptor_heap->GetCPUDescriptorHandleForHeapStart();

    if (descriptor_index_to_use >= m_descriptor_heap->GetDesc().NumDescriptors)
    {
        descriptor_index_to_use = m_descriptors_allocated;
        m_descriptors_allocated += 1;
    }

    *cpu_descriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptor_heap_cpu_base, descriptor_index_to_use, m_descriptor_size);
    return descriptor_index_to_use;
}

u32 Renderer::create_buffer_srv(D3DBuffer* buffer, u32 num_elements, u32 element_size)
{
    auto const device = m_device_resources->get_d3d_device();

    // SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Buffer.NumElements = num_elements;

    if (element_size == 0)
    {
        srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
        srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        srv_desc.Buffer.StructureByteStride = 0;
    }
    else
    {
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        srv_desc.Buffer.StructureByteStride = element_size;
    }

    u32 descriptor_index = allocate_descriptor(&buffer->cpu_descriptor_handle);
    device->CreateShaderResourceView(buffer->resource.Get(), &srv_desc, buffer->cpu_descriptor_handle);
    buffer->gpu_descriptor_handle =
        CD3DX12_GPU_DESCRIPTOR_HANDLE(m_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), descriptor_index, m_descriptor_size);
    return descriptor_index;
}

void Renderer::release_device_dependent_resources()
{
    for (auto& gpu_timer : m_gpu_timers)
    {
        gpu_timer.ReleaseDevice();
    }

    m_raytracing_global_root_signature.Reset();
    ResetComPtrArray(&m_raytracing_local_root_signature);

    m_dxr_device.Reset();
    m_dxr_command_list.Reset();
    m_dxr_state_object.Reset();

    m_descriptor_heap.Reset();
    m_descriptors_allocated = 0;
    m_scene_cb.Release();
    m_aabb_primitive_attribute_buffer.Release();
    m_index_buffer.resource.Reset();
    m_vertex_buffer.resource.Reset();
    m_aabb_buffer.resource.Reset();

    ResetComPtrArray(&m_bottom_level_as);
    m_top_level_as.Reset();

    m_raytracing_output.Reset();
    m_raytracing_output_resource_uav_descriptor_heap_index = UINT_MAX;
    m_ray_gen_shader_table.Reset();
    m_miss_shader_table.Reset();
    m_hit_group_shader_table.Reset();
}

void Renderer::release_window_size_dependent_resources()
{
    m_raytracing_output.Reset();
}
