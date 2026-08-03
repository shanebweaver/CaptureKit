#include "AudioRecorder.h"
#include "AudioRecorderImpl.h"
#include "RecorderExceptionGuard.h"
#include <Windows.h>
#include <mutex>

namespace
{
    std::mutex g_audioRecorderMutex;

    AudioRecorderImpl& GetAudioRecorder()
    {
        static AudioRecorderImpl recorder;
        return recorder;
    }
}

static CaptureRecorderResult AudioRecorderResult(CaptureRecorderStatus status, HRESULT hr)
{
    return CaptureRecorderResult{ status, hr };
}

static CaptureRecorderResult AudioSuccess()
{
    return AudioRecorderResult(CaptureRecorderStatus::Success, S_OK);
}

static CaptureRecorderResult AudioNoActiveSession()
{
    return AudioRecorderResult(CaptureRecorderStatus::NoActiveSession, E_ILLEGAL_METHOD_CALL);
}

extern "C"
{
    __declspec(dllexport) CaptureRecorderResult StartAudioRecording(const AudioRecordingOptions* options)
    {
        return GuardRecorderCall(
            L"StartAudioRecording",
            CaptureRecorderStatus::StartFailed,
            [&]() -> CaptureRecorderResult {
                std::lock_guard<std::mutex> lock(g_audioRecorderMutex);
                if (!options || !options->outputPath)
                {
                    return AudioRecorderResult(CaptureRecorderStatus::InvalidArgument, E_INVALIDARG);
                }

                AudioRecordingConfig config(
                    options->outputPath,
                    options->captureAudio != 0,
                    options->audioInputSourceId ? options->audioInputSourceId : L"",
                    options->audioInputVolumePercentage);

                HRESULT hr = S_OK;
                if (!GetAudioRecorder().StartRecording(config, &hr))
                {
                    return AudioRecorderResult(
                        hr == E_INVALIDARG
                            ? CaptureRecorderStatus::InvalidArgument
                            : CaptureRecorderStatus::StartFailed,
                        hr);
                }

                return AudioSuccess();
            });
    }

    __declspec(dllexport) CaptureRecorderResult PauseAudioRecording()
    {
        return GuardRecorderCall(L"PauseAudioRecording", CaptureRecorderStatus::InvalidState, [] {
            std::lock_guard<std::mutex> lock(g_audioRecorderMutex);
            return GetAudioRecorder().PauseRecording() ? AudioSuccess() : AudioNoActiveSession();
        });
    }

    __declspec(dllexport) CaptureRecorderResult ResumeAudioRecording()
    {
        return GuardRecorderCall(L"ResumeAudioRecording", CaptureRecorderStatus::InvalidState, [] {
            std::lock_guard<std::mutex> lock(g_audioRecorderMutex);
            return GetAudioRecorder().ResumeRecording() ? AudioSuccess() : AudioNoActiveSession();
        });
    }

    __declspec(dllexport) CaptureRecorderResult StopAudioRecording()
    {
        return GuardRecorderCall(L"StopAudioRecording", CaptureRecorderStatus::InvalidState, [] {
            std::lock_guard<std::mutex> lock(g_audioRecorderMutex);
            return GetAudioRecorder().StopRecording() ? AudioSuccess() : AudioNoActiveSession();
        });
    }

    __declspec(dllexport) CaptureRecorderResult SetAudioRecordingEnabled(uint32_t enabled)
    {
        return GuardRecorderCall(L"SetAudioRecordingEnabled", CaptureRecorderStatus::InvalidState, [enabled] {
            std::lock_guard<std::mutex> lock(g_audioRecorderMutex);
            return GetAudioRecorder().SetAudioCaptureEnabled(enabled != 0) ? AudioSuccess() : AudioNoActiveSession();
        });
    }

    __declspec(dllexport) CaptureRecorderResult SetAudioRecordingInputSource(const wchar_t* sourceId)
    {
        return GuardRecorderCall(L"SetAudioRecordingInputSource", CaptureRecorderStatus::InvalidState, [sourceId] {
            std::lock_guard<std::mutex> lock(g_audioRecorderMutex);
            return GetAudioRecorder().SetAudioInputSource(sourceId ? sourceId : L"") ? AudioSuccess() : AudioNoActiveSession();
        });
    }

    __declspec(dllexport) CaptureRecorderResult SetAudioRecordingInputVolume(uint32_t volumePercentage)
    {
        return GuardRecorderCall(L"SetAudioRecordingInputVolume", CaptureRecorderStatus::InvalidState, [volumePercentage] {
            std::lock_guard<std::mutex> lock(g_audioRecorderMutex);
            return GetAudioRecorder().SetAudioInputVolume(volumePercentage) ? AudioSuccess() : AudioNoActiveSession();
        });
    }

    __declspec(dllexport) CaptureRecorderResult RegisterAudioRecordingSampleCallback(AudioSampleCallback callback)
    {
        return GuardRecorderCall(L"RegisterAudioRecordingSampleCallback", CaptureRecorderStatus::InvalidState, [callback] {
            std::lock_guard<std::mutex> lock(g_audioRecorderMutex);
            const HRESULT hr = GetAudioRecorder().SetAudioSampleCallback(callback);
            return SUCCEEDED(hr)
                ? AudioSuccess()
                : AudioRecorderResult(CaptureRecorderStatus::InvalidState, hr);
        });
    }
}
