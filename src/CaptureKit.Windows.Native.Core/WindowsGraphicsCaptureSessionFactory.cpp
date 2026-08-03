#include "pch.h"
#include "CompositeAudioCaptureSource.h"
#include "WindowsGraphicsCaptureSessionFactory.h"
#include "WindowsGraphicsCaptureSession.h"
#include <strsafe.h>

WindowsGraphicsCaptureSessionFactory::WindowsGraphicsCaptureSessionFactory(
    std::unique_ptr<IMediaClockFactory> mediaClockFactory,
    std::unique_ptr<IAudioCaptureSourceFactory> audioCaptureSourceFactory,
    std::unique_ptr<IVideoCaptureSourceFactory> videoCaptureSourceFactory,
    std::unique_ptr<IMP4SinkWriterFactory> mp4SinkWriterFactory)
    : m_mediaClockFactory(std::move(mediaClockFactory))
    , m_audioCaptureSourceFactory(std::move(audioCaptureSourceFactory))
    , m_videoCaptureSourceFactory(std::move(videoCaptureSourceFactory))
    , m_mp4SinkWriterFactory(std::move(mp4SinkWriterFactory))
{
}

std::unique_ptr<ICaptureSession> WindowsGraphicsCaptureSessionFactory::CreateSession(
    const CaptureSessionConfig& config,
    HRESULT* outHr)
{
    // Validate configuration first (Guard pattern)
    if (!config.IsValid())
    {
        if (outHr) *outHr = E_INVALIDARG;
        return nullptr;
    }

    // Create the media clock first
    auto mediaClock = m_mediaClockFactory->CreateClock();
    if (!mediaClock)
    {
        if (outHr) *outHr = E_FAIL;
        return nullptr;
    }

    // Create audio capture source with clock reader
    auto audioCaptureSource = std::make_unique<CompositeAudioCaptureSource>(
        mediaClock.get(),
        m_audioCaptureSourceFactory.get(),
        config.audioInputSourceId);
    if (!audioCaptureSource)
    {
        if (outHr) *outHr = E_FAIL;
        return nullptr;
    }

    // Create video capture source with clock reader
    auto videoCaptureSource = m_videoCaptureSourceFactory->CreateVideoCaptureSource(config, mediaClock.get());
    if (!videoCaptureSource)
    {
        if (outHr) *outHr = E_FAIL;
        return nullptr;
    }

    // Create sink writer
    auto sinkWriter = m_mp4SinkWriterFactory->CreateSinkWriter();
    if (!sinkWriter)
    {
        if (outHr) *outHr = E_FAIL;
        return nullptr;
    }

    // Create session with all dependencies - ownership is transferred to the session
    auto session = std::make_unique<WindowsGraphicsCaptureSession>(
        config,
        std::move(mediaClock),
        std::move(audioCaptureSource),
        std::move(videoCaptureSource),
        std::move(sinkWriter));

    // Initialize the session - this sets up all sources and sink writer
    // If initialization fails, return nullptr (fail-fast)
    HRESULT hr = S_OK;
    if (!session->Initialize(&hr))
    {
        wchar_t message[160]{};
        StringCchPrintfW(
            message,
            ARRAYSIZE(message),
            L"[CaptureInterop V1] CreateSession initialization failed. HRESULT=0x%08X\r\n",
            static_cast<unsigned int>(hr));
        OutputDebugStringW(message);
        if (outHr) *outHr = hr;
        return nullptr;
    }

    if (outHr) *outHr = S_OK;
    return session;
}
