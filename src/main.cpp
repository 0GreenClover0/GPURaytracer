#include "stdafx.h"

#pragma comment(lib, "dxguid.lib")

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

#include "Renderer.h"
#include "Window.h"

int main(int, char**)
{
    auto const renderer = std::make_shared<Renderer>(1280, 720, L"DirectX12 raytracer");
    renderer->on_init();

    // Setup Dear ImGui context
    //IMGUI_CHECKVERSION();
    //ImGui::CreateContext();
    //ImGuiIO& io = ImGui::GetIO();
    //(void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls

    // Setup Dear ImGui style
    //ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    //ImGui_ImplWin32_Init(renderer->get_window()->get_hwnd());

    //DeviceResources const* device_resources = renderer->get_device_resources();
    //ImGui_ImplDX12_Init(device_resources->get_d3d_device(), Renderer::get_frames_in_flight(), device_resources->get_back_buffer_format(),
    //                    device_resources->get_srv_descriptor_heap(),
    //                    device_resources->get_srv_descriptor_heap()->GetCPUDescriptorHandleForHeapStart(),
    //                    device_resources->get_srv_descriptor_heap()->GetGPUDescriptorHandleForHeapStart());

    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        // See the WndProc() function below for our to dispatch events to the Win32 backend.
        if (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }

        // Handle window screen locked
        //if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        //{
        //    ::Sleep(10);
        //    continue;
        //}
        //g_SwapChainOccluded = false;

        // Start the Dear ImGui frame
        //ImGui_ImplDX12_NewFrame();
        //ImGui_ImplWin32_NewFrame();
        //ImGui::NewFrame();

        //// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        //if (show_demo_window)
        //    ImGui::ShowDemoWindow(&show_demo_window);

        //// 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
        //{
        //    static float f = 0.0f;
        //    static int counter = 0;

        //    ImGui::Begin("Hello, world!"); // Create a window called "Hello, world!" and append into it.

        //    ImGui::Text("This is some useful text."); // Display some text (you can use a format strings too)
        //    ImGui::Checkbox("Demo Window", &show_demo_window); // Edit bools storing our window open/close state
        //    ImGui::Checkbox("Another Window", &show_another_window);

        //    ImGui::SliderFloat("float", &f, 0.0f, 1.0f); // Edit 1 float using a slider from 0.0f to 1.0f
        //    ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

        //    if (ImGui::Button("Button")) // Buttons return true when clicked (most widgets return true when edited/activated)
        //        counter++;
        //    ImGui::SameLine();
        //    ImGui::Text("counter = %d", counter);

        //    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        //    ImGui::End();
        //}

        //// 3. Show another simple window.
        //if (show_another_window)
        //{
        //    ImGui::Begin(
        //        "Another Window",
        //        &show_another_window); // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
        //    ImGui::Text("Hello from another window!");
        //    if (ImGui::Button("Close Me"))
        //        show_another_window = false;
        //    ImGui::End();
        //}

        //// Rendering
        //ImGui::Render();

        //renderer->on_render();

        //FrameContext* frameCtx = WaitForNextFrameResources();
        //UINT backBufferIdx = g_pSwapChain->GetCurrentBackBufferIndex();
        //frameCtx->CommandAllocator->Reset();

        //D3D12_RESOURCE_BARRIER barrier = {};
        //barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        //barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        //barrier.Transition.pResource = g_mainRenderTargetResource[backBufferIdx];
        //barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        //barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        //barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        //g_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);
        //g_pd3dCommandList->ResourceBarrier(1, &barrier);

        // Render Dear ImGui graphics
        //float const clear_color_with_alpha[4] = {clear_color.x * clear_color.w, clear_color.y * clear_color.w,
        //                                         clear_color.z * clear_color.w, clear_color.w};
        //g_pd3dCommandList->ClearRenderTargetView(g_mainRenderTargetDescriptor[backBufferIdx], clear_color_with_alpha, 0, nullptr);
        //g_pd3dCommandList->OMSetRenderTargets(1, &g_mainRenderTargetDescriptor[backBufferIdx], FALSE, nullptr);
        //g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);
        //ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList);
        //barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        //barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        //g_pd3dCommandList->ResourceBarrier(1, &barrier);
        //g_pd3dCommandList->Close();

        //g_pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_pd3dCommandList);

        //// Present
        //HRESULT hr = g_pSwapChain->Present(1, 0); // Present with vsync
        ////HRESULT hr = g_pSwapChain->Present(0, 0); // Present without vsync
        //g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);

        //UINT64 fenceValue = g_fenceLastSignaledValue + 1;
        //g_pd3dCommandQueue->Signal(g_fence, fenceValue);
        //g_fenceLastSignaledValue = fenceValue;
        //frameCtx->FenceValue = fenceValue;
    }

    renderer->on_destroy();

    // Cleanup
    //ImGui_ImplDX12_Shutdown();
    //ImGui_ImplWin32_Shutdown();
    //ImGui::DestroyContext();

    return 0;
}
