#include "pch.h"
#include "ScreenRecorderImpl.h"
#include "WindowsGraphicsCaptureSessionFactory.h"
#include "WindowsLocalAudioCaptureSourceFactory.h"
#include "WindowsDesktopVideoCaptureSourceFactory.h"
#include "WindowsMFMP4SinkWriterFactory.h"
#include "SimpleMediaClockFactory.h"
#include "CaptureSessionConfig.h"
#include "ICaptureSessionFactory.h"
#include "NativeExceptionBoundary.h"

#include <strsafe.h>
#include <Windows.h>
#include <memory>
#include <utility>

namespace
{
    void StopSessionBestEffort(ICaptureSession* session, const wchar_t* boundary) noexcept
    {
        if (!session)
        {
            return;
        }

        try
        {
            session->Stop();
        }
        catch (...)
        {
            const HRESULT hr = CaptureKit::Native::HResultFromCurrentException();
            CaptureKit::Native::ReportBoundaryException(boundary, hr);
        }
    }
}

ScreenRecorderImpl::ScreenRecorderImpl(std::unique_ptr<ICaptureSessionFactory> factory)
    : m_factory(std::move(factory))
    , m_captureSession(nullptr)
{
    // Principle #6 (No Globals): Factory is injected, not accessed as a singleton
    // Principle #3 (No Nullable Pointers): Session starts as nullptr, will be created on demand
}

ScreenRecorderImpl::ScreenRecorderImpl()
    : ScreenRecorderImpl(std::make_unique<WindowsGraphicsCaptureSessionFactory>(
        std::make_unique<SimpleMediaClockFactory>(),
        std::make_unique<WindowsLocalAudioCaptureSourceFactory>(),
        std::make_unique<WindowsDesktopVideoCaptureSourceFactory>(),
        std::make_unique<WindowsMFMP4SinkWriterFactory>()))
{
    // Default constructor creates standard factory chain
    // All dependencies are explicitly created and owned (Principle #6: No Globals)
}

ScreenRecorderImpl::~ScreenRecorderImpl()
{
    try
    {
        StopRecording();
    }
    catch (...)
    {
        const HRESULT hr = CaptureKit::Native::HResultFromCurrentException();
        CaptureKit::Native::ReportBoundaryException(L"ScreenRecorderImpl::~ScreenRecorderImpl", hr);
    }
    // Principle #5 (RAII Everything): Destructor ensures cleanup even if caller forgets
}

bool ScreenRecorderImpl::StartRecording(const CaptureSessionConfig& config, HRESULT* outHr)
{
    if (HasActiveSession())
    {
        if (outHr) *outHr = E_ILLEGAL_METHOD_CALL;
        return false;
    }

    if (!config.IsValid())
    {
        if (outHr) *outHr = E_INVALIDARG;
        return false;
    }

    HRESULT createHr = S_OK;
    auto session = m_factory->CreateSession(config, &createHr);
    if (!session)
    {
        if (outHr) *outHr = FAILED(createHr) ? createHr : E_FAIL;
        return false;
    }

    if (m_videoFrameCallback)
    {
        const HRESULT callbackHr = session->SetVideoFrameCallback(m_videoFrameCallback);
        if (FAILED(callbackHr))
        {
            StopSessionBestEffort(
                session.get(),
                L"ScreenRecorderImpl::StartRecording video callback rollback");
            if (outHr) *outHr = callbackHr;
            return false;
        }
    }
    if (m_audioSampleCallback)
    {
        const HRESULT callbackHr = session->SetAudioSampleCallback(m_audioSampleCallback);
        if (FAILED(callbackHr))
        {
            StopSessionBestEffort(
                session.get(),
                L"ScreenRecorderImpl::StartRecording audio callback rollback");
            if (outHr) *outHr = callbackHr;
            return false;
        }
    }

    HRESULT hr = S_OK;
    try
    {
        if (!session->Start(&hr))
        {
            StopSessionBestEffort(session.get(), L"ScreenRecorderImpl::StartRecording rollback");
            if (outHr) *outHr = hr;
            return false;
        }
    }
    catch (...)
    {
        StopSessionBestEffort(session.get(), L"ScreenRecorderImpl::StartRecording exception rollback");
        throw;
    }

    m_captureSession = std::move(session);
    if (outHr) *outHr = S_OK;
    return true;
}

bool ScreenRecorderImpl::StartRecording(HMONITOR hMonitor, const wchar_t* outputPath, bool audioEnabled, HRESULT* outHr)
{
    CaptureSessionConfig config(hMonitor, outputPath, audioEnabled);
    return StartRecording(config, outHr);
}

bool ScreenRecorderImpl::PauseRecording()
{
    if (!HasActiveSession() || !m_captureSession->IsActive())
    {
        return false;
    }

    m_captureSession->Pause();
    return true;
}

bool ScreenRecorderImpl::ResumeRecording()
{
    if (!HasActiveSession() || !m_captureSession->IsActive())
    {
        return false;
    }

    m_captureSession->Resume();
    return true;
}

bool ScreenRecorderImpl::StopRecording()
{
    if (!HasActiveSession())
    {
        return false;
    }

    // Remove the session from global recorder state before finalization. A
    // throwing Stop can then be returned by the C ABI guard without poisoning
    // the next recording attempt.
    auto session = std::move(m_captureSession);
    session->Stop();
    return true;
}

bool ScreenRecorderImpl::SetAudioCaptureEnabled(bool enabled)
{
    if (!HasActiveSession() || !m_captureSession->IsActive())
    {
        return false;
    }

    m_captureSession->ToggleAudioCapture(enabled);
    return true;
}

bool ScreenRecorderImpl::SetSystemAudioVolume(uint32_t volumePercentage)
{
    if (!HasActiveSession() || !m_captureSession->IsActive())
    {
        return false;
    }

    m_captureSession->SetSystemAudioVolume(volumePercentage);
    return true;
}

bool ScreenRecorderImpl::SetAudioInputSource(const wchar_t* sourceId)
{
    if (!HasActiveSession() || !m_captureSession->IsActive())
    {
        return false;
    }

    return m_captureSession->SetAudioInputSource(sourceId ? sourceId : L"");
}

bool ScreenRecorderImpl::SetAudioInputVolume(uint32_t volumePercentage)
{
    if (!HasActiveSession() || !m_captureSession->IsActive())
    {
        return false;
    }

    m_captureSession->SetAudioInputVolume(volumePercentage);
    return true;
}

HRESULT ScreenRecorderImpl::SetVideoFrameCallback(VideoFrameCallback callback) noexcept
{
    if (HasActiveSession())
    {
        const HRESULT hr = m_captureSession->SetVideoFrameCallback(callback);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    // Persist the callback only after the active session accepted the update.
    m_videoFrameCallback = callback;
    return S_OK;
}

HRESULT ScreenRecorderImpl::SetAudioSampleCallback(AudioSampleCallback callback) noexcept
{
    if (HasActiveSession())
    {
        const HRESULT hr = m_captureSession->SetAudioSampleCallback(callback);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    // Persist the callback only after the active session accepted the update.
    m_audioSampleCallback = callback;
    return S_OK;
}
