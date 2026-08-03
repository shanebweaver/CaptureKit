#include "pch.h"
#include "AudioRecorderImpl.h"
#include "SimpleMediaClockFactory.h"
#include "WindowsLocalAudioCaptureSourceFactory.h"
#include "WindowsWaveSinkWriterFactory.h"
#include "NativeExceptionBoundary.h"

namespace
{
    void StopAudioSessionBestEffort(
        WindowsAudioCaptureSession* session,
        const wchar_t* boundary) noexcept
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

AudioRecorderImpl::AudioRecorderImpl(std::unique_ptr<WindowsAudioCaptureSessionFactory> factory)
    : m_factory(std::move(factory))
{
}

AudioRecorderImpl::AudioRecorderImpl()
    : AudioRecorderImpl(std::make_unique<WindowsAudioCaptureSessionFactory>(
        std::make_unique<SimpleMediaClockFactory>(),
        std::make_unique<WindowsLocalAudioCaptureSourceFactory>(),
        std::make_unique<WindowsWaveSinkWriterFactory>()))
{
}

AudioRecorderImpl::~AudioRecorderImpl()
{
    try
    {
        StopRecording();
    }
    catch (...)
    {
        const HRESULT hr = CaptureKit::Native::HResultFromCurrentException();
        CaptureKit::Native::ReportBoundaryException(L"AudioRecorderImpl::~AudioRecorderImpl", hr);
    }
}

bool AudioRecorderImpl::StartRecording(const AudioRecordingConfig& config, HRESULT* outHr)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_captureSession)
    {
        if (outHr) *outHr = E_NOT_VALID_STATE;
        return false;
    }

    HRESULT createHr = S_OK;
    auto session = m_factory->CreateSession(config, &createHr);
    if (!session)
    {
        if (outHr) *outHr = FAILED(createHr) ? createHr : E_FAIL;
        return false;
    }

    if (m_audioSampleCallback)
    {
        const HRESULT callbackHr = session->SetAudioSampleCallback(m_audioSampleCallback);
        if (FAILED(callbackHr))
        {
            StopAudioSessionBestEffort(
                session.get(),
                L"AudioRecorderImpl::StartRecording callback rollback");
            if (outHr) *outHr = callbackHr;
            return false;
        }
    }

    HRESULT hr = S_OK;
    if (!session->Start(&hr))
    {
        if (outHr) *outHr = hr;
        return false;
    }

    m_captureSession = std::move(session);
    if (outHr) *outHr = S_OK;
    return true;
}

bool AudioRecorderImpl::PauseRecording()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_captureSession)
    {
        return false;
    }

    m_captureSession->Pause();
    return true;
}

bool AudioRecorderImpl::ResumeRecording()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_captureSession)
    {
        return false;
    }

    m_captureSession->Resume();
    return true;
}

bool AudioRecorderImpl::StopRecording()
{
    std::unique_ptr<WindowsAudioCaptureSession> session;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_captureSession)
        {
            return false;
        }

        session = std::move(m_captureSession);
    }

    session->Stop();
    return true;
}

bool AudioRecorderImpl::SetAudioCaptureEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_captureSession)
    {
        return false;
    }

    m_captureSession->ToggleAudioCapture(enabled);
    return true;
}

bool AudioRecorderImpl::SetAudioInputSource(const wchar_t* sourceId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_captureSession)
    {
        return false;
    }

    return m_captureSession->SetAudioInputSource(sourceId);
}

bool AudioRecorderImpl::SetAudioInputVolume(uint32_t volumePercentage)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_captureSession)
    {
        return false;
    }

    m_captureSession->SetAudioInputVolume(volumePercentage);
    return true;
}

HRESULT AudioRecorderImpl::SetAudioSampleCallback(AudioSampleCallback callback) noexcept
{
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_captureSession)
        {
            const HRESULT hr = m_captureSession->SetAudioSampleCallback(callback);
            if (FAILED(hr))
            {
                return hr;
            }
        }

        m_audioSampleCallback = callback;
        return S_OK;
    }
    catch (...)
    {
        const HRESULT hr = CaptureKit::Native::HResultFromCurrentException();
        CaptureKit::Native::ReportBoundaryException(
            L"AudioRecorderImpl::SetAudioSampleCallback",
            hr);
        return hr;
    }
}
