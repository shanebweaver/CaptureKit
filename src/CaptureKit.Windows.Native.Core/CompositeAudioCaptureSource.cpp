#include "pch.h"
#include "CompositeAudioCaptureSource.h"
#include "IMediaClockReader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <strsafe.h>

namespace
{
    constexpr size_t MaxBufferedInputMilliseconds = 500;

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

    bool IsSupportedFormat(const WAVEFORMATEX* format)
    {
        if (!format ||
            format->nSamplesPerSec == 0 ||
            format->nChannels == 0 ||
            format->nBlockAlign == 0)
        {
            return false;
        }

        if (IsFloatFormat(format))
        {
            return format->wBitsPerSample == 32;
        }

        return IsPcmFormat(format) &&
            (format->wBitsPerSample == 8 ||
             format->wBitsPerSample == 16 ||
             format->wBitsPerSample == 24 ||
             format->wBitsPerSample == 32);
    }

    UINT16 BytesPerSample(const WAVEFORMATEX* format)
    {
        if (!format || format->nChannels == 0)
        {
            return 0;
        }

        return static_cast<UINT16>(format->nBlockAlign / format->nChannels);
    }

    size_t FrameCountFromByteCount(size_t byteCount, const WAVEFORMATEX* format)
    {
        if (!format || format->nBlockAlign == 0)
        {
            return 0;
        }

        return byteCount / format->nBlockAlign;
    }

    double ClampUnit(double value)
    {
        if (!std::isfinite(value))
        {
            return 0;
        }

        return std::clamp(value, -1.0, 1.0);
    }

    double ReadPcm24Sample(const uint8_t* sample)
    {
        int32_t value =
            static_cast<int32_t>(sample[0]) |
            (static_cast<int32_t>(sample[1]) << 8) |
            (static_cast<int32_t>(sample[2]) << 16);

        if ((value & 0x800000) != 0)
        {
            value |= ~0xFFFFFF;
        }

        return static_cast<double>(value) / 8388608.0;
    }

    double ReadNormalizedSample(const uint8_t* data, const WAVEFORMATEX* format, size_t frameIndex, UINT16 channel)
    {
        UINT16 bytesPerSample = BytesPerSample(format);
        if (!data || !format || bytesPerSample == 0 || channel >= format->nChannels)
        {
            return 0;
        }

        const uint8_t* sample = data + (frameIndex * format->nBlockAlign) + (channel * bytesPerSample);

        if (IsFloatFormat(format) && format->wBitsPerSample == 32)
        {
            float value = 0;
            std::memcpy(&value, sample, sizeof(value));
            return ClampUnit(value);
        }

        if (!IsPcmFormat(format))
        {
            return 0;
        }

        switch (format->wBitsPerSample)
        {
        case 8:
            return (static_cast<double>(*sample) - 128.0) / 128.0;
        case 16:
        {
            int16_t value = 0;
            std::memcpy(&value, sample, sizeof(value));
            return static_cast<double>(value) / 32768.0;
        }
        case 24:
            return ReadPcm24Sample(sample);
        case 32:
        {
            int32_t value = 0;
            std::memcpy(&value, sample, sizeof(value));
            return static_cast<double>(value) / 2147483648.0;
        }
        default:
            return 0;
        }
    }

    void WritePcm24Sample(uint8_t* sample, double normalized)
    {
        double scaled = normalized < 0
            ? normalized * 8388608.0
            : normalized * 8388607.0;
        auto value = static_cast<int32_t>(std::clamp(
            std::llround(scaled),
            static_cast<long long>(-8388608),
            static_cast<long long>(8388607)));

        sample[0] = static_cast<uint8_t>(value & 0xFF);
        sample[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        sample[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    }

    void WriteNormalizedSample(uint8_t* data, const WAVEFORMATEX* format, size_t frameIndex, UINT16 channel, double normalized)
    {
        UINT16 bytesPerSample = BytesPerSample(format);
        if (!data || !format || bytesPerSample == 0 || channel >= format->nChannels)
        {
            return;
        }

        normalized = ClampUnit(normalized);
        uint8_t* sample = data + (frameIndex * format->nBlockAlign) + (channel * bytesPerSample);

        if (IsFloatFormat(format) && format->wBitsPerSample == 32)
        {
            float value = static_cast<float>(normalized);
            std::memcpy(sample, &value, sizeof(value));
            return;
        }

        if (!IsPcmFormat(format))
        {
            return;
        }

        switch (format->wBitsPerSample)
        {
        case 8:
        {
            long value = std::lround((normalized * 127.0) + 128.0);
            *sample = static_cast<uint8_t>(std::clamp(value, 0L, 255L));
            break;
        }
        case 16:
        {
            double scaled = normalized < 0
                ? normalized * 32768.0
                : normalized * 32767.0;
            auto value = static_cast<int16_t>(std::clamp(
                std::lround(scaled),
                static_cast<long>(std::numeric_limits<int16_t>::min()),
                static_cast<long>(std::numeric_limits<int16_t>::max())));
            std::memcpy(sample, &value, sizeof(value));
            break;
        }
        case 24:
            WritePcm24Sample(sample, normalized);
            break;
        case 32:
        {
            double scaled = normalized < 0
                ? normalized * 2147483648.0
                : normalized * 2147483647.0;
            auto value = static_cast<int32_t>(std::clamp(
                std::llround(scaled),
                static_cast<long long>(std::numeric_limits<int32_t>::min()),
                static_cast<long long>(std::numeric_limits<int32_t>::max())));
            std::memcpy(sample, &value, sizeof(value));
            break;
        }
        }
    }

    void LogInputSourceFailure(const wchar_t* operation, HRESULT hr)
    {
        wchar_t message[224]{};
        StringCchPrintfW(
            message,
            ARRAYSIZE(message),
            L"[CaptureInterop Audio] Input source %ls failed. HRESULT=0x%08X\r\n",
            operation,
            static_cast<unsigned int>(hr));
        OutputDebugStringW(message);
    }
}

CompositeAudioCaptureSource::CompositeAudioCaptureSource(
    IMediaClockReader* clockReader,
    IAudioCaptureSourceFactory* sourceFactory,
    std::wstring inputDeviceId)
    : m_clockReader(clockReader)
    , m_sourceFactory(sourceFactory)
    , m_inputDeviceId(std::move(inputDeviceId))
{
}

CompositeAudioCaptureSource::~CompositeAudioCaptureSource()
{
    Stop();
}

bool CompositeAudioCaptureSource::Initialize(HRESULT* outHr)
{
    if (!m_sourceFactory)
    {
        if (outHr) *outHr = E_POINTER;
        return false;
    }

    m_loopbackSource = m_sourceFactory->CreateAudioCaptureSource(m_clockReader, L"");
    if (!m_loopbackSource)
    {
        if (outHr) *outHr = E_FAIL;
        return false;
    }

    m_loopbackSource->SetClockWriter(m_clockWriter);
    m_loopbackSource->SetAudioSampleReadyCallback([this](const AudioSampleReadyEventArgs& args) {
        OnLoopbackSample(args);
    });

    HRESULT hr = S_OK;
    if (!m_loopbackSource->Initialize(&hr))
    {
        if (outHr) *outHr = hr;
        return false;
    }

    if (!m_inputDeviceId.empty())
    {
        HRESULT inputHr = S_OK;
        if (!TryCreateInputSource(m_inputDeviceId, &inputHr))
        {
            LogInputSourceFailure(L"initialize", inputHr);
        }
    }

    if (outHr) *outHr = S_OK;
    return true;
}

bool CompositeAudioCaptureSource::Start(HRESULT* outHr)
{
    if (m_isRunning.load())
    {
        if (outHr) *outHr = E_NOT_VALID_STATE;
        return false;
    }

    if (!m_loopbackSource)
    {
        if (outHr) *outHr = E_NOT_VALID_STATE;
        return false;
    }

    m_loopbackSource->SetEnabled(m_systemAudioEnabled.load());

    HRESULT hr = S_OK;
    if (!m_loopbackSource->Start(&hr))
    {
        if (outHr) *outHr = hr;
        return false;
    }

    m_isRunning.store(true);

    std::unique_ptr<IAudioCaptureSource> failedInputSource;
    {
        std::lock_guard<std::mutex> lock(m_sourceMutex);
        if (m_inputSource && !m_inputSource->IsRunning())
        {
            m_inputSource->SetVolume(m_inputVolumePercentage.load());
            HRESULT inputHr = S_OK;
            if (!m_inputSource->Start(&inputHr))
            {
                LogInputSourceFailure(L"start", inputHr);
                failedInputSource = std::move(m_inputSource);
            }
        }
    }

    if (failedInputSource)
    {
        failedInputSource->Stop();
        ClearInputBuffer();
    }

    if (outHr) *outHr = S_OK;
    return true;
}

void CompositeAudioCaptureSource::Stop()
{
    m_isRunning.store(false);

    {
        std::lock_guard<std::mutex> lock(m_sourceMutex);
        if (m_inputSource)
        {
            m_inputSource->Stop();
        }
    }

    if (m_loopbackSource)
    {
        m_loopbackSource->Stop();
    }

    ClearInputBuffer();
}

WAVEFORMATEX* CompositeAudioCaptureSource::GetFormat() const
{
    return m_loopbackSource ? m_loopbackSource->GetFormat() : nullptr;
}

void CompositeAudioCaptureSource::SetAudioSampleReadyCallback(AudioSampleReadyCallback callback)
{
    bool hasCallback = static_cast<bool>(callback);
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_callback = std::move(callback);
    }

    if (m_loopbackSource)
    {
        if (hasCallback)
        {
            m_loopbackSource->SetAudioSampleReadyCallback([this](const AudioSampleReadyEventArgs& args) {
                OnLoopbackSample(args);
            });
        }
        else
        {
            m_loopbackSource->SetAudioSampleReadyCallback(nullptr);
        }
    }

    std::lock_guard<std::mutex> lock(m_sourceMutex);
    if (m_inputSource)
    {
        if (hasCallback)
        {
            m_inputSource->SetAudioSampleReadyCallback([this](const AudioSampleReadyEventArgs& args) {
                OnInputSample(args);
            });
        }
        else
        {
            m_inputSource->SetAudioSampleReadyCallback(nullptr);
        }
    }
}

void CompositeAudioCaptureSource::SetEnabled(bool enabled)
{
    m_systemAudioEnabled.store(enabled);
    if (m_loopbackSource)
    {
        m_loopbackSource->SetEnabled(enabled);
    }
}

bool CompositeAudioCaptureSource::IsEnabled() const
{
    return m_systemAudioEnabled.load();
}

void CompositeAudioCaptureSource::SetVolume(uint32_t volumePercentage)
{
    uint32_t clampedVolume = std::min<uint32_t>(volumePercentage, 100);
    m_inputVolumePercentage.store(clampedVolume);

    std::lock_guard<std::mutex> lock(m_sourceMutex);
    if (m_inputSource)
    {
        m_inputSource->SetVolume(clampedVolume);
    }
}

bool CompositeAudioCaptureSource::IsRunning() const
{
    return m_isRunning.load();
}

bool CompositeAudioCaptureSource::SetInputDeviceId(const wchar_t* sourceId, HRESULT* outHr)
{
    m_inputDeviceId = sourceId ? sourceId : L"";
    if (m_inputDeviceId.empty())
    {
        std::unique_ptr<IAudioCaptureSource> oldInputSource;
        {
            std::lock_guard<std::mutex> lock(m_sourceMutex);
            oldInputSource = std::move(m_inputSource);
        }

        if (oldInputSource)
        {
            oldInputSource->Stop();
        }

        ClearInputBuffer();
        if (outHr) *outHr = S_OK;
        return true;
    }

    HRESULT hr = S_OK;
    if (!TryCreateInputSource(m_inputDeviceId, &hr))
    {
        LogInputSourceFailure(L"switch", hr);
        if (outHr) *outHr = S_OK;
        return true;
    }

    if (outHr) *outHr = S_OK;
    return true;
}

void CompositeAudioCaptureSource::SetClockWriter(IMediaClockWriter* clockWriter)
{
    m_clockWriter = clockWriter;

    if (m_loopbackSource)
    {
        m_loopbackSource->SetClockWriter(clockWriter);
    }

    std::lock_guard<std::mutex> lock(m_sourceMutex);
    if (m_inputSource)
    {
        m_inputSource->SetClockWriter(&m_inputClockWriter);
    }
}

void CompositeAudioCaptureSource::OnLoopbackSample(const AudioSampleReadyEventArgs& args)
{
    AudioSampleReadyCallback callback;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        callback = m_callback;
    }

    if (!callback || !args.pFormat || args.data.empty())
    {
        return;
    }

    m_mixBuffer.assign(args.data.begin(), args.data.end());
    TryMixInput(m_mixBuffer, args.pFormat);

    AudioSampleReadyEventArgs mixedArgs{};
    mixedArgs.data = std::span<const uint8_t>(m_mixBuffer.data(), m_mixBuffer.size());
    mixedArgs.timestamp = args.timestamp;
    mixedArgs.pFormat = args.pFormat;
    callback(mixedArgs);
}

void CompositeAudioCaptureSource::OnInputSample(const AudioSampleReadyEventArgs& args)
{
    if (!args.pFormat || args.data.empty() || !IsSupportedFormat(args.pFormat))
    {
        return;
    }

    size_t frameCount = FrameCountFromByteCount(args.data.size(), args.pFormat);
    if (frameCount == 0)
    {
        return;
    }

    UINT16 channelCount = args.pFormat->nChannels;
    std::vector<float> convertedSamples;
    convertedSamples.reserve(frameCount * channelCount);

    const uint8_t* sampleData = args.data.data();
    for (size_t frameIndex = 0; frameIndex < frameCount; frameIndex++)
    {
        for (UINT16 channel = 0; channel < channelCount; channel++)
        {
            convertedSamples.push_back(static_cast<float>(ReadNormalizedSample(sampleData, args.pFormat, frameIndex, channel)));
        }
    }

    std::lock_guard<std::mutex> lock(m_inputBufferMutex);
    if (m_inputSampleRate != args.pFormat->nSamplesPerSec || m_inputChannels != channelCount)
    {
        m_inputSamples.clear();
        m_inputReadFrameOffset = 0;
        m_inputReadFrameFraction = 0;
        m_inputSampleRate = args.pFormat->nSamplesPerSec;
        m_inputChannels = channelCount;
    }

    m_inputSamples.insert(m_inputSamples.end(), convertedSamples.begin(), convertedSamples.end());
    TrimInputBuffer();
}

bool CompositeAudioCaptureSource::TryCreateInputSource(const std::wstring& sourceId, HRESULT* outHr)
{
    if (!m_sourceFactory || sourceId.empty())
    {
        if (outHr) *outHr = E_INVALIDARG;
        return false;
    }

    auto inputSource = m_sourceFactory->CreateAudioCaptureSource(m_clockReader, sourceId);
    if (!inputSource)
    {
        if (outHr) *outHr = E_FAIL;
        return false;
    }

    inputSource->SetClockWriter(&m_inputClockWriter);
    inputSource->SetVolume(m_inputVolumePercentage.load());
    inputSource->SetAudioSampleReadyCallback([this](const AudioSampleReadyEventArgs& args) {
        OnInputSample(args);
    });

    HRESULT hr = S_OK;
    if (!inputSource->Initialize(&hr))
    {
        if (outHr) *outHr = hr;
        return false;
    }

    if (m_isRunning.load() && !inputSource->Start(&hr))
    {
        if (outHr) *outHr = hr;
        return false;
    }

    std::unique_ptr<IAudioCaptureSource> oldInputSource;
    {
        std::lock_guard<std::mutex> lock(m_sourceMutex);
        oldInputSource = std::move(m_inputSource);
        m_inputSource = std::move(inputSource);
    }

    if (oldInputSource)
    {
        oldInputSource->Stop();
    }

    ClearInputBuffer();
    if (outHr) *outHr = S_OK;
    return true;
}

void CompositeAudioCaptureSource::ClearInputBuffer()
{
    std::lock_guard<std::mutex> lock(m_inputBufferMutex);
    m_inputSamples.clear();
    m_inputSampleRate = 0;
    m_inputChannels = 0;
    m_inputReadFrameOffset = 0;
    m_inputReadFrameFraction = 0;
}

bool CompositeAudioCaptureSource::TryMixInput(std::vector<uint8_t>& outputData, WAVEFORMATEX* outputFormat)
{
    if (outputData.empty() || !IsSupportedFormat(outputFormat))
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_inputBufferMutex);
    if (m_inputSamples.empty() || m_inputSampleRate == 0 || m_inputChannels == 0)
    {
        return false;
    }

    size_t outputFrameCount = FrameCountFromByteCount(outputData.size(), outputFormat);
    size_t totalInputFrameCount = m_inputSamples.size() / m_inputChannels;
    if (outputFrameCount == 0 || m_inputReadFrameOffset >= totalInputFrameCount)
    {
        return false;
    }

    double inputFramesPerOutputFrame =
        static_cast<double>(m_inputSampleRate) / static_cast<double>(outputFormat->nSamplesPerSec);
    if (!std::isfinite(inputFramesPerOutputFrame) || inputFramesPerOutputFrame <= 0)
    {
        return false;
    }

    uint8_t* outputBytes = outputData.data();
    double inputPosition = m_inputReadFrameFraction;
    size_t mixedFrameCount = 0;

    for (size_t outputFrameIndex = 0; outputFrameIndex < outputFrameCount; outputFrameIndex++)
    {
        double absoluteInputFrame = static_cast<double>(m_inputReadFrameOffset) + inputPosition;
        size_t inputFrameIndex = static_cast<size_t>(absoluteInputFrame);
        if (inputFrameIndex >= totalInputFrameCount)
        {
            break;
        }

        size_t nextInputFrameIndex = inputFrameIndex + 1;
        double blend = absoluteInputFrame - static_cast<double>(inputFrameIndex);
        if (nextInputFrameIndex >= totalInputFrameCount)
        {
            if (inputFramesPerOutputFrame != 1.0 && outputFrameIndex + 1 < outputFrameCount)
            {
                break;
            }

            nextInputFrameIndex = inputFrameIndex;
            blend = 0;
        }

        for (UINT16 outputChannel = 0; outputChannel < outputFormat->nChannels; outputChannel++)
        {
            double existingSample = ReadNormalizedSample(outputBytes, outputFormat, outputFrameIndex, outputChannel);
            double inputSample = ReadBufferedInputSample(inputFrameIndex, outputChannel, outputFormat->nChannels);
            if (blend > 0)
            {
                double nextInputSample = ReadBufferedInputSample(nextInputFrameIndex, outputChannel, outputFormat->nChannels);
                inputSample += (nextInputSample - inputSample) * blend;
            }

            WriteNormalizedSample(
                outputBytes,
                outputFormat,
                outputFrameIndex,
                outputChannel,
                existingSample + inputSample);
        }

        inputPosition += inputFramesPerOutputFrame;
        mixedFrameCount++;
    }

    if (mixedFrameCount == 0)
    {
        return false;
    }

    size_t wholeInputFramesConsumed = static_cast<size_t>(inputPosition);
    m_inputReadFrameOffset += wholeInputFramesConsumed;
    m_inputReadFrameFraction = inputPosition - static_cast<double>(wholeInputFramesConsumed);
    CompactInputBuffer();
    return true;
}

void CompositeAudioCaptureSource::CompactInputBuffer()
{
    if (m_inputChannels == 0 || m_inputReadFrameOffset == 0)
    {
        return;
    }

    size_t totalFrameCount = m_inputSamples.size() / m_inputChannels;
    if (m_inputReadFrameOffset < 4096 && m_inputReadFrameOffset * 2 < totalFrameCount)
    {
        return;
    }

    size_t samplesToErase = std::min(m_inputReadFrameOffset, totalFrameCount) * m_inputChannels;
    m_inputSamples.erase(m_inputSamples.begin(), m_inputSamples.begin() + samplesToErase);
    m_inputReadFrameOffset = 0;
}

void CompositeAudioCaptureSource::TrimInputBuffer()
{
    if (m_inputSampleRate == 0 || m_inputChannels == 0)
    {
        return;
    }

    CompactInputBuffer();

    size_t totalFrameCount = m_inputSamples.size() / m_inputChannels;
    size_t maxFrameCount = std::max<size_t>(1, (static_cast<size_t>(m_inputSampleRate) * MaxBufferedInputMilliseconds) / 1000);
    if (totalFrameCount <= maxFrameCount)
    {
        return;
    }

    size_t framesToDrop = totalFrameCount - maxFrameCount;
    size_t samplesToErase = framesToDrop * m_inputChannels;
    m_inputSamples.erase(m_inputSamples.begin(), m_inputSamples.begin() + samplesToErase);
    m_inputReadFrameOffset = 0;
    m_inputReadFrameFraction = 0;
}

float CompositeAudioCaptureSource::ReadBufferedInputSample(size_t frameIndex, UINT16 outputChannel, UINT16 outputChannelCount) const
{
    if (m_inputChannels == 0 || frameIndex >= m_inputSamples.size() / m_inputChannels)
    {
        return 0;
    }

    if (outputChannelCount == 1 && m_inputChannels > 1)
    {
        double sum = 0;
        for (UINT16 inputChannel = 0; inputChannel < m_inputChannels; inputChannel++)
        {
            sum += m_inputSamples[(frameIndex * m_inputChannels) + inputChannel];
        }

        return static_cast<float>(sum / m_inputChannels);
    }

    UINT16 inputChannel = m_inputChannels == 1
        ? 0
        : std::min<UINT16>(outputChannel, static_cast<UINT16>(m_inputChannels - 1));

    return m_inputSamples[(frameIndex * m_inputChannels) + inputChannel];
}
