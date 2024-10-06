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

wchar_t const* Renderer::hit_group_name = L"hit_group";
wchar_t const* Renderer::raygen_shader_name = L"raygen_shader";
wchar_t const* Renderer::closest_hit_shader_name = L"closest_hit_shader";
wchar_t const* Renderer::miss_shader_name = L"miss_shader";

Renderer::Renderer(u32 const width, u32 const height, std::wstring const& name)
{
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
    {
        float constexpr seconds_to_rotate_around = 24.0f;
        float const angle_to_rotate_by = 360.0f * (elapsed_time / seconds_to_rotate_around);
        XMMATRIX const rotate = XMMatrixRotationY(XMConvertToRadians(angle_to_rotate_by));
        m_eye = XMVector3Transform(m_eye, rotate);
        m_up = XMVector3Transform(m_up, rotate);
        m_at = XMVector3Transform(m_at, rotate);
        update_camera_matrices();
    }

    // Rotate the second light around Y axis.
    {
        float constexpr seconds_to_rotate_around = 8.0f;
        float const angle_to_rotate_by = -360.0f * (elapsed_time / seconds_to_rotate_around);
        XMMATRIX const rotate = XMMatrixRotationY(XMConvertToRadians(angle_to_rotate_by));
        m_scene_cb[frame_index].light_position = XMVector3Transform(m_scene_cb[previous_frame_index].light_position, rotate);
    }
}

void Renderer::on_render()
{
    if (!m_device_resources->is_window_visible())
    {
        return;
    }

    m_device_resources->prepare();

    do_raytracing();

    copy_raytracing_output_to_backbuffer();

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
        m_cube_cb.albedo = {1.0f, 1.0f, 1.0f, 1.0f};
    }

    // Setup camera.
    {
        // Initialize the view and projection inverse matrices.
        m_eye = {0.0f, 2.0f, -5.0f, 1.0f};
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

        light_position = {0.0f, 1.8f, -3.0f, 0.0f};
        m_scene_cb[frame_index].light_position = XMLoadFloat4(&light_position);

        light_ambient_color = {0.5f, 0.5f, 0.5f, 1.0f};
        m_scene_cb[frame_index].light_ambient_color = XMLoadFloat4(&light_ambient_color);

        light_diffuse_color = {0.5f, 0.0f, 0.0f, 1.0f};
        m_scene_cb[frame_index].light_diffuse_color = XMLoadFloat4(&light_diffuse_color);
    }

    // Apply the initial values to all frames' buffer instances.
    for (auto& scene_cb : m_scene_cb)
    {
        scene_cb = m_scene_cb[frame_index];
    }
}

void Renderer::update_camera_matrices()
{
    auto const frame_index = m_device_resources->get_current_frame_index();

    m_scene_cb[frame_index].camera_position = m_eye;
    float constexpr fov_angle_y = 45.0f;
    XMMATRIX const view = XMMatrixLookAtLH(m_eye, m_at, m_up);
    XMMATRIX const proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(fov_angle_y), m_window->get_aspect_ratio(), 1.0f, 125.0f);
    XMMATRIX const view_proj = view * proj;

    m_scene_cb[frame_index].projection_to_world = XMMatrixInverse(nullptr, view_proj);
}

void Renderer::create_constant_buffers()
{
    auto const device = m_device_resources->get_d3d_device();
    u32 const frame_count = m_device_resources->get_back_buffer_count();

    // Create the constant buffer memory and map the CPU and GPU addresses
    D3D12_HEAP_PROPERTIES const upload_heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    // Allocate one constant buffer per frame, since it gets updated every frame.
    size_t const cb_size = frame_count * sizeof(AlignedSceneConstantBuffer);
    D3D12_RESOURCE_DESC const constant_buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(cb_size);

    HRESULT hr = device->CreateCommittedResource(&upload_heap_properties, D3D12_HEAP_FLAG_NONE, &constant_buffer_desc,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_per_frame_constants));
    assert(SUCCEEDED(hr));

    // Map the constant buffer and cache its heap pointers.
    // We don't unmap this until the app closes. Keeping buffer mapped for the lifetime of the resource is okay.
    hr = m_per_frame_constants->Map(0, nullptr, reinterpret_cast<void**>(&m_mapped_constant_data));
    assert(SUCCEEDED(hr));
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
        // Since each shader table has only one shader record, the stride is same as the size.
        dispatch_desc->HitGroupTable.StartAddress = m_hit_group_shader_table->GetGPUVirtualAddress();
        dispatch_desc->HitGroupTable.SizeInBytes = m_hit_group_shader_table->GetDesc().Width;
        dispatch_desc->HitGroupTable.StrideInBytes = dispatch_desc->HitGroupTable.SizeInBytes;
        dispatch_desc->MissShaderTable.StartAddress = m_miss_shader_table->GetGPUVirtualAddress();
        dispatch_desc->MissShaderTable.SizeInBytes = m_miss_shader_table->GetDesc().Width;
        dispatch_desc->MissShaderTable.StrideInBytes = dispatch_desc->MissShaderTable.SizeInBytes;
        dispatch_desc->RayGenerationShaderRecord.StartAddress = m_ray_gen_shader_table->GetGPUVirtualAddress();
        dispatch_desc->RayGenerationShaderRecord.SizeInBytes = m_ray_gen_shader_table->GetDesc().Width;
        dispatch_desc->Width = m_window->get_width();
        dispatch_desc->Height = m_window->get_height();
        dispatch_desc->Depth = 1;
        dxr_command_list->SetPipelineState1(state_object);
        dxr_command_list->DispatchRays(dispatch_desc);
    };

    auto set_common_pipeline_state = [&](auto* descriptor_set_command_list) {
        descriptor_set_command_list->SetDescriptorHeaps(1, m_descriptor_heap.GetAddressOf());

        // Set index and successive vertex buffer descriptor tables
        command_list->SetComputeRootDescriptorTable(GlobalRootSignatureParams::VertexBuffersSlot, m_index_buffer.gpu_descriptor_handle);
        command_list->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot,
                                                    m_raytracing_output_resource_uav_gpu_descriptor);
    };

    command_list->SetComputeRootSignature(m_raytracing_global_root_signature.Get());

    u32 const frame_index = m_device_resources->get_current_frame_index();

    // Copy the updated scene constant buffer to GPU.
    memcpy(&m_mapped_constant_data[frame_index].constants, &m_scene_cb[frame_index], sizeof(m_scene_cb[frame_index]));
    auto const cb_gpu_address = m_per_frame_constants->GetGPUVirtualAddress() + frame_index * sizeof(m_mapped_constant_data[0]);
    command_list->SetComputeRootConstantBufferView(GlobalRootSignatureParams::SceneConstantSlot, cb_gpu_address);

    // Bind the heaps, acceleration structure and dispatch rays.
    D3D12_DISPATCH_RAYS_DESC dispatch_desc = {};
    set_common_pipeline_state(command_list);
    command_list->SetComputeRootShaderResourceView(GlobalRootSignatureParams::AccelerationStructureSlot,
                                                   m_top_level_acceleration_structure->GetGPUVirtualAddress());
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
    auto const device = m_device_resources->get_d3d_device();

    std::array<Index, 36> indices = {
        3,  1,  0,  2,  1,  3,

        6,  4,  5,  7,  4,  6,

        11, 9,  8,  10, 9,  11,

        14, 12, 13, 15, 12, 14,

        19, 17, 16, 18, 17, 19,

        22, 20, 21, 23, 20, 22,
    };

    std::array<Vertex, 24> vertices = {
        Vertex {XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f)}, {XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f)},
        {XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f)},          {XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f)},

        {XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f)},      {XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f)},
        {XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f)},        {XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f)},

        {XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f)},       {XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f)},
        {XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f)},       {XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f)},

        {XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f)},         {XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f)},
        {XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f)},         {XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f)},

        {XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f)},      {XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f)},
        {XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f)},        {XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f)},

        {XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f)},        {XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f)},
        {XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f)},          {XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f)},
    };

    AllocateUploadBuffer(device, indices.data(), sizeof(indices), &m_index_buffer.resource);
    AllocateUploadBuffer(device, vertices.data(), sizeof(vertices), &m_vertex_buffer.resource);

    // Vertex buffer is passed to the shader along with index buffer as a descriptor table.
    // Vertex buffer descriptor must follow index buffer descriptor in the descriptor heap.
    u32 const descriptor_index_ib = create_buffer_srv(&m_index_buffer, sizeof(indices) / 4, 0);
    u32 const descriptor_index_vb = create_buffer_srv(&m_vertex_buffer, vertices.size(), sizeof(vertices[0]));
    assert(descriptor_index_vb == descriptor_index_ib + 1);
}

void Renderer::build_acceleration_structures()
{
    auto const device = m_device_resources->get_d3d_device();
    auto const command_list = m_device_resources->get_command_list();
    auto const command_allocator = m_device_resources->get_command_allocator();

    // Reset the command list for the acceleration structure construction.
    HRESULT hr = command_list->Reset(command_allocator, nullptr);
    assert(SUCCEEDED(hr));

    D3D12_RAYTRACING_GEOMETRY_DESC geometry_desc = {};
    geometry_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry_desc.Triangles.IndexBuffer = m_index_buffer.resource->GetGPUVirtualAddress();
    geometry_desc.Triangles.IndexCount = static_cast<u32>(m_index_buffer.resource->GetDesc().Width) / sizeof(Index);
    geometry_desc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
    geometry_desc.Triangles.Transform3x4 = 0;
    geometry_desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry_desc.Triangles.VertexCount = static_cast<u32>(m_vertex_buffer.resource->GetDesc().Width) / sizeof(Vertex);
    geometry_desc.Triangles.VertexBuffer.StartAddress = m_vertex_buffer.resource->GetGPUVirtualAddress();
    geometry_desc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);

    // Mark the geometry as opaque.
    // Mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
    // Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
    geometry_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    // Get required sizes for an acceleration structure.
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS constexpr build_flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottom_level_build_desc = {};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& bottom_level_inputs = bottom_level_build_desc.Inputs;
    bottom_level_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    bottom_level_inputs.Flags = build_flags;
    bottom_level_inputs.NumDescs = 1;
    bottom_level_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bottom_level_inputs.pGeometryDescs = &geometry_desc;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top_level_build_desc = {};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& top_level_inputs = top_level_build_desc.Inputs;
    top_level_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    top_level_inputs.Flags = build_flags;
    top_level_inputs.NumDescs = 1;
    top_level_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    top_level_inputs.pGeometryDescs = nullptr;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO top_level_prebuild_info = {};
    m_dxr_device->GetRaytracingAccelerationStructurePrebuildInfo(&top_level_inputs, &top_level_prebuild_info);
    assert(top_level_prebuild_info.ResultDataMaxSizeInBytes > 0);

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottom_level_prebuild_info = {};
    m_dxr_device->GetRaytracingAccelerationStructurePrebuildInfo(&bottom_level_inputs, &bottom_level_prebuild_info);
    assert(bottom_level_prebuild_info.ResultDataMaxSizeInBytes > 0);

    ComPtr<ID3D12Resource> scratch_resource = {};
    AllocateUAVBuffer(device, std::max(top_level_prebuild_info.ScratchDataSizeInBytes, bottom_level_prebuild_info.ScratchDataSizeInBytes),
                      &scratch_resource, D3D12_RESOURCE_STATE_COMMON, L"ScratchResource");

    // Allocate resources for acceleration structures.
    // Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent).
    // Default heap is OK since the application doesn't need CPU read/write access to them.
    // The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
    // and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both:
    //  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
    //  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
    {
        D3D12_RESOURCE_STATES constexpr initial_resource_state = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

        AllocateUAVBuffer(device, bottom_level_prebuild_info.ResultDataMaxSizeInBytes, &m_bottom_level_acceleration_structure,
                          initial_resource_state, L"BottomLevelAccelerationStructure");

        AllocateUAVBuffer(device, top_level_prebuild_info.ResultDataMaxSizeInBytes, &m_top_level_acceleration_structure,
                          initial_resource_state, L"TopLevelAccelerationStructure");
    }

    // Create an instance desc for the bottom-level acceleration structure.
    ComPtr<ID3D12Resource> instance_descs = {};
    D3D12_RAYTRACING_INSTANCE_DESC instance_desc = {};
    instance_desc.Transform[0][0] = instance_desc.Transform[1][1] = instance_desc.Transform[2][2] = 1;
    instance_desc.InstanceMask = 1;
    instance_desc.AccelerationStructure = m_bottom_level_acceleration_structure->GetGPUVirtualAddress();
    AllocateUploadBuffer(device, &instance_desc, sizeof(instance_desc), &instance_descs, L"InstanceDescs");

    // Bottom level acceleration structure des
    {
        bottom_level_build_desc.ScratchAccelerationStructureData = scratch_resource->GetGPUVirtualAddress();
        bottom_level_build_desc.DestAccelerationStructureData = m_bottom_level_acceleration_structure->GetGPUVirtualAddress();
    }

    // Top level acceleration structure desc
    {
        top_level_build_desc.DestAccelerationStructureData = m_top_level_acceleration_structure->GetGPUVirtualAddress();
        top_level_build_desc.ScratchAccelerationStructureData = scratch_resource->GetGPUVirtualAddress();
        top_level_build_desc.Inputs.InstanceDescs = instance_descs->GetGPUVirtualAddress();
    }

    auto build_acceleration_structure = [&](auto* raytracing_command_list) {
        raytracing_command_list->BuildRaytracingAccelerationStructure(&bottom_level_build_desc, 0, nullptr);
        auto const resource_barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_bottom_level_acceleration_structure.Get());
        command_list->ResourceBarrier(1, &resource_barrier);
        raytracing_command_list->BuildRaytracingAccelerationStructure(&top_level_build_desc, 0, nullptr);
    };

    // Build acceleration structure.
    build_acceleration_structure(m_dxr_command_list.Get());

    // Kick off acceleration structure construction.
    m_device_resources->execute_command_list();

    // Wait for GPU to finish as the locally created temporary GPU resources will get released once we go out of scope.
    m_device_resources->wait_for_gpu();
}

// Build shader tables.
// This encapsulates all shader records - shaders and the arguments for their local root signatures.
void Renderer::build_shader_tables()
{
    auto const device = m_device_resources->get_d3d_device();

    void* ray_gen_shader_identifier = nullptr;
    void* miss_shader_identifier = nullptr;
    void* hit_group_shader_identifier = nullptr;

    auto get_shader_identifiers = [&](auto* state_object_properties) {
        ray_gen_shader_identifier = state_object_properties->GetShaderIdentifier(raygen_shader_name);
        miss_shader_identifier = state_object_properties->GetShaderIdentifier(miss_shader_name);
        hit_group_shader_identifier = state_object_properties->GetShaderIdentifier(hit_group_name);
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

    // Ray gen shader table
    {
        u32 constexpr num_shader_records = 1;
        u32 const shader_record_size = shader_identifier_size;
        ShaderTable ray_gen_shader_table(device, num_shader_records, shader_record_size, L"RayGenShaderTable");
        ray_gen_shader_table.push_back(ShaderRecord(ray_gen_shader_identifier, shader_identifier_size));
        m_ray_gen_shader_table = ray_gen_shader_table.GetResource();
    }

    // Miss shader table
    {
        u32 constexpr num_shader_records = 1;
        u32 const shader_record_size = shader_identifier_size;
        ShaderTable miss_shader_table(device, num_shader_records, shader_record_size, L"MissShaderTable");
        miss_shader_table.push_back(ShaderRecord(miss_shader_identifier, shader_identifier_size));
        m_miss_shader_table = miss_shader_table.GetResource();
    }

    // Hit group shader table
    {
        struct RootArguments
        {
            CubeConstantBuffer cb;
        } root_arguments;

        root_arguments.cb = m_cube_cb;

        u32 constexpr num_shader_records = 1;
        u32 const shader_record_size = shader_identifier_size + sizeof(root_arguments);
        ShaderTable hit_group_shader_table(device, num_shader_records, shader_record_size, L"HitGroupShaderTable");
        hit_group_shader_table.push_back(
            ShaderRecord(hit_group_shader_identifier, shader_identifier_size, &root_arguments, sizeof(root_arguments)));
        m_hit_group_shader_table = hit_group_shader_table.GetResource();
    }
}

void Renderer::create_device_dependent_resources()
{
    // Initialize raytracing pipeline.

    // Create raytracing interfaces: raytracing device and command list.
    create_raytracing_interfaces();

    // Create root signatures for the shaders.
    create_root_signatures();

    // Create a raytracing pipeline state object which defines the binding of shaders, state and resources to be used during raytracing.
    create_raytracing_pipeline_state_object();

    // Create a heap for descriptors.
    create_descriptor_heap();

    // TODO: Sample specific: Build geometry to be used in the sample.
    build_geometry();

    // Build raytracing acceleration structures from the generated geometry.
    build_acceleration_structures();

    // Create constant buffers for the geometry and the scene.
    create_constant_buffers();

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

        std::array<CD3DX12_ROOT_PARAMETER, GlobalRootSignatureParams::Count> root_parameters = {};
        root_parameters[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable(1, &ranges[0]);
        root_parameters[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView(0);
        root_parameters[GlobalRootSignatureParams::SceneConstantSlot].InitAsConstantBufferView(0);
        root_parameters[GlobalRootSignatureParams::VertexBuffersSlot].InitAsDescriptorTable(1, &ranges[1]);

        CD3DX12_ROOT_SIGNATURE_DESC const global_root_signature_desc(root_parameters.size(), root_parameters.data());
        serialize_and_create_raytracing_root_signature(global_root_signature_desc, &m_raytracing_global_root_signature);
    }

    // Local Root Signature
    // This is a root signature that enables a shader to have unique arguments that come from shader tables.
    {
        std::array<CD3DX12_ROOT_PARAMETER, LocalRootSignatureParams::Count> root_parameters = {};
        root_parameters[LocalRootSignatureParams::CubeConstantSlot].InitAsConstants(SIZE_OF_IN_UINT32(m_cube_cb), 1);
        CD3DX12_ROOT_SIGNATURE_DESC local_root_signature_desc(root_parameters.size(), root_parameters.data());
        local_root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
        serialize_and_create_raytracing_root_signature(local_root_signature_desc, &m_raytracing_local_root_signature);
    }
}

// Create a raytracing pipeline state object (RTPSO).
// An RTPSO represents a full set of shaders reachable by a dispatch_rays() call,
// with all configuration options resolved, such as local signatures and other state.
void Renderer::create_raytracing_pipeline_state_object()
{
    // Create 7 subobjects that combine into a RTPSO:
    // Subobjects need to be associated with DXIL exports (i.e. shaders) either by way of default or explicit associations.
    // Default association applies to every exported shader entrypoint that doesn't have any of the same type of subobject associated with it.
    // This simple sample utilizes default shader association except for local root signature subobject
    // which has an explicit association specified purely for demonstration purposes.
    // 1 - DXIL library
    // 1 - Triangle hit group
    // 1 - Shader config
    // 2 - Local root signature and association
    // 1 - Global root signature
    // 1 - Pipeline config
    CD3DX12_STATE_OBJECT_DESC raytracing_pipeline = {D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};

    // DXIL library
    // This contains the shaders and their entrypoints for the state object.
    // Since shaders are not considered a subobject, they need to be passed in via DXIL library subobjects.
    auto const library = raytracing_pipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE library_dxil = CD3DX12_SHADER_BYTECODE((void*)g_pRaytracing, ARRAYSIZE(g_pRaytracing));
    library->SetDXILLibrary(&library_dxil);

    // Define which shader exports to surface from the library.
    // If no shader exports are defined for a DXIL library subobject, all shaders will be surface.
    // TODO: Sample specific: In this sample, this could be omitted for convenience since the sample uses all shaders in the library.
    {
        library->DefineExport(raygen_shader_name);
        library->DefineExport(closest_hit_shader_name);
        library->DefineExport(miss_shader_name);
    }

    // Triangle hit group
    // A hit group specifies closest hit, any hit and intersection shaders to be executed when a ray intersects
    // the geometry's triangle/AABB.
    // TODO: Sample specific: In this sample, we only use triangle geometry with a closest hit shader, so others are not set.
    auto const hit_group = raytracing_pipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hit_group->SetClosestHitShaderImport(closest_hit_shader_name);
    hit_group->SetHitGroupExport(hit_group_name);
    hit_group->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    // Shader config
    // Defines the maximum sizes in bytes for the ray payload and attribute structure.
    auto const shader_config = raytracing_pipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    u32 constexpr payload_size = sizeof(XMFLOAT4); // float4 color
    u32 constexpr attribute_size = sizeof(XMFLOAT2); // float2 barycentrics
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
    u32 constexpr max_recursion_depth = 1; // Primary rays only
    pipeline_config->Config(max_recursion_depth);

#if _DEBUG
    print_state_object_desc(raytracing_pipeline);
#endif

    HRESULT const hr = m_dxr_device->CreateStateObject(raytracing_pipeline, IID_PPV_ARGS(&m_dxr_state_object));
    assert(SUCCEEDED(hr));
}

// Local root signature and shader association
// This is a root signature that enables a shader to have unique arguments that come from shader tables.
void Renderer::create_local_root_signature_subobjects(CD3DX12_STATE_OBJECT_DESC* raytracing_pipeline) const
{
    // TODO: Sample specific: Hit group and miss shaders in this sample are not using a local root signature and thus one is not associated with them.

    // Local root signature to be used in a ray gen shader.
    {
        auto const local_root_signature = raytracing_pipeline->CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
        local_root_signature->SetRootSignature(m_raytracing_local_root_signature.Get());

        // Shader association
        auto const root_signature_association = raytracing_pipeline->CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
        root_signature_association->SetSubobjectToAssociate(*local_root_signature);
        root_signature_association->AddExport(hit_group_name);
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
    m_raytracing_global_root_signature.Reset();
    m_raytracing_local_root_signature.Reset();

    m_dxr_device.Reset();
    m_dxr_command_list.Reset();
    m_dxr_state_object.Reset();

    m_descriptor_heap.Reset();
    m_descriptors_allocated = 0;
    m_raytracing_output_resource_uav_descriptor_heap_index = UINT_MAX;
    m_index_buffer.resource.Reset();
    m_vertex_buffer.resource.Reset();
    m_per_frame_constants.Reset();
    m_ray_gen_shader_table.Reset();
    m_miss_shader_table.Reset();
    m_hit_group_shader_table.Reset();

    m_bottom_level_acceleration_structure.Reset();
    m_top_level_acceleration_structure.Reset();
}

void Renderer::release_window_size_dependent_resources()
{
    m_raytracing_output.Reset();
}
