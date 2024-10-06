#pragma once

#include "AK/Event.h"

#include <d3d12.h>

class Renderer;

class Window
{
public:
    Window(Renderer* renderer, u32 const width, u32 const height, std::wstring const& name);
    ~Window();

    [[nodiscard]] static Window* get_instance();
    static void set_instance(Window* window);

    void set_custom_window_text(LPCWSTR text) const;
    void set_window_size(u32 const width, u32 const height);

    [[nodiscard]] u32 get_width() const;
    [[nodiscard]] u32 get_height() const;
    [[nodiscard]] float get_aspect_ratio() const;
    [[nodiscard]] HWND get_hwnd() const;
    [[nodiscard]] bool is_fullscreen() const;

    AK::Event<void(u32, u32, bool)> on_size_changed;

    Renderer* renderer;

private:
    void update_for_size_change();

    static inline Window* m_instance;

    u32 m_width;
    u32 m_height;
    float m_aspect_ratio;

    bool m_is_fullscreen = false;

    // Window title.
    std::wstring m_title;

    HWND m_hwnd;
    WNDCLASSEXW m_window_class_information;
};
