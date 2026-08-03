#include "pch.h"
#include "CompositeAudioCaptureSource.h"
#include "WindowsAudioCaptureSessionFactory.h"
#include <strsafe.h>

WindowsAudioCaptureSessionFactory::WindowsAudioCaptureSessionFactory(
    std::unique_ptr<IMediaClockFactory> mediaClockFactory,
    std::unique_ptr<IAudioCaptureSourceFactory> audioCaptureSourceFactory,
    std::unique_ptr<IWavSinkWriterFactory> wavSinkWriterFactory)
    : m_mediaClockFactory(std::move(mediaClockFactory))
    , m_audioCaptureSourceFactory(std::move(audioCaptureSourceFactory))
    , m_wavSinkWriterFactory(std::move(wavSinkWriterFactory))
{
}

std::unique_ptr<WindowsAudioCaptureSession> WindowsAudioCaptureSessionFactory::CreateSession(
    const AudioRecordingConfig& config,
    HRESULT* outHr)
{
    if (!config.IsValid())
    {
        if (outHr) *outHr = E_INVALIDARG;
        return nullptr;
    }

    auto mediaClock = m_mediaClockFactory->CreateClock();
    if (!mediaClock)
    {
        if (outHr) *outHr = E_FAIL;
        return nullptr;
    }

    auto audioCaptureSource = std::make_unique<CompositeAudioCaptureSource>(
        mediaClock.get(),
        m_audioCaptureSourceFactory.get(),
        config.audioInputSourceId);
    if (!audioCaptureSource)
    {
        if (outHr) *outHr = E_FAIL;
        return nullptr;
    }

    auto sinkWriter = m_wavSinkWriterFactory->CreateSinkWriter();
    if (!sinkWriter)
    {
        if (outHr) *outHr = E_FAIL;
        return nullptr;
    }

    auto session = std::make_unique<WindowsAudioCaptureSession>(
        config,
        std::move(mediaClock),
        std::move(audioCaptureSource),
        std::move(sinkWriter));

    HRESULT hr = S_OK;
    if (!session->Initialize(&hr))
    {
        wchar_t message[160]{};
        StringCchPrintfW(
            message,
            ARRAYSIZE(message),
            L"[CaptureInterop Audio] CreateSession initialization failed. HRESULT=0x%08X\r\n",
            static_cast<unsigned int>(hr));
        OutputDebugStringW(message);
        if (outHr) *outHr = hr;
        return nullptr;
    }

    if (outHr) *outHr = S_OK;
    return session;
}
