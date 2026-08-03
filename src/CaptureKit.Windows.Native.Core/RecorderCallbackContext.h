#pragma once

namespace CaptureKit::Native
{
    /// <summary>
    /// Tracks execution inside a recorder callback on the current thread. Recorder
    /// control exports use this to reject synchronous reentrant calls that would
    /// otherwise attempt to join the callback thread or deadlock on the recorder lock.
    /// </summary>
    inline thread_local unsigned int g_recorderCallbackDepth = 0;

    class RecorderCallbackScope final
    {
    public:
        RecorderCallbackScope() noexcept
        {
            ++g_recorderCallbackDepth;
        }

        ~RecorderCallbackScope() noexcept
        {
            --g_recorderCallbackDepth;
        }

        RecorderCallbackScope(const RecorderCallbackScope&) = delete;
        RecorderCallbackScope& operator=(const RecorderCallbackScope&) = delete;

        static bool IsActive() noexcept
        {
            return g_recorderCallbackDepth != 0;
        }
    };
}
