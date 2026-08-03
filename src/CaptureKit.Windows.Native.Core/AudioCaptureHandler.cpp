#include "pch.h"
#include "AudioCaptureHandler.h"
#include "IAudioCaptureSource.h"
#include "IMediaClockWriter.h"
#include "IMediaClockReader.h"
#include "MediaTimeConstants.h"
#include "NativeExceptionBoundary.h"

#include <mmreg.h>
#include <span>
#include <strsafe.h>
#include <Audioclient.h>
#include <Windows.h>
#include <ksmedia.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>

namespace
{
    class AudioBufferReleaseGuard final
    {
    public:
        AudioBufferReleaseGuard(AudioCaptureDevice& device, UINT32 frameCount) noexcept
            : m_device(device)
            , m_frameCount(frameCount)
        {
        }

        ~AudioBufferReleaseGuard() noexcept
        {
            if (m_frameCount > 0)
            {
                m_device.ReleaseBuffer(m_frameCount);
            }
        }

        AudioBufferReleaseGuard(const AudioBufferReleaseGuard&) = delete;
        AudioBufferReleaseGuard& operator=(const AudioBufferReleaseGuard&) = delete;

    private:
        AudioCaptureDevice& m_device;
        UINT32 m_frameCount;
    };

    bool IsExtensibleSubFormat(const WAVEFORMATEX* format, const GUID& subFormat)
    {
        if (!format ||
            format->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
            format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        {
            return false;
        }

        auto extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return IsEqualGUID(extensible->SubFormat, subFormat);
    }

    bool IsPcmFormat(const WAVEFORMATEX* format)
    {
        return format &&
            (format->wFormatTag == WAVE_FORMAT_PCM || IsExtensibleSubFormat(format, KSDATAFORMAT_SUBTYPE_PCM));
    }

    bool IsFloatFormat(const WAVEFORMATEX* format)
    {
        return format &&
            (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT || IsExtensibleSubFormat(format, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT));
    }
}

AudioCaptureHandler::AudioCaptureHandler(IMediaClockReader* clockReader)
    : m_clockReader(clockReader)
{
    QueryPerformanceFrequency(&m_qpcFrequency);
    // Principle #6 (No Globals): Clock reader passed via constructor
}

AudioCaptureHandler::~AudioCaptureHandler()
{
    Stop();
    // Principle #5 (RAII Everything): Destructor ensures cleanup via following chain:
    // 1. Stop() joins capture thread and releases audio device
    // 2. m_device destructor releases WASAPI COM objects via wil::com_ptr
    // 3. m_silentBuffer memory is automatically freed via std::vector destructor
    // No manual delete/free calls needed - type system guarantees cleanup.
}

// ============================================================================
// Initialization and Lifecycle
// ============================================================================

bool AudioCaptureHandler::Initialize(bool loopback, const wchar_t* deviceId, HRESULT* outHr)
{
    // Only one initialization attempt may own/reset the lifecycle state at a
    // time. Stop remains independently callable so it can cancel this wait.
    std::lock_guard<std::mutex> initializeCallLock(m_initializeCallMutex);
    Stop();

    m_sampleRate = 0;
    {
        std::lock_guard<std::mutex> lock(m_silentBufferMutex);
        m_silentBuffer.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_loopback = loopback;
        m_deviceId = deviceId ? deviceId : L"";
        m_initializeCompleted = false;
        m_initializeSucceeded = false;
        m_startRequested = false;
        m_startCompleted = false;
        m_startSucceeded = false;
        m_shutdownRequested = false;
        m_threadCreationInProgress = true;
        m_initializeResult = E_UNEXPECTED;
        m_startResult = E_UNEXPECTED;
    }

    try
    {
        std::lock_guard<std::mutex> captureThreadLock(m_captureThreadMutex);
        m_captureThread = std::thread(&AudioCaptureHandler::CaptureThreadProc, this);

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_threadCreationInProgress = false;
        }
        m_stateChanged.notify_all();
    }
    catch (...)
    {
        const HRESULT hr = CaptureKit::Native::HResultFromCurrentException();
        CaptureKit::Native::ReportBoundaryException(L"AudioCaptureHandler::Initialize", hr);

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_initializeCompleted = true;
            m_initializeSucceeded = false;
            m_initializeResult = hr;
            m_threadCreationInProgress = false;
        }
        m_stateChanged.notify_all();

        if (outHr) *outHr = hr;
        return false;
    }

    const bool initialized = WaitForInitialization(outHr);
    if (!initialized)
    {
        JoinCaptureThread();
    }

    return initialized;
}

bool AudioCaptureHandler::WaitForInitialization(HRESULT* outHr)
{
    std::unique_lock<std::mutex> lock(m_stateMutex);
    m_stateChanged.wait(lock, [this] {
        return m_initializeCompleted || m_shutdownRequested;
    });

    const bool initialized =
        m_initializeCompleted &&
        m_initializeSucceeded &&
        !m_shutdownRequested;
    HRESULT hr = m_initializeCompleted ? m_initializeResult : E_ABORT;
    if (m_shutdownRequested && m_initializeSucceeded)
    {
        hr = E_ABORT;
    }

    if (outHr) *outHr = hr;
    return initialized;
}

void AudioCaptureHandler::JoinCaptureThread()
{
    // More than one caller may observe shutdown concurrently. Serialize the
    // join so only one of them ever operates on the std::thread object and all
    // callers return after an in-progress join has completed.
    std::lock_guard<std::mutex> captureThreadLock(m_captureThreadMutex);
    if (m_captureThread.joinable())
    {
        m_captureThread.join();
    }
}

bool AudioCaptureHandler::SetInputDevice(bool loopback, const wchar_t* deviceId, HRESULT* outHr)
{
    bool wasRunning = m_isRunning.load();
    bool wasEnabled = m_isEnabled.load();

    Stop();
    m_sampleRate = 0;
    m_silentBuffer.clear();

    if (!Initialize(loopback, deviceId, outHr))
    {
        return false;
    }

    m_isEnabled = wasEnabled;
    if (wasRunning && !Start(outHr))
    {
        return false;
    }

    if (outHr) *outHr = S_OK;
    return true;
}

bool AudioCaptureHandler::Start(HRESULT* outHr)
{
    if (m_isRunning)
    {
        if (outHr) *outHr = E_NOT_VALID_STATE;
        return false;
    }

    std::unique_lock<std::mutex> lock(m_stateMutex);
    if (!m_initializeSucceeded || m_shutdownRequested)
    {
        if (outHr) *outHr = E_NOT_VALID_STATE;
        return false;
    }

    if (m_startRequested)
    {
        if (outHr) *outHr = E_NOT_VALID_STATE;
        return false;
    }

    m_startRequested = true;
    m_stateChanged.notify_all();
    m_stateChanged.wait(lock, [this] { return m_startCompleted || m_shutdownRequested; });

    const bool started = m_startCompleted && m_startSucceeded;
    const HRESULT hr = m_startCompleted ? m_startResult : E_ABORT;

    if (outHr) *outHr = hr;
    return started;
}

void AudioCaptureHandler::Stop()
{
    m_isRunning = false;

    {
        std::unique_lock<std::mutex> lock(m_stateMutex);
        m_shutdownRequested = true;

        // Publish terminal results before notifying. These flags stay latched
        // until the next Initialize call resets the state, so a waiter cannot
        // miss the transition if Stop reaches the join first.
        if (!m_initializeCompleted)
        {
            m_initializeCompleted = true;
            m_initializeSucceeded = false;
            m_initializeResult = E_ABORT;
        }
        if (m_startRequested && !m_startCompleted)
        {
            m_startCompleted = true;
            m_startSucceeded = false;
            m_startResult = E_ABORT;
        }

        m_stateChanged.notify_all();
        m_stateChanged.wait(lock, [this] {
            return !m_threadCreationInProgress;
        });
    }

    JoinCaptureThread();
}

// ============================================================================
// Audio Format Access
// ============================================================================

WAVEFORMATEX* AudioCaptureHandler::GetFormat() const
{
    return m_device.GetFormat();
}

// ============================================================================
// Thread-Safe Silent Buffer Management
// ============================================================================

BYTE* AudioCaptureHandler::GetSilentBuffer(UINT32 requiredSize)
{
    static constexpr size_t BUFFER_GROWTH_FACTOR = 2;
    
    std::lock_guard<std::mutex> lock(m_silentBufferMutex);
    
    if (m_silentBuffer.size() < requiredSize)
    {
        // Resize with growth factor to reduce future reallocations
        m_silentBuffer.resize(requiredSize * BUFFER_GROWTH_FACTOR, 0);
    }
    else
    {
        // Just zero the needed portion
        memset(m_silentBuffer.data(), 0, requiredSize);
    }
    
    return m_silentBuffer.data();
}

void AudioCaptureHandler::SetVolume(uint32_t volumePercentage)
{
    m_volumePercentage = std::min<uint32_t>(volumePercentage, 100);
}

BYTE* AudioCaptureHandler::GetVolumeAdjustedBuffer(const BYTE* sourceData, UINT32 bufferSize, WAVEFORMATEX* format)
{
    uint32_t volumePercentage = m_volumePercentage.load();
    if (!sourceData || !format || volumePercentage >= 100)
    {
        return const_cast<BYTE*>(sourceData);
    }

    if (volumePercentage == 0)
    {
        return GetSilentBuffer(bufferSize);
    }

    std::lock_guard<std::mutex> lock(m_volumeBufferMutex);
    m_volumeBuffer.resize(bufferSize);
    memcpy(m_volumeBuffer.data(), sourceData, bufferSize);

    double gain = static_cast<double>(volumePercentage) / 100.0;
    if (IsFloatFormat(format) && format->wBitsPerSample == 32)
    {
        auto* samples = reinterpret_cast<float*>(m_volumeBuffer.data());
        size_t sampleCount = bufferSize / sizeof(float);
        for (size_t i = 0; i < sampleCount; i++)
        {
            samples[i] = static_cast<float>(samples[i] * gain);
        }

        return m_volumeBuffer.data();
    }

    if (IsPcmFormat(format) && format->wBitsPerSample == 16)
    {
        auto* samples = reinterpret_cast<int16_t*>(m_volumeBuffer.data());
        size_t sampleCount = bufferSize / sizeof(int16_t);
        for (size_t i = 0; i < sampleCount; i++)
        {
            double scaled = static_cast<double>(samples[i]) * gain;
            samples[i] = static_cast<int16_t>(std::clamp(
                std::lround(scaled),
                static_cast<long>(INT16_MIN),
                static_cast<long>(INT16_MAX)));
        }

        return m_volumeBuffer.data();
    }

    return const_cast<BYTE*>(sourceData);
}

// ============================================================================
// Audio Capture Thread
// ============================================================================

void AudioCaptureHandler::CaptureThreadProc() noexcept
{
    HRESULT hr = E_UNEXPECTED;
    try
    {
        bool loopback;
        std::wstring deviceId;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            loopback = m_loopback;
            deviceId = m_deviceId;
        }

        bool initialized = m_device.Initialize(
            loopback,
            deviceId.empty() ? nullptr : deviceId.c_str(),
            &hr);
        WAVEFORMATEX* format = initialized ? m_device.GetFormat() : nullptr;
        if (initialized && (!format || format->nSamplesPerSec == 0 || format->nBlockAlign == 0))
        {
            initialized = false;
            hr = E_UNEXPECTED;
        }

        if (initialized)
        {
            m_sampleRate = format->nSamplesPerSec;
            const UINT32 maxBufferSize = m_sampleRate * format->nBlockAlign;
            (void)GetSilentBuffer(maxBufferSize);
            hr = S_OK;
        }

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            if (m_shutdownRequested)
            {
                m_initializeSucceeded = false;
                m_initializeResult = E_ABORT;
            }
            else
            {
                m_initializeSucceeded = initialized;
                m_initializeResult = hr;
            }
            m_initializeCompleted = true;
        }
        m_stateChanged.notify_all();

        if (!initialized)
        {
            m_device.Shutdown();
            return;
        }

        {
            std::unique_lock<std::mutex> lock(m_stateMutex);
            m_stateChanged.wait(lock, [this] {
                return m_startRequested || m_shutdownRequested;
            });
            if (m_shutdownRequested)
            {
                lock.unlock();
                m_device.Shutdown();
                return;
            }
        }

        bool started = false;
        if (!m_clockWriter || m_sampleRate == 0)
        {
            hr = E_UNEXPECTED;
        }
        else
        {
            started = m_device.Start(&hr);
        }

        bool shouldRun;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            shouldRun = started && !m_shutdownRequested;
            m_startSucceeded = shouldRun;
            m_startResult = shouldRun ? S_OK : (FAILED(hr) ? hr : E_ABORT);
            m_startCompleted = true;
            m_isRunning = shouldRun;
        }
        m_stateChanged.notify_all();

        if (!shouldRun)
        {
            m_device.Shutdown();
            return;
        }

        RunCaptureLoop();
    }
    catch (...)
    {
        hr = CaptureKit::Native::HResultFromCurrentException();
        CaptureKit::Native::ReportBoundaryException(L"AudioCaptureHandler::CaptureThreadProc", hr);

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_shutdownRequested = true;
            if (!m_initializeCompleted)
            {
                m_initializeCompleted = true;
                m_initializeSucceeded = false;
                m_initializeResult = hr;
            }
            if (m_startRequested && !m_startCompleted)
            {
                m_startCompleted = true;
                m_startSucceeded = false;
                m_startResult = hr;
            }
        }
        m_stateChanged.notify_all();
    }

    m_isRunning = false;
    m_device.Shutdown();
}

void AudioCaptureHandler::RunCaptureLoop() noexcept
{
    try
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    
    LONGLONG lastTimestamp = 0;
    constexpr UINT32 SLEEP_DURATION_MS = 10;
    const UINT32 VIRTUAL_FRAMES_PER_SLEEP = (m_sampleRate * SLEEP_DURATION_MS) / 1000;
    
    // Track last clock advancement time for more accurate timing
    LARGE_INTEGER qpcFreq, lastAdvanceQpc, currentQpc;
    QueryPerformanceFrequency(&qpcFreq);
    QueryPerformanceCounter(&lastAdvanceQpc);
    
        while (m_isRunning)
        {
        BYTE* pData = nullptr;
        UINT32 numFramesAvailable = 0;
        DWORD flags = 0;
        UINT64 devicePosition = 0;
        UINT64 qpcPosition = 0;

        UINT32 framesRead = m_device.ReadSamples(
            &pData,
            &numFramesAvailable,
            &flags,
            &devicePosition,
            &qpcPosition
        );

        if (framesRead > 0)
        {
            AudioBufferReleaseGuard releaseBuffer(m_device, framesRead);

            if (m_clockReader && m_clockReader->IsPaused())
            {
                // Keep draining WASAPI while paused, but do not write samples at
                // a frozen timestamp or build a backlog for resume.
                QueryPerformanceCounter(&lastAdvanceQpc);
                Sleep(1);
                continue;
            }

            WAVEFORMATEX* format = m_device.GetFormat();
            if (!format)
            {
                continue;
            }
            
            LONGLONG duration = MediaTimeConstants::TicksFromAudioFrames(framesRead, m_sampleRate);
            
            // Check if callback is still valid and we're still running before invoking
            if (m_audioSampleReadyCallback && m_isRunning)
            {
                LONGLONG timestamp = 0;
                if (m_clockReader && m_clockReader->IsRunning())
                {
                    timestamp = m_clockReader->GetCurrentTime();
                }
                else
                {
                    timestamp = lastTimestamp + duration;
                }
                
                if (m_wasDisabled)
                {
                    m_wasDisabled = false;
                    m_samplesToSkip = 5;
                }
                
                if (m_samplesToSkip > 0)
                {
                    m_samplesToSkip--;
                    
                    // Still advance clock even when skipping samples
                    m_clockWriter->AdvanceByAudioSamples(framesRead, m_sampleRate);
                    QueryPerformanceCounter(&lastAdvanceQpc);
                    continue;
                }
                
                BYTE* pAudioData = pData;
                UINT32 bufferSize = framesRead * format->nBlockAlign;
                if (!m_isEnabled || (flags & AUDCLNT_BUFFERFLAGS_SILENT))
                {
                    pAudioData = GetSilentBuffer(bufferSize);
                }
                else
                {
                    pAudioData = GetVolumeAdjustedBuffer(pAudioData, bufferSize, format);
                }
                
                AudioSampleReadyEventArgs args{};
                args.data = std::span<const uint8_t>(pAudioData, bufferSize);
                args.timestamp = timestamp;
                args.pFormat = format;
                
                m_audioSampleReadyCallback(args);
                
                lastTimestamp = timestamp;
            }

            // Always advance the clock when frames are processed
            m_clockWriter->AdvanceByAudioSamples(framesRead, m_sampleRate);
            QueryPerformanceCounter(&lastAdvanceQpc);
        }
        else
        {
            // No audio data available from WASAPI
            // This happens during silence - generate silent audio to maintain A/V sync
            if (m_clockReader && m_clockReader->IsPaused())
            {
                QueryPerformanceCounter(&lastAdvanceQpc);
                Sleep(1);
                continue;
            }

            QueryPerformanceCounter(&currentQpc);
            LONGLONG qpcElapsed = currentQpc.QuadPart - lastAdvanceQpc.QuadPart;
            LONGLONG ticksElapsed = (qpcElapsed * MediaTimeConstants::TicksPerSecond()) / qpcFreq.QuadPart;
            
            // If more than 10ms has elapsed since last advancement, generate silent audio
            constexpr LONGLONG TEN_MS_TICKS = MediaTimeConstants::TicksFromMilliseconds(10);
            if (ticksElapsed >= TEN_MS_TICKS)
            {
                // Calculate frames equivalent to elapsed time
                UINT32 virtualFrames = (UINT32)((ticksElapsed * m_sampleRate) / MediaTimeConstants::TicksPerSecond());
                
                if (virtualFrames > 0)
                {
                    // Generate and write silent audio samples to maintain A/V sync
                    // This prevents video frame backpressure during silence
                    // Check if callback is still valid and we're still running before invoking
                    if (m_audioSampleReadyCallback && m_isRunning)
                    {
                        WAVEFORMATEX* format = m_device.GetFormat();
                        if (format)
                        {
                            UINT32 bufferSize = virtualFrames * format->nBlockAlign;
                            BYTE* pSilentData = GetSilentBuffer(bufferSize);
                            
                            // Calculate timestamp
                            LONGLONG timestamp = 0;
                            if (m_clockReader && m_clockReader->IsRunning())
                            {
                                timestamp = m_clockReader->GetCurrentTime();
                            }
                            else
                            {
                                LONGLONG duration = MediaTimeConstants::TicksFromAudioFrames(virtualFrames, m_sampleRate);
                                timestamp = lastTimestamp + duration;
                            }
                            
                            // Write silent audio to encoder
                            AudioSampleReadyEventArgs args{};
                            args.data = std::span<const uint8_t>(pSilentData, bufferSize);
                            args.timestamp = timestamp;
                            args.pFormat = format;
                            
                            m_audioSampleReadyCallback(args);
                            lastTimestamp = timestamp;
                        }
                    }
                    
                    // Advance the clock
                    m_clockWriter->AdvanceByAudioSamples(virtualFrames, m_sampleRate);
                    lastAdvanceQpc = currentQpc;
                }
            }
            
            // Sleep briefly to avoid busy-waiting
            Sleep(1); // Shorter sleep to check more frequently
        }
        }
    }
    catch (...)
    {
        const HRESULT hr = CaptureKit::Native::HResultFromCurrentException();
        CaptureKit::Native::ReportBoundaryException(L"AudioCaptureHandler::CaptureThreadProc", hr);

        // Audio is optional for video capture. If WASAPI or an audio callback
        // faults after startup, keep advancing the shared media clock from a
        // low-risk timer loop so video timestamps do not freeze permanently.
        constexpr UINT32 FALLBACK_INTERVAL_MS = 10;
        const UINT32 fallbackFrames = (m_sampleRate * FALLBACK_INTERVAL_MS) / 1000;
        while (m_isRunning && m_clockWriter && fallbackFrames > 0)
        {
            Sleep(FALLBACK_INTERVAL_MS);
            if (m_isRunning)
            {
                try
                {
                    m_clockWriter->AdvanceByAudioSamples(fallbackFrames, m_sampleRate);
                }
                catch (...)
                {
                    const HRESULT fallbackHr = CaptureKit::Native::HResultFromCurrentException();
                    CaptureKit::Native::ReportBoundaryException(
                        L"AudioCaptureHandler::CaptureThreadProc clock fallback",
                        fallbackHr);
                    m_isRunning = false;
                }
            }
        }
    }
}
