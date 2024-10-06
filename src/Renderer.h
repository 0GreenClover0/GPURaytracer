#pragma once

#include "AK/Types.h"
#include "ConstantBuffers.h"
#include "DeviceResources.h"
#include "StepTimer.h"

#include <dxgi.h>
#include <memory>
#include <string>

class Window;

namespace GlobalRootSignatureParams
{

enum Value
{
    OutputViewSlot = 0,
    AccelerationStructureSlot,
    SceneConstantSlot,
    VertexBuffersSlot,
    Count
};

}

namespace LocalRootSignatureParams
{

enum Value
{
    CubeConstantSlot = 0,
    Count
};

}

class Renderer
    : public IDeviceNotify
    , public std::enable_shared_from_this<Renderer>
{
public:
    Renderer(u32 const width, u32 const height, std::wstring const& name);
    virtual ~Renderer();

    void on_init();
    void on_update();
    void on_render();
    void on_size_changed(u32 const width, u32 const height, bool const minimized);
    void on_destroy();

    [[nodiscard]] Window* get_window() const;
    [[nodiscard]] DeviceResources* get_device_resources() const;
    [[nodiscard]] static u32 get_frames_in_flight();

    virtual void on_device_lost() override;
    virtual void on_device_restored() override;

private:
    struct D3DBuffer
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor_handle;
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_descriptor_handle;
    };

    static_assert(sizeof(SceneConstantBuffer) < D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, "Checking the size here.");

    void initialize_scene();
    void update_camera_matrices();
    void create_constant_buffers();

    void calculate_frame_stats() const;
    void do_raytracing();
    void copy_raytracing_output_to_backbuffer() const;

    void build_geometry(); // TODO: Sample specific
    void build_acceleration_structures();
    void build_shader_tables();

    void create_device_dependent_resources();
    void create_raytracing_interfaces();
    void create_root_signatures();
    void create_raytracing_pipeline_state_object();
    void create_local_root_signature_subobjects(CD3DX12_STATE_OBJECT_DESC* raytracing_pipeline) const;
    void create_descriptor_heap();
    void create_raytracing_output_resource();
    void create_window_size_dependent_resources();
    void serialize_and_create_raytracing_root_signature(D3D12_ROOT_SIGNATURE_DESC const& desc,
                                                        Microsoft::WRL::ComPtr<ID3D12RootSignature>* root_signature) const;

    u32 allocate_descriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpu_descriptor, u32 descriptor_index_to_use = UINT_MAX);
    u32 create_buffer_srv(D3DBuffer* buffer, u32 num_elements, u32 element_size);

    void release_device_dependent_resources();
    void release_window_size_dependent_resources();

    static u32 constexpr frame_count = 3;

    union AlignedSceneConstantBuffer {
        SceneConstantBuffer constants;
        uint8_t alignment_padding[D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT];
    };

    AlignedSceneConstantBuffer* m_mapped_constant_data = {};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_per_frame_constants = {};

    // FIXME: Isn't u16 pretty low for an index?
    typedef u16 Index;

    // Application state
    StepTimer m_timer;
    XMVECTOR m_eye = {};
    XMVECTOR m_at = {};
    XMVECTOR m_up = {};

    // TODO: Sample specific
    SceneConstantBuffer m_scene_cb[frame_count];
    CubeConstantBuffer m_cube_cb;

    // Geometry
    D3DBuffer m_index_buffer = {};
    D3DBuffer m_vertex_buffer = {};

    // Acceleration structure
    Microsoft::WRL::ComPtr<ID3D12Resource> m_bottom_level_acceleration_structure = {};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_top_level_acceleration_structure = {};

    // Raytracing output
    Microsoft::WRL::ComPtr<ID3D12Resource> m_raytracing_output = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_raytracing_output_resource_uav_gpu_descriptor = {};
    u32 m_raytracing_output_resource_uav_descriptor_heap_index = UINT_MAX;

    // Shader tables
    static wchar_t const* hit_group_name;
    static wchar_t const* raygen_shader_name;
    static wchar_t const* closest_hit_shader_name;
    static wchar_t const* miss_shader_name;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_miss_shader_table = {};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_hit_group_shader_table = {};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_ray_gen_shader_table = {};

    u32 m_adapter_id_override = U32_MAX;

    std::unique_ptr<DeviceResources> m_device_resources = {};
    std::unique_ptr<Window> m_window = {};

    // DirectX Raytracing (DXR) attributes
    Microsoft::WRL::ComPtr<ID3D12Device5> m_dxr_device = {};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> m_dxr_command_list = {};
    Microsoft::WRL::ComPtr<ID3D12StateObject> m_dxr_state_object = {};

    // Root signatures
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_raytracing_global_root_signature = {};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_raytracing_local_root_signature = {};

    // Descriptors
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_descriptor_heap = {};
    u32 m_descriptors_allocated = 0;
    u32 m_descriptor_size = 0;
};
