#pragma once

#include "DeviceResources.h"

// Helper class for animation and simulation timing.
class StepTimer
{
public:
    StepTimer() : m_target_elapsed_ticks(ticks_per_second / 60)
    {
        QueryPerformanceFrequency(&m_qpc_qpc_frequency);
        QueryPerformanceCounter(&m_qpc_last_time);

        // Initialize max delta to 1/10 of a second.
        m_qpc_max_delta = m_qpc_qpc_frequency.QuadPart / 10;
    }

    // Get elapsed time since the previous Update call.
    [[nodiscard]] u64 get_elapsed_ticks() const
    {
        return m_elapsed_ticks;
    }

    [[nodiscard]] double get_elapsed_seconds() const
    {
        return TicksToSeconds(m_elapsed_ticks);
    }

    // Get total time since the start of the program.
    [[nodiscard]] u64 get_total_ticks() const
    {
        return m_total_ticks;
    }

    [[nodiscard]] double get_total_seconds() const
    {
        return TicksToSeconds(m_total_ticks);
    }

    // Get total number of updates since start of the program.
    [[nodiscard]] u32 get_frame_count() const
    {
        return m_frame_count;
    }

    // Get the current framerate.
    [[nodiscard]] u32 get_frames_per_second() const
    {
        return m_frames_per_second;
    }

    // Set whether to use fixed or variable timestep mode.
    void set_fixed_time_step(bool const is_fixed_timestep)
    {
        m_is_fixed_time_step = is_fixed_timestep;
    }

    // Set how often to call Update when in fixed timestep mode.
    void set_target_elapsed_ticks(u64 const target_elapsed)
    {
        m_target_elapsed_ticks = target_elapsed;
    }

    void set_target_elapsed_seconds(double const target_elapsed)
    {
        m_target_elapsed_ticks = seconds_to_ticks(target_elapsed);
    }

    // Integer format represents time using 10,000,000 ticks per second.
    static constexpr u64 ticks_per_second = 10000000;

    static double TicksToSeconds(u64 ticks)
    {
        return static_cast<double>(ticks) / ticks_per_second;
    }

    static u64 seconds_to_ticks(double const seconds)
    {
        return static_cast<u64>(seconds * ticks_per_second);
    }

    // After an intentional timing discontinuity (for instance a blocking IO operation)
    // call this to avoid having the fixed timestep logic attempt a set of catch-up
    // Update calls.

    void reset_elapsed_time()
    {
        QueryPerformanceCounter(&m_qpc_last_time);

        m_left_over_ticks = 0;
        m_frames_per_second = 0;
        m_frames_this_second = 0;
        m_qpc_second_counter = 0;
    }

    typedef void (*LPUPDATEFUNC)(void);

    // Update timer state, calling the specified Update function the appropriate number of times.
    void tick(LPUPDATEFUNC const update = nullptr)
    {
        // Query the current time.
        LARGE_INTEGER current_time;

        QueryPerformanceCounter(&current_time);

        u64 time_delta = current_time.QuadPart - m_qpc_last_time.QuadPart;

        m_qpc_last_time = current_time;
        m_qpc_second_counter += time_delta;

        // Clamp excessively large time deltas (e.g. after paused in the debugger).
        if (time_delta > m_qpc_max_delta)
        {
            time_delta = m_qpc_max_delta;
        }

        // Convert QPC units into a canonical tick format. This cannot overflow due to the previous clamp.
        time_delta *= ticks_per_second;
        time_delta /= m_qpc_qpc_frequency.QuadPart;

        u32 const last_frame_count = m_frame_count;

        if (m_is_fixed_time_step)
        {
            // Fixed timestep update logic

            // If the app is running very close to the target elapsed time (within 1/4 of a millisecond) just clamp
            // the clock to exactly match the target value. This prevents tiny and irrelevant errors
            // from accumulating over time. Without this clamping, a game that requested a 60 fps
            // fixed update, running with vsync enabled on a 59.94 NTSC display, would eventually
            // accumulate enough tiny errors that it would drop a frame. It is better to just round
            // small deviations down to zero to leave things running smoothly.

            if (abs(static_cast<int>(time_delta - m_target_elapsed_ticks)) < ticks_per_second / 4000)
            {
                time_delta = m_target_elapsed_ticks;
            }

            m_left_over_ticks += time_delta;

            while (m_left_over_ticks >= m_target_elapsed_ticks)
            {
                m_elapsed_ticks = m_target_elapsed_ticks;
                m_total_ticks += m_target_elapsed_ticks;
                m_left_over_ticks -= m_target_elapsed_ticks;
                m_frame_count++;

                if (update)
                {
                    update();
                }
            }
        }
        else
        {
            // Variable timestep update logic.
            m_elapsed_ticks = time_delta;
            m_total_ticks += time_delta;
            m_left_over_ticks = 0;
            m_frame_count++;

            if (update)
            {
                update();
            }
        }

        // Track the current framerate.
        if (m_frame_count != last_frame_count)
        {
            m_frames_this_second++;
        }

        if (m_qpc_second_counter >= static_cast<u64>(m_qpc_qpc_frequency.QuadPart))
        {
            m_frames_per_second = m_frames_this_second;
            m_frames_this_second = 0;
            m_qpc_second_counter %= m_qpc_qpc_frequency.QuadPart;
        }
    }

private:
    // Source timing data uses QPC units.
    LARGE_INTEGER m_qpc_qpc_frequency = {};
    LARGE_INTEGER m_qpc_last_time = {};
    u64 m_qpc_max_delta = 0;

    // Derived timing data uses a canonical tick format.
    u64 m_elapsed_ticks = 0;
    u64 m_total_ticks = 0;
    u64 m_left_over_ticks = 0;

    // Members for tracking the framerate.
    u32 m_frame_count = 0;
    u32 m_frames_per_second = 0;
    u32 m_frames_this_second = 0;
    u64 m_qpc_second_counter = 0;

    // Members for configuring fixed timestep mode.
    bool m_is_fixed_time_step = false;
    u64 m_target_elapsed_ticks = 0;
};
