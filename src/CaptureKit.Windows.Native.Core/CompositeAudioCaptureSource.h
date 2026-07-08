#pragma once
#include "IAudioCaptureSource.h"
#include "IAudioCaptureSourceFactory.h"
#include "IMediaClockWriter.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class IMediaClockReader;

class CompositeAudioCaptureSource final : public IAudioCaptureSource
{
public:
    CompositeAudioCaptureSource(
        IMediaClockReader* clockReader,
        IAudioCaptureSourceFactory* sourceFactory,
        std::wstring inputDeviceId);
    ~CompositeAudioCaptureSource() override;

    bool Initialize(HRESULT* outHr = nullptr) override;
    bool Start(HRESULT* outHr = nullptr) override;
    void Stop() override;
    WAVEFORMATEX* GetFormat() const override;
    void SetAudioSampleReadyCallback(AudioSampleReadyCallback callback) override;
    void SetEnabled(bool enabled) override;
    bool IsEnabled() const override;
    void SetVolume(uint32_t volumePercentage) override;
    bool IsRunning() const override;
    bool SetInputDeviceId(const wchar_t* sourceId, HRESULT* outHr = nullptr) override;
    void SetClockWriter(IMediaClockWriter* clockWriter) override;

private:
    class NoOpMediaClockWriter final : public IMediaClockWriter
    {
    public:
        void AdvanceByAudioSamples(UINT32, UINT32) override {}
    };

    void OnLoopbackSample(const AudioSampleReadyEventArgs& args);
    void OnInputSample(const AudioSampleReadyEventArgs& args);
    bool TryCreateInputSource(const std::wstring& sourceId, HRESULT* outHr = nullptr);
    void ClearInputBuffer();
    bool TryMixInput(std::vector<uint8_t>& outputData, WAVEFORMATEX* outputFormat);
    void CompactInputBuffer();
    void TrimInputBuffer();
    float ReadBufferedInputSample(size_t frameIndex, UINT16 outputChannel, UINT16 outputChannelCount) const;

    IMediaClockReader* m_clockReader = nullptr;
    IAudioCaptureSourceFactory* m_sourceFactory = nullptr;
    std::unique_ptr<IAudioCaptureSource> m_loopbackSource;
    std::unique_ptr<IAudioCaptureSource> m_inputSource;
    NoOpMediaClockWriter m_inputClockWriter;
    IMediaClockWriter* m_clockWriter = nullptr;
    std::wstring m_inputDeviceId;
    mutable std::mutex m_sourceMutex;
    mutable std::mutex m_callbackMutex;
    std::mutex m_inputBufferMutex;
    AudioSampleReadyCallback m_callback;
    std::vector<float> m_inputSamples;
    UINT32 m_inputSampleRate = 0;
    UINT16 m_inputChannels = 0;
    size_t m_inputReadFrameOffset = 0;
    double m_inputReadFrameFraction = 0;
    std::vector<uint8_t> m_mixBuffer;
    std::atomic<bool> m_isRunning{ false };
    std::atomic<bool> m_systemAudioEnabled{ true };
    std::atomic<uint32_t> m_inputVolumePercentage{ 100 };
};
