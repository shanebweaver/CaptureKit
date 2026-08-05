#include "ScreenRecorder.h"
#include "RecorderExceptionGuard.h"
#include "ScreenRecorderImpl.h"
#include <Windows.h>
#include <mutex>

namespace
{
    std::mutex g_recorderMutex;

    ScreenRecorderImpl& GetRecorder()
    {
        // Lazy construction keeps allocation/initialization exceptions inside
        // the exported function exception guards below.
        static ScreenRecorderImpl recorder;
        return recorder;
    }
}

static CaptureRecorderResult RecorderResult(CaptureRecorderStatus status, HRESULT hr)
{
    return CaptureRecorderResult{ status, hr };
}

static CaptureRecorderResult Success()
{
    return RecorderResult(CaptureRecorderStatus::Success, S_OK);
}

static CaptureRecorderResult NoActiveSession()
{
    return RecorderResult(CaptureRecorderStatus::NoActiveSession, E_ILLEGAL_METHOD_CALL);
}

extern "C"
{
    __declspec(dllexport) CaptureRecorderResult StartScreenRecording(const CaptureRecordingOptions* options)
    {
        return GuardRecorderCall(
            L"StartScreenRecording",
            CaptureRecorderStatus::StartFailed,
            [&]() -> CaptureRecorderResult {
                std::lock_guard<std::mutex> lock(g_recorderMutex);

                if (!options || !options->outputPath)
                {
                    return RecorderResult(CaptureRecorderStatus::InvalidArgument, E_INVALIDARG);
                }

                CaptureSessionConfig config;
                switch (options->targetKind)
                {
                case CaptureRecordingTargetKind::Monitor:
                    if (!options->hMonitor)
                    {
                        return RecorderResult(CaptureRecorderStatus::InvalidArgument, E_INVALIDARG);
                    }
                    config = CaptureSessionConfig::ForMonitor(
                        options->hMonitor,
                        options->outputPath,
                        options->captureAudio != 0,
                        options->frameRate,
                        options->videoBitrate,
                        options->audioBitrate,
                        options->audioInputSourceId ? options->audioInputSourceId : L"",
                        options->audioInputVolumePercentage);
                    break;

                case CaptureRecordingTargetKind::Window:
                    if (!options->hwnd)
                    {
                        return RecorderResult(CaptureRecorderStatus::InvalidArgument, E_INVALIDARG);
                    }
                    config = CaptureSessionConfig::ForWindow(
                        options->hwnd,
                        options->outputPath,
                        options->captureAudio != 0,
                        options->frameRate,
                        options->videoBitrate,
                        options->audioBitrate,
                        options->audioInputSourceId ? options->audioInputSourceId : L"",
                        options->audioInputVolumePercentage);
                    break;

                case CaptureRecordingTargetKind::Rectangle:
                    if (!options->hMonitor || options->width <= 0 || options->height <= 0)
                    {
                        return RecorderResult(CaptureRecorderStatus::InvalidArgument, E_INVALIDARG);
                    }
                    config = CaptureSessionConfig::ForRectangle(
                        options->hMonitor,
                        options->left,
                        options->top,
                        static_cast<uint32_t>(options->width),
                        static_cast<uint32_t>(options->height),
                        options->outputPath,
                        options->captureAudio != 0,
                        options->frameRate,
                        options->videoBitrate,
                        options->audioBitrate,
                        options->audioInputSourceId ? options->audioInputSourceId : L"",
                        options->audioInputVolumePercentage);
                    break;

                default:
                    return RecorderResult(CaptureRecorderStatus::InvalidArgument, E_INVALIDARG);
                }

                HRESULT hr = S_OK;
                if (!GetRecorder().StartRecording(config, &hr))
                {
                    return RecorderResult(
                        hr == E_INVALIDARG
                            ? CaptureRecorderStatus::InvalidArgument
                            : hr == E_ILLEGAL_METHOD_CALL
                                ? CaptureRecorderStatus::InvalidState
                                : CaptureRecorderStatus::StartFailed,
                        hr);
                }

                return Success();
            });
    }

    __declspec(dllexport) CaptureRecorderResult PauseScreenRecording()
    {
        return GuardRecorderCall(L"PauseScreenRecording", CaptureRecorderStatus::InvalidState, [] {
            std::lock_guard<std::mutex> lock(g_recorderMutex);
            return GetRecorder().PauseRecording() ? Success() : NoActiveSession();
        });
    }

    __declspec(dllexport) CaptureRecorderResult ResumeScreenRecording()
    {
        return GuardRecorderCall(L"ResumeScreenRecording", CaptureRecorderStatus::InvalidState, [] {
            std::lock_guard<std::mutex> lock(g_recorderMutex);
            return GetRecorder().ResumeRecording() ? Success() : NoActiveSession();
        });
    }

    __declspec(dllexport) CaptureRecorderResult StopScreenRecording()
    {
        return GuardRecorderCall(L"StopScreenRecording", CaptureRecorderStatus::InvalidState, [] {
            std::lock_guard<std::mutex> lock(g_recorderMutex);
            return GetRecorder().StopRecording() ? Success() : NoActiveSession();
        });
    }

    __declspec(dllexport) CaptureRecorderResult SetScreenRecordingAudioEnabled(uint32_t enabled)
    {
        return GuardRecorderCall(L"SetScreenRecordingAudioEnabled", CaptureRecorderStatus::InvalidState, [enabled] {
            std::lock_guard<std::mutex> lock(g_recorderMutex);
            return GetRecorder().SetAudioCaptureEnabled(enabled != 0) ? Success() : NoActiveSession();
        });
    }

    __declspec(dllexport) CaptureRecorderResult SetScreenRecordingAudioInputSource(const wchar_t* sourceId)
    {
        return GuardRecorderCall(L"SetScreenRecordingAudioInputSource", CaptureRecorderStatus::InvalidState, [sourceId] {
            std::lock_guard<std::mutex> lock(g_recorderMutex);
            return GetRecorder().SetAudioInputSource(sourceId ? sourceId : L"") ? Success() : NoActiveSession();
        });
    }

    __declspec(dllexport) CaptureRecorderResult SetScreenRecordingAudioInputVolume(uint32_t volumePercentage)
    {
        return GuardRecorderCall(L"SetScreenRecordingAudioInputVolume", CaptureRecorderStatus::InvalidState, [volumePercentage] {
            std::lock_guard<std::mutex> lock(g_recorderMutex);
            return GetRecorder().SetAudioInputVolume(volumePercentage) ? Success() : NoActiveSession();
        });
    }

    __declspec(dllexport) CaptureRecorderResult RegisterVideoFrameCallback(VideoFrameCallback callback)
    {
        return GuardRecorderCall(L"RegisterVideoFrameCallback", CaptureRecorderStatus::InvalidState, [callback] {
            std::lock_guard<std::mutex> lock(g_recorderMutex);
            const HRESULT hr = GetRecorder().SetVideoFrameCallback(callback);
            return SUCCEEDED(hr)
                ? Success()
                : RecorderResult(CaptureRecorderStatus::InvalidState, hr);
        });
    }

    __declspec(dllexport) CaptureRecorderResult RegisterAudioSampleCallback(AudioSampleCallback callback)
    {
        return GuardRecorderCall(L"RegisterAudioSampleCallback", CaptureRecorderStatus::InvalidState, [callback] {
            std::lock_guard<std::mutex> lock(g_recorderMutex);
            const HRESULT hr = GetRecorder().SetAudioSampleCallback(callback);
            return SUCCEEDED(hr)
                ? Success()
                : RecorderResult(CaptureRecorderStatus::InvalidState, hr);
        });
    }
}
