#include "Window.h"

#include "Renderer.h"

#include "imgui_impl_win32.h"

#include <cassert>
#include <dxgi.h>

LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param);

Window::Window(Renderer* renderer, u32 const width, u32 const height, std::wstring const& name)
    : renderer(renderer), m_width(width), m_height(height), m_title(name)
{
    update_for_size_change();

    m_window_class_information = {};
    m_window_class_information.cbSize = sizeof(WNDCLASSEXW);
    m_window_class_information.style = CS_HREDRAW | CS_VREDRAW;
    m_window_class_information.lpfnWndProc = wnd_proc;
    m_window_class_information.hInstance = GetModuleHandle(nullptr);
    m_window_class_information.hCursor = LoadCursor(nullptr, IDC_ARROW);
    m_window_class_information.lpszClassName = L"WindowClass";

    RegisterClassExW(&m_window_class_information);

    RECT window_rect = {0, 0, static_cast<i32>(width), static_cast<i32>(height)};
    AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = ::CreateWindowW(m_window_class_information.lpszClassName, name.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             window_rect.right - window_rect.left, window_rect.bottom - window_rect.top, nullptr, nullptr,
                             m_window_class_information.hInstance, renderer);
}

Window::~Window()
{
    UnregisterClassW(m_window_class_information.lpszClassName, m_window_class_information.hInstance);
}

Window* Window::get_instance()
{
    return m_instance;
}

void Window::set_instance(Window* window)
{
    m_instance = window;
}

// Helper function for setting the window's title text.
void Window::set_custom_window_text(LPCWSTR const text) const
{
    std::wstring const window_text = m_title + L": " + text;
    SetWindowTextW(m_hwnd, window_text.c_str());
}

void Window::set_window_size(u32 const width, u32 const height)
{
    m_width = width;
    m_height = height;
    update_for_size_change();
}

void Window::update_for_size_change()
{
    m_aspect_ratio = static_cast<float>(m_width) / static_cast<float>(m_height);
}

u32 Window::get_width() const
{
    return m_width;
}

u32 Window::get_height() const
{
    return m_height;
}

float Window::get_aspect_ratio() const
{
    return m_aspect_ratio;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, w_param, l_param))
        return true;

    switch (msg)
    {
    case WM_SIZE:
    {
        auto const window = Window::get_instance();

        if (window == nullptr)
        {
            return 0;
        }

        window->on_size_changed(static_cast<u32>(LOWORD(l_param)), static_cast<u32>(HIWORD(l_param)), w_param == SIZE_MINIMIZED);
        return 0;
    }
    case WM_PAINT:
    {
        auto const window = Window::get_instance();

        if (window == nullptr)
        {
            return 0;
        }

        window->renderer->on_update();
        window->renderer->on_render();
        return 0;
    }
    case WM_SYSCOMMAND:
    {
        if ((w_param & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    }
    case WM_DESTROY:
    {
        PostQuitMessage(0);
        return 0;
    }
    default:
    {
        break;
    }
    }
    return ::DefWindowProcW(hwnd, msg, w_param, l_param);
}

HWND Window::get_hwnd() const
{
    return m_hwnd;
}

bool Window::is_fullscreen() const
{
    return m_is_fullscreen;
}
