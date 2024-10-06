#pragma once

#include "AK/Types.h"

#include <d3d12.h>
#include <d3dcommon.h>
#include <d3dx12.h>
#include <dxgi.h>
#include <dxgi1_6.h>
#include <dxgiformat.h>

#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

#include <array>
#include <string>

// Provides an interface for an application that owns DeviceResources to be notified of the device being lost or created.
// clang-format off
interface IDeviceNotify
{
    virtual void on_device_lost() = 0;
    virtual void on_device_restored() = 0;
};
// clang-format on

class DeviceResources
{
public:
    static constexpr u32 allow_tearing = 0x1;
    static constexpr u32 require_tearing_support = 0x2;

    explicit DeviceResources(DXGI_FORMAT back_buffer_format = DXGI_FORMAT_B8G8R8A8_UNORM,
                             DXGI_FORMAT depth_buffer_format = DXGI_FORMAT_D32_FLOAT, u32 back_buffer_count = 2,
                             D3D_FEATURE_LEVEL min_feature_level = D3D_FEATURE_LEVEL_11_0, u32 flags = 0,
                             u32 adapter_id_override = UINT_MAX);
    ~DeviceResources();

    void initialize_dxgi_adapter();
    void set_adapter_override(u32 const adapter_id);
    void create_device_resources();
    void create_window_size_dependent_resources();
    void set_window(HWND const window, i32 const width, i32 const height);
    bool window_size_changed(i32 const width, i32 const height, bool const minimized);
    void handle_device_lost();
    void register_device_notify(IDeviceNotify* device_notify);

    void prepare(D3D12_RESOURCE_STATES const before_state = D3D12_RESOURCE_STATE_PRESENT) const;
    void present(D3D12_RESOURCE_STATES const before_state = D3D12_RESOURCE_STATE_RENDER_TARGET);
    void execute_command_list() const;
    void wait_for_gpu() noexcept;

    // Device accessors.
    [[nodiscard]] RECT get_output_size() const;
    [[nodiscard]] bool is_window_visible() const;
    [[nodiscard]] bool is_tearing_supported() const;

    // Direct3D accessors.
    [[nodiscard]] IDXGIAdapter1* get_adapter() const;
    [[nodiscard]] ID3D12Device* get_d3d_device() const;
    [[nodiscard]] IDXGIFactory4* get_dxgi_factory() const;
    [[nodiscard]] IDXGISwapChain3* get_swap_chain() const;
    [[nodiscard]] D3D_FEATURE_LEVEL get_device_feature_level() const;
    [[nodiscard]] ID3D12Resource* get_render_target() const;
    [[nodiscard]] ID3D12Resource* get_depth_stencil() const;
    [[nodiscard]] ID3D12CommandQueue* get_command_queue() const;
    [[nodiscard]] ID3D12CommandAllocator* get_command_allocator() const;
    [[nodiscard]] ID3D12GraphicsCommandList* get_command_list() const;
    [[nodiscard]] DXGI_FORMAT get_back_buffer_format() const;
    [[nodiscard]] DXGI_FORMAT get_depth_buffer_format() const;
    [[nodiscard]] ID3D12DescriptorHeap* get_back_buffer_descriptor_heap() const;
    [[nodiscard]] ID3D12DescriptorHeap* get_depth_buffer_descriptor_heap() const;
    [[nodiscard]] ID3D12DescriptorHeap* get_srv_descriptor_heap() const;
    [[nodiscard]] D3D12_VIEWPORT get_screen_viewport() const;
    [[nodiscard]] D3D12_RECT get_scissor_rect() const;
    [[nodiscard]] u32 get_current_frame_index() const;
    [[nodiscard]] u32 get_previous_frame_index() const;
    [[nodiscard]] u32 get_back_buffer_count() const;
    [[nodiscard]] u32 get_device_options() const;
    [[nodiscard]] LPCWSTR get_adapter_description() const;
    [[nodiscard]] u32 get_adapter_id() const;

    CD3DX12_CPU_DESCRIPTOR_HANDLE get_render_target_view() const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE get_depth_stencil_view() const;

private:
    DeviceResources();

    void move_to_next_frame();
    void initialize_adapter(IDXGIAdapter1** adapter);

    static constexpr size_t max_back_buffer_count = 3;

    u32 m_adapter_id_override = 0;
    u32 m_back_buffer_index = 0;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> m_adapter = {};
    u32 m_adapter_id = 0;
    std::wstring m_adapter_description;

    // Direct3D objects.
    Microsoft::WRL::ComPtr<ID3D12Device> m_d3d_device = {};
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_command_queue = {};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_command_list = {};
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, max_back_buffer_count> m_command_allocators = {};

    // Swap chain objects.
    Microsoft::WRL::ComPtr<IDXGIFactory4> m_dxgi_factory = {};
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swap_chain = {};
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, max_back_buffer_count> m_render_targets = {};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_depth_stencil = {};

    // Presentation fence objects.
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence = {};
    std::array<u64, max_back_buffer_count> m_fence_values = {};
    Microsoft::WRL::Wrappers::Event m_fence_event;

    // Direct3D rendering objects.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtv_descriptor_heap = {};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsv_descriptor_heap = {};
    //Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srv_descriptor_heap = {};

    u32 m_rtv_descriptor_size = 0;
    D3D12_VIEWPORT m_screen_viewport = {};
    D3D12_RECT m_scissor_rect = {};

    // Direct3D properties.
    DXGI_FORMAT m_back_buffer_format = {};
    DXGI_FORMAT m_depth_buffer_format = {};
    u32 m_back_buffer_count = 0;
    D3D_FEATURE_LEVEL m_d3d_min_feature_level = {};

    // Cached device properties.
    HWND m_window = {};
    D3D_FEATURE_LEVEL m_d3d_feature_level = D3D_FEATURE_LEVEL_11_0;
    RECT m_output_size = {};
    bool m_is_window_visible = true;

    // DeviceResources options (see flags above).
    u32 m_options = 0;

    // The IDeviceNotify can be held directly as it owns the DeviceResources.
    IDeviceNotify* m_device_notify = nullptr;
};
