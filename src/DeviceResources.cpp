#include "stdafx.h"

#include "DeviceResources.h"

#include <dxgidebug.h>

#include <cassert>
#include <iostream>

using Microsoft::WRL::ComPtr;

inline DXGI_FORMAT no_srgb(DXGI_FORMAT const format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    {
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    {
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    }
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    {
        return DXGI_FORMAT_B8G8R8X8_UNORM;
    }
    default:
    {
        return format;
    }
    }
}

DeviceResources::DeviceResources(DXGI_FORMAT back_buffer_format, DXGI_FORMAT depth_buffer_format, u32 back_buffer_count,
                                 D3D_FEATURE_LEVEL min_feature_level, u32 flags, u32 adapter_id_override)
    : m_adapter_id_override(adapter_id_override), m_adapter_id(U32_MAX), m_back_buffer_format(back_buffer_format),
      m_depth_buffer_format(depth_buffer_format), m_back_buffer_count(back_buffer_count), m_d3d_min_feature_level(min_feature_level),
      m_output_size({0, 0, 1, 1}), m_options(flags)
{
    if (back_buffer_count > max_back_buffer_count)
    {
        std::cerr << "BackBufferCount out of range.\n";
    }

    if (min_feature_level < D3D_FEATURE_LEVEL_11_0)
    {
        std::cerr << "MinFeatureLevel too low.\n";
    }

    if (m_options & require_tearing_support)
    {
        m_options |= allow_tearing;
    }
}

DeviceResources::~DeviceResources()
{
    // Ensure that the GPU is no longer referencing resources that are about to be destroyed.
    wait_for_gpu();
}

void DeviceResources::initialize_dxgi_adapter()
{
    bool debug_dxgi = false;

#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debug_controller = {};
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller))))
        {
            debug_controller->EnableDebugLayer();
        }
        else
        {
            std::cerr << "WARNING: Direct3D Debug Device is not available.\n";
        }

        ComPtr<IDXGIInfoQueue> dxgi_info_queue = {};
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_info_queue))))
        {
            debug_dxgi = true;

            HRESULT hr = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&m_dxgi_factory));
            assert(SUCCEEDED(hr));

            hr = dxgi_info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
            assert(SUCCEEDED(hr));

            hr = dxgi_info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
            assert(SUCCEEDED(hr));
        }
    }
#endif

    if (!debug_dxgi)
    {
        HRESULT const hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgi_factory));
        assert(SUCCEEDED(hr));
    }

    // Determines whether tearing support is available for fullscreen borderless windows.
    if (m_options & (allow_tearing | require_tearing_support))
    {
        BOOL allows_tearing = FALSE;

        ComPtr<IDXGIFactory5> factory5 = {};
        HRESULT hr = m_dxgi_factory.As(&factory5);

        if (SUCCEEDED(hr))
        {
            hr = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allows_tearing, sizeof(allows_tearing));
        }

        if (FAILED(hr) || !allows_tearing)
        {
            std::cerr << "WARNING: Variable refresh rate displays are not supported.\n";

            if (m_options & require_tearing_support)
            {
                std::cerr << "Error: Sample must be run on an OS with tearing support.\n";
                assert(false);
            }

            m_options &= ~allow_tearing;
        }
    }

    initialize_adapter(&m_adapter);
}

void DeviceResources::set_adapter_override(u32 const adapter_id)
{
    m_adapter_id_override = adapter_id;
}

// Configures the Direct3D device, and stores handles to it and the device context.
void DeviceResources::create_device_resources()
{
    // Create the DX12 API device object.
    HRESULT hr = D3D12CreateDevice(m_adapter.Get(), m_d3d_min_feature_level, IID_PPV_ARGS(&m_d3d_device));
    assert(SUCCEEDED(hr));

#ifndef NDEBUG
    // Configure debug device (if active).
    ComPtr<ID3D12InfoQueue> d3d_info_queue = {};

    if (SUCCEEDED(m_d3d_device.As(&d3d_info_queue)))
    {
#ifdef _DEBUG
        hr = d3d_info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        assert(SUCCEEDED(hr));

        hr = d3d_info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        assert(SUCCEEDED(hr));
#endif

        std::array hide = {D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE, D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE};
        D3D12_INFO_QUEUE_FILTER filter = {};
        filter.DenyList.NumIDs = hide.size();
        filter.DenyList.pIDList = hide.data();
        d3d_info_queue->AddStorageFilterEntries(&filter);
    }
#endif

    // Determine maximum supported feature level for this device.
    static constexpr std::array feature_levels = {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    D3D12_FEATURE_DATA_FEATURE_LEVELS data_feature_levels = {feature_levels.size(), feature_levels.data(), D3D_FEATURE_LEVEL_11_0};

    hr = m_d3d_device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &data_feature_levels, sizeof(data_feature_levels));

    if (SUCCEEDED(hr))
    {
        m_d3d_feature_level = data_feature_levels.MaxSupportedFeatureLevel;
    }
    else
    {
        m_d3d_feature_level = m_d3d_min_feature_level;
    }

    // Create the command queue.
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    hr = m_d3d_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&m_command_queue));
    assert(SUCCEEDED(hr));

    // Create descriptor heaps for render target views and depth stencil views.
    D3D12_DESCRIPTOR_HEAP_DESC rtv_descriptor_heap_desc = {};
    rtv_descriptor_heap_desc.NumDescriptors = m_back_buffer_count;
    rtv_descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

    hr = m_d3d_device->CreateDescriptorHeap(&rtv_descriptor_heap_desc, IID_PPV_ARGS(&m_rtv_descriptor_heap));
    assert(SUCCEEDED(hr));

    m_rtv_descriptor_size = m_d3d_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    if (m_depth_buffer_format != DXGI_FORMAT_UNKNOWN)
    {
        D3D12_DESCRIPTOR_HEAP_DESC dsv_descriptor_heap_desc = {};
        dsv_descriptor_heap_desc.NumDescriptors = 1;
        dsv_descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

        hr = m_d3d_device->CreateDescriptorHeap(&dsv_descriptor_heap_desc, IID_PPV_ARGS(&m_dsv_descriptor_heap));
        assert(SUCCEEDED(hr));
    }

    // Create a command allocator for each back buffer that will be rendered to.
    for (u32 n = 0; n < m_back_buffer_count; n++)
    {
        hr = m_d3d_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_command_allocators[n]));
        assert(SUCCEEDED(hr));
    }

    // Create a command list for recording graphics commands.
    hr = m_d3d_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_command_allocators[0].Get(), nullptr,
                                         IID_PPV_ARGS(&m_command_list));
    assert(SUCCEEDED(hr));

    hr = m_command_list->Close();
    assert(SUCCEEDED(hr));

    // Create a fence for tracking GPU execution progress.
    hr = m_d3d_device->CreateFence(m_fence_values[m_back_buffer_index], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    assert(SUCCEEDED(hr));

    m_fence_values[m_back_buffer_index] += 1;

    m_fence_event.Attach(CreateEvent(nullptr, FALSE, FALSE, nullptr));
    if (!m_fence_event.IsValid())
    {
        std::cerr << "Create event failed.\n";
        assert(false);
    }
}

// These resources need to be recreated every time the window size is changed.
void DeviceResources::create_window_size_dependent_resources()
{
    if (!m_window)
    {
        std::cerr << "Invalid window pointer.\n";
        assert(false);
    }

    wait_for_gpu();

    // Release resources that are tied to the swap chain and update fence values.
    for (u32 n = 0; n < m_back_buffer_count; n++)
    {
        m_render_targets[n].Reset();
        m_fence_values[n] = m_fence_values[m_back_buffer_index];
    }

    // Determine the render target size in pixels.
    u32 back_buffer_width = std::max(static_cast<u32>(m_output_size.right - m_output_size.left), static_cast<u32>(1));
    u32 back_buffer_height = std::max(static_cast<u32>(m_output_size.bottom - m_output_size.top), static_cast<u32>(1));
    DXGI_FORMAT back_buffer_format = no_srgb(m_back_buffer_format);

    // If the swap chain already exists, resize it, otherwise create one.
    if (m_swap_chain)
    {
        HRESULT hr = m_swap_chain->ResizeBuffers(m_back_buffer_count, back_buffer_width, back_buffer_height, back_buffer_format,
                                                 (m_options & allow_tearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);

        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
#ifdef _DEBUG
            char buffer[64] = {};
            sprintf_s(buffer, "Device Lost on ResizeBuffers: Reason code 0x%08X\n",
                      (hr == DXGI_ERROR_DEVICE_REMOVED) ? m_d3d_device->GetDeviceRemovedReason() : hr);
            OutputDebugStringA(buffer);
#endif

            // If the device was removed for any reason, a new device and swap chain need to be created.
            handle_device_lost();

            // Everything is set up now. Do not continue execution of this method. handle_device_lost will reenter this method
            // and correctly set up the new device.
            return;
        }

        assert(SUCCEEDED(hr));
    }
    else
    {
        // Create a descriptor for the swap chain.
        DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
        swap_chain_desc.Width = back_buffer_width;
        swap_chain_desc.Height = back_buffer_height;
        swap_chain_desc.Format = back_buffer_format;
        swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_desc.BufferCount = m_back_buffer_count;
        swap_chain_desc.SampleDesc.Count = 1;
        swap_chain_desc.SampleDesc.Quality = 0;
        swap_chain_desc.Scaling = DXGI_SCALING_STRETCH;
        swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        swap_chain_desc.Flags = (m_options & allow_tearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_swap_chain_desc = {};
        fullscreen_swap_chain_desc.Windowed = TRUE;

        // Create a swap chain for the window.
        ComPtr<IDXGISwapChain1> swap_chain = {};

        // DXGI does not allow creating a swap chain targeting a window which has fullscreen styles (no border + topmost).
        // Temporarily remove the topmost property for creating the swap chain.
        bool previous_is_fullscreen = false; // TODO: Win32Application::IsFullscreen();
        if (previous_is_fullscreen)
        {
            // TODO:
        }

        HRESULT hr = m_dxgi_factory->CreateSwapChainForHwnd(m_command_queue.Get(), m_window, &swap_chain_desc, &fullscreen_swap_chain_desc,
                                                            nullptr, &swap_chain);
        assert(SUCCEEDED(hr));

        if (previous_is_fullscreen)
        {
            // TODO: Win32Application::SetWindowZorderToTopMost(true);
        }

        hr = swap_chain.As(&m_swap_chain);
        assert(SUCCEEDED(hr));

        // With tearing support enabled we will handle ALT+Enter key presses in the window message loop rather than
        // let DXGI handle it by calling SetFullscreenState.
        if (is_tearing_supported())
        {
            hr = m_dxgi_factory->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER);
            assert(SUCCEEDED(hr));
        }
    }

    // Obtain the back buffers for this window which will be the final render targets
    // and create render target views for each of them.
    for (u32 n = 0; n < m_back_buffer_count; n++)
    {
        HRESULT hr = m_swap_chain->GetBuffer(n, IID_PPV_ARGS(&m_render_targets[n]));
        assert(SUCCEEDED(hr));

        wchar_t name[25] = {};
        swprintf_s(name, L"Render target %u", n);

        hr = m_render_targets[n]->SetName(name);
        assert(SUCCEEDED(hr));

        D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
        rtv_desc.Format = m_back_buffer_format;
        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        CD3DX12_CPU_DESCRIPTOR_HANDLE const rtv_descriptor(m_rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart(), n,
                                                           m_rtv_descriptor_size);
        m_d3d_device->CreateRenderTargetView(m_render_targets[n].Get(), &rtv_desc, rtv_descriptor);
    }

    // Reset the index to the current back buffer.
    m_back_buffer_index = m_swap_chain->GetCurrentBackBufferIndex();

    if (m_depth_buffer_format != DXGI_FORMAT_UNKNOWN)
    {
        // Allocate a 2D surface as the depth/stencil buffer and create a depth/stencil view on this surface.
        CD3DX12_HEAP_PROPERTIES depth_heap_properties(D3D12_HEAP_TYPE_DEFAULT);

        D3D12_RESOURCE_DESC depth_stencil_desc = CD3DX12_RESOURCE_DESC::Tex2D(m_depth_buffer_format, back_buffer_width, back_buffer_height,
                                                                              1, // This depth stencil view has only one texture.
                                                                              1 // Use a single mipmap level.
        );

        depth_stencil_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE depth_optimized_clear_value = {};
        depth_optimized_clear_value.Format = m_depth_buffer_format;
        depth_optimized_clear_value.DepthStencil.Depth = 1.0f;
        depth_optimized_clear_value.DepthStencil.Stencil = 0;

        HRESULT hr = m_d3d_device->CreateCommittedResource(&depth_heap_properties, D3D12_HEAP_FLAG_NONE, &depth_stencil_desc,
                                                           D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth_optimized_clear_value,
                                                           IID_PPV_ARGS(&m_depth_stencil));
        assert(SUCCEEDED(hr));

        hr = m_depth_stencil->SetName(L"Depth stencil");
        assert(SUCCEEDED(hr));

        D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
        dsv_desc.Format = m_depth_buffer_format;
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

        m_d3d_device->CreateDepthStencilView(m_depth_stencil.Get(), &dsv_desc, m_dsv_descriptor_heap->GetCPUDescriptorHandleForHeapStart());
    }

#pragma region Custom SRV descriptor heap creation code for ImGui
//    {
//        D3D12_DESCRIPTOR_HEAP_DESC srv_desc = {};
//        srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
//        srv_desc.NumDescriptors = 1;
//        srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
//        HRESULT const hr = m_d3d_device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&m_srv_descriptor_heap));
//        assert(SUCCEEDED(hr));
//    }
#pragma endregion

    // Set the 3D rendering viewport and scissor rectangle to target the entire window.
    m_screen_viewport.TopLeftX = 0.0f;
    m_screen_viewport.TopLeftY = 0.0f;
    m_screen_viewport.Width = static_cast<float>(back_buffer_width);
    m_screen_viewport.Height = static_cast<float>(back_buffer_height);
    m_screen_viewport.MinDepth = D3D12_MIN_DEPTH;
    m_screen_viewport.MaxDepth = D3D12_MAX_DEPTH;

    m_scissor_rect.left = 0;
    m_scissor_rect.top = 0;
    m_scissor_rect.right = static_cast<i32>(back_buffer_width);
    m_scissor_rect.bottom = static_cast<i32>(back_buffer_height);
}

// This method is called when the Win32 window is created (or re-created).
void DeviceResources::set_window(HWND const window, i32 const width, i32 const height)
{
    m_window = window;

    m_output_size.left = 0;
    m_output_size.top = 0;
    m_output_size.right = width;
    m_output_size.bottom = height;
}

// This method is called when the Win32 window changes size.
// It returns true if window size change was applied.
bool DeviceResources::window_size_changed(i32 const width, i32 const height, bool const minimized)
{
    m_is_window_visible = !minimized;

    if (minimized || width == 0 || height == 0)
    {
        return false;
    }

    RECT new_rc;
    new_rc.left = 0;
    new_rc.top = 0;
    new_rc.right = width;
    new_rc.bottom = height;

    if (new_rc.left == m_output_size.left && new_rc.top == m_output_size.top && new_rc.right == m_output_size.right
        && new_rc.bottom == m_output_size.bottom)
    {
        return false;
    }

    m_output_size = new_rc;
    create_window_size_dependent_resources();
    return true;
}

// Recreate all device resources and set them back to the current state.
void DeviceResources::handle_device_lost()
{
    if (m_device_notify)
    {
        m_device_notify->on_device_lost();
    }

    for (u32 n = 0; n < m_back_buffer_count; n++)
    {
        m_command_allocators[n].Reset();
        m_render_targets[n].Reset();
    }

    m_depth_stencil.Reset();
    m_command_queue.Reset();
    m_command_list.Reset();
    m_fence.Reset();
    m_rtv_descriptor_heap.Reset();
    m_dsv_descriptor_heap.Reset();
    m_swap_chain.Reset();
    m_d3d_device.Reset();
    m_dxgi_factory.Reset();
    m_adapter.Reset();

#ifdef _DEBUG
    {
        ComPtr<IDXGIDebug1> dxgi_debug = {};
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug))))
        {
            HRESULT const hr = dxgi_debug->ReportLiveObjects(
                DXGI_DEBUG_ALL, static_cast<DXGI_DEBUG_RLO_FLAGS>(DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
            assert(SUCCEEDED(hr));
        }
    }
#endif

    initialize_dxgi_adapter();
    create_device_resources();
    create_window_size_dependent_resources();

    if (m_device_notify)
    {
        m_device_notify->on_device_restored();
    }
}

void DeviceResources::register_device_notify(IDeviceNotify* device_notify)
{
    m_device_notify = device_notify;

    __if_exists(DXGIDeclareAdapterRemovalSupport)
    {
        if (device_notify)
        {
            if (FAILED(DXGIDeclareAdapterRemovalSupport()))
            {
                std::cerr << "Warning: application failed to declare adapter removal support.\n";
            }
        }
    }
}

// Prepare the command list and render target for rendering.
void DeviceResources::prepare(D3D12_RESOURCE_STATES const before_state) const
{
    // Reset command list and allocator.
    HRESULT hr = m_command_allocators[m_back_buffer_index]->Reset();
    assert(SUCCEEDED(hr));

    hr = m_command_list->Reset(m_command_allocators[m_back_buffer_index].Get(), nullptr);
    assert(SUCCEEDED(hr));

    if (before_state != D3D12_RESOURCE_STATE_RENDER_TARGET)
    {
        // Transition the render target into the correct state to allow for drawing into it.
        D3D12_RESOURCE_BARRIER const barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[m_back_buffer_index].Get(),
                                                                                    before_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_command_list->ResourceBarrier(1, &barrier);
    }
}

// Present the contents of the swap chain to the screen.
void DeviceResources::present(D3D12_RESOURCE_STATES const before_state)
{
    if (before_state != D3D12_RESOURCE_STATE_PRESENT)
    {
        // Transition the render target to the state that allows it to be presented to the display.
        D3D12_RESOURCE_BARRIER const barrier =
            CD3DX12_RESOURCE_BARRIER::Transition(m_render_targets[m_back_buffer_index].Get(), before_state, D3D12_RESOURCE_STATE_PRESENT);
        m_command_list->ResourceBarrier(1, &barrier);
    }

    execute_command_list();

    HRESULT hr;
    if (m_options & allow_tearing)
    {
        // Recommended to always use tearing if supported when using a sync interval of 0.
        // Note this will fail if in true 'fullscreen' mode.
        hr = m_swap_chain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
    }
    else
    {
        // The first argument instructs DXGI to block until VSync, putting the application
        // to sleep until the next VSync. This ensures we don't waste any cycles rendering
        // frames that will never be displayed to the screen.
        hr = m_swap_chain->Present(1, 0);
    }

    // If the device was reset we must completely reinitialize the renderer.
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
#ifdef _DEBUG
        char buffer[64] = {};
        sprintf_s(buffer, "Device Lost on Present: Reason code 0x%08X\n",
                  (hr == DXGI_ERROR_DEVICE_REMOVED) ? m_d3d_device->GetDeviceRemovedReason() : hr);
        OutputDebugStringA(buffer);
#endif

        handle_device_lost();
    }
    else
    {
        assert(SUCCEEDED(hr));

        move_to_next_frame();
    }
}

// Send the command list off to the GPU for processing.
void DeviceResources::execute_command_list() const
{
    HRESULT hr = m_command_list->Close();
    assert(SUCCEEDED(hr));

    std::array<ID3D12CommandList*, 1> const command_lists = {m_command_list.Get()};
    m_command_queue->ExecuteCommandLists(command_lists.size(), command_lists.data());
}

// Wait for pending GPU work to complete.
void DeviceResources::wait_for_gpu() noexcept
{
    if (m_command_queue && m_fence && m_fence_event.IsValid())
    {
        // Schedule a signal command in the GPU queue.
        u64 const fence_value = m_fence_values[m_back_buffer_index];
        if (SUCCEEDED(m_command_queue->Signal(m_fence.Get(), fence_value)))
        {
            // Wait until the signal has been processed.
            if (SUCCEEDED(m_fence->SetEventOnCompletion(fence_value, m_fence_event.Get())))
            {
                WaitForSingleObjectEx(m_fence_event.Get(), INFINITE, FALSE);

                // Increment the fence value for the current frame.
                m_fence_values[m_back_buffer_index] += 1;
            }
        }
    }
}

RECT DeviceResources::get_output_size() const
{
    return m_output_size;
}

bool DeviceResources::is_window_visible() const
{
    return m_is_window_visible;
}

bool DeviceResources::is_tearing_supported() const
{
    return m_options & allow_tearing;
}

IDXGIAdapter1* DeviceResources::get_adapter() const
{
    return m_adapter.Get();
}

ID3D12Device* DeviceResources::get_d3d_device() const
{
    return m_d3d_device.Get();
}

IDXGIFactory4* DeviceResources::get_dxgi_factory() const
{
    return m_dxgi_factory.Get();
}

IDXGISwapChain3* DeviceResources::get_swap_chain() const
{
    return m_swap_chain.Get();
}

D3D_FEATURE_LEVEL DeviceResources::get_device_feature_level() const
{
    return m_d3d_feature_level;
}

ID3D12Resource* DeviceResources::get_render_target() const
{
    return m_render_targets[m_back_buffer_index].Get();
}

ID3D12Resource* DeviceResources::get_depth_stencil() const
{
    return m_depth_stencil.Get();
}

ID3D12CommandQueue* DeviceResources::get_command_queue() const
{
    return m_command_queue.Get();
}

ID3D12CommandAllocator* DeviceResources::get_command_allocator() const
{
    return m_command_allocators[m_back_buffer_index].Get();
}

ID3D12GraphicsCommandList* DeviceResources::get_command_list() const
{
    return m_command_list.Get();
}

DXGI_FORMAT DeviceResources::get_back_buffer_format() const
{
    return m_back_buffer_format;
}

DXGI_FORMAT DeviceResources::get_depth_buffer_format() const
{
    return m_depth_buffer_format;
}

ID3D12DescriptorHeap* DeviceResources::get_back_buffer_descriptor_heap() const
{
    return m_rtv_descriptor_heap.Get();
}

ID3D12DescriptorHeap* DeviceResources::get_depth_buffer_descriptor_heap() const
{
    return m_dsv_descriptor_heap.Get();
}

ID3D12DescriptorHeap* DeviceResources::get_srv_descriptor_heap() const
{
    assert(false);
    return {};
    //return m_srv_descriptor_heap.Get();
}

D3D12_VIEWPORT DeviceResources::get_screen_viewport() const
{
    return m_screen_viewport;
}

D3D12_RECT DeviceResources::get_scissor_rect() const
{
    return m_scissor_rect;
}

u32 DeviceResources::get_current_frame_index() const
{
    return m_back_buffer_index;
}

u32 DeviceResources::get_previous_frame_index() const
{
    return m_back_buffer_index == 0 ? m_back_buffer_count - 1 : m_back_buffer_index - 1;
}

u32 DeviceResources::get_back_buffer_count() const
{
    return m_back_buffer_count;
}

u32 DeviceResources::get_device_options() const
{
    return m_options;
}

LPCWSTR DeviceResources::get_adapter_description() const
{
    return m_adapter_description.c_str();
}

u32 DeviceResources::get_adapter_id() const
{
    return m_adapter_id;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE DeviceResources::get_render_target_view() const
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart(), m_back_buffer_index,
                                         m_rtv_descriptor_size);
}

CD3DX12_CPU_DESCRIPTOR_HANDLE DeviceResources::get_depth_stencil_view() const
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_dsv_descriptor_heap->GetCPUDescriptorHandleForHeapStart());
}

DeviceResources::DeviceResources() = default;

// Prepare to render the next frame.
void DeviceResources::move_to_next_frame()
{
    // Schedule a signal command in the queue.
    u64 const current_fence_value = m_fence_values[m_back_buffer_index];
    HRESULT hr = m_command_queue->Signal(m_fence.Get(), current_fence_value);
    assert(SUCCEEDED(hr));

    // Update the back buffer index.
    m_back_buffer_index = m_swap_chain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if (m_fence->GetCompletedValue() < m_fence_values[m_back_buffer_index])
    {
        hr = m_fence->SetEventOnCompletion(m_fence_values[m_back_buffer_index], m_fence_event.Get());
        assert(SUCCEEDED(hr));

        WaitForSingleObjectEx(m_fence_event.Get(), INFINITE, FALSE);
    }

    // Set the fence value for the next frame.
    m_fence_values[m_back_buffer_index] = current_fence_value + 1;
}

// This method acquires the first high-performance hardware adapter that supports Direct3D 12.
// If no such adapter can be found, try WARP. Otherwise throw an exception.
void DeviceResources::initialize_adapter(IDXGIAdapter1** adapter)
{
    *adapter = nullptr;

    ComPtr<IDXGIAdapter1> searched_adapter = {};
    ComPtr<IDXGIFactory6> factory6 = {};

    HRESULT hr = m_dxgi_factory.As(&factory6);

    if (FAILED(hr))
    {
        std::cerr << "DXGI 1.6 not supported.\n";
        return;
    }

    for (u32 adapter_id = 0;
         DXGI_ERROR_NOT_FOUND
         != factory6->EnumAdapterByGpuPreference(adapter_id, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&searched_adapter));
         ++adapter_id)
    {
        if (m_adapter_id_override != UINT_MAX && adapter_id != m_adapter_id_override)
        {
            continue;
        }

        DXGI_ADAPTER_DESC1 desc = {};
        hr = searched_adapter->GetDesc1(&desc);
        assert(SUCCEEDED(hr));

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            // Don't select the Basic Render Driver adapter.
            continue;
        }

        // Check to see if the adapter supports Direct3D 12, but don't create the actual device yet.
        if (SUCCEEDED(D3D12CreateDevice(searched_adapter.Get(), m_d3d_min_feature_level, _uuidof(ID3D12Device), nullptr)))
        {
            m_adapter_id = adapter_id;
            m_adapter_description = desc.Description;

#ifdef _DEBUG
            wchar_t buffer[256] = {};
            swprintf_s(buffer, L"Direct3D Adapter (%u): VID:%04X, PID:%04X - %ls\n", adapter_id, desc.VendorId, desc.DeviceId,
                       desc.Description);
            OutputDebugStringW(buffer);
#endif

            break;
        }
    }

#if !defined(NDEBUG)
    if (!searched_adapter && m_adapter_id_override == UINT_MAX)
    {
        // Try WARP instead.
        if (FAILED(m_dxgi_factory->EnumWarpAdapter(IID_PPV_ARGS(&searched_adapter))))
        {
            std::cerr << "WARP not available. Enable the 'Graphics Tools' optional feature\n";
            return;
        }

        OutputDebugStringA("Direct3D Adapter - WARP\n");
    }
#endif

    if (!searched_adapter)
    {
        if (m_adapter_id_override != UINT_MAX)
        {
            std::cerr << "Unavailable adapter requested.\n";
            return;
        }

        std::cerr << "Unavailable adapter.\n";
        return;
    }

    *adapter = searched_adapter.Detach();
}
