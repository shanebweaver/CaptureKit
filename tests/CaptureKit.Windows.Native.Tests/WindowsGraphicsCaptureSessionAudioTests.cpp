#include "pch.h"
#include "CppUnitTest.h"
#include "WindowsGraphicsCaptureSession.h"
#include "CaptureSessionConfig.h"
#include "IAudioCaptureSource.h"
#include "IMP4SinkWriter.h"
#include "IVideoCaptureSource.h"
#include "SimpleMediaClock.h"

#include <memory>
#include <span>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CaptureInteropTests
{
    TEST_CLASS(WindowsGraphicsCaptureSessionAudioTests)
    {
    private:
        struct AudioState
        {
            bool initializeSucceeds = true;
            bool startSucceeds = true;
            int initializeCalls = 0;
            int startCalls = 0;
            int stopCalls = 0;
            int setClockWriterCalls = 0;
            int setCallbackCalls = 0;
            uint32_t volumePercentage = 100;
            bool enabled = true;
            bool running = false;
        };

        class TrackingAudioCaptureSource final : public IAudioCaptureSource
        {
        public:
            explicit TrackingAudioCaptureSource(std::shared_ptr<AudioState> state)
                : m_state(std::move(state))
            {
                m_format.wFormatTag = WAVE_FORMAT_PCM;
                m_format.nChannels = 2;
                m_format.nSamplesPerSec = 48000;
                m_format.nAvgBytesPerSec = 48000 * 4;
                m_format.nBlockAlign = 4;
                m_format.wBitsPerSample = 16;
            }

            bool Initialize(HRESULT* outHr = nullptr) override
            {
                m_state->initializeCalls++;
                if (outHr) *outHr = m_state->initializeSucceeds ? S_OK : AUDCLNT_E_DEVICE_INVALIDATED;
                return m_state->initializeSucceeds;
            }

            bool Start(HRESULT* outHr = nullptr) override
            {
                m_state->startCalls++;
                m_state->running = m_state->startSucceeds;
                if (outHr) *outHr = m_state->startSucceeds ? S_OK : AUDCLNT_E_DEVICE_INVALIDATED;
                return m_state->startSucceeds;
            }

            void Stop() override
            {
                m_state->stopCalls++;
                m_state->running = false;
            }

            WAVEFORMATEX* GetFormat() const override
            {
                return const_cast<WAVEFORMATEX*>(&m_format);
            }

            void SetAudioSampleReadyCallback(AudioSampleReadyCallback callback) override
            {
                m_state->setCallbackCalls++;
                m_callback = std::move(callback);
            }

            void SetEnabled(bool enabled) override
            {
                m_state->enabled = enabled;
            }

            bool IsEnabled() const override
            {
                return m_state->enabled;
            }

            void SetVolume(uint32_t volumePercentage) override
            {
                m_state->volumePercentage = volumePercentage;
            }

            bool IsRunning() const override
            {
                return m_state->running;
            }

            bool SetInputDeviceId(const wchar_t*, HRESULT* outHr = nullptr) override
            {
                if (outHr) *outHr = S_OK;
                return true;
            }

            void SetClockWriter(IMediaClockWriter*) override
            {
                m_state->setClockWriterCalls++;
            }

        private:
            std::shared_ptr<AudioState> m_state;
            WAVEFORMATEX m_format{};
            AudioSampleReadyCallback m_callback;
        };

        struct VideoState
        {
            int initializeCalls = 0;
            int startCalls = 0;
            int stopCalls = 0;
            bool running = false;
        };

        class TrackingVideoCaptureSource final : public IVideoCaptureSource
        {
        public:
            explicit TrackingVideoCaptureSource(std::shared_ptr<VideoState> state)
                : m_state(std::move(state))
            {
            }

            bool Initialize(HRESULT* outHr = nullptr) override
            {
                m_state->initializeCalls++;
                if (outHr) *outHr = S_OK;
                return true;
            }

            bool Start(HRESULT* outHr = nullptr) override
            {
                m_state->startCalls++;
                m_state->running = true;
                if (m_callback)
                {
                    VideoFrameReadyEventArgs args{};
                    args.pTexture = reinterpret_cast<ID3D11Texture2D*>(static_cast<uintptr_t>(1));
                    args.timestamp = 0;
                    m_callback(args);
                }
                if (outHr) *outHr = S_OK;
                return true;
            }

            void Stop() override
            {
                m_state->stopCalls++;
                m_state->running = false;
            }

            UINT32 GetWidth() const override { return 1280; }
            UINT32 GetHeight() const override { return 720; }
            ID3D11Device* GetDevice() const override
            {
                return reinterpret_cast<ID3D11Device*>(static_cast<uintptr_t>(1));
            }
            void SetVideoFrameReadyCallback(VideoFrameReadyCallback callback) override
            {
                m_callback = std::move(callback);
            }
            bool IsRunning() const override { return m_state->running; }

        private:
            std::shared_ptr<VideoState> m_state;
            VideoFrameReadyCallback m_callback;
        };

        struct SinkState
        {
            bool audioInitializationSucceeds = true;
            bool beginWritingSucceeds = true;
            int initializeCalls = 0;
            int initializeAudioCalls = 0;
            int beginWritingCalls = 0;
            int finalizeCalls = 0;
            int writeFrameCalls = 0;
            bool videoSinkReady = false;
        };

        class TrackingSinkWriter final : public IMP4SinkWriter
        {
        public:
            explicit TrackingSinkWriter(std::shared_ptr<SinkState> state)
                : m_state(std::move(state))
            {
            }

            bool Initialize(
                const wchar_t*,
                ID3D11Device*,
                uint32_t,
                uint32_t,
                long* outHr = nullptr,
                uint32_t = 0,
                uint32_t = 0) override
            {
                m_state->initializeCalls++;
                m_state->videoSinkReady = true;
                if (outHr) *outHr = S_OK;
                return true;
            }

            bool InitializeAudioStream(WAVEFORMATEX*, long* outHr = nullptr) override
            {
                m_state->initializeAudioCalls++;
                if (!m_state->audioInitializationSucceeds)
                {
                    // Model a partially mutated sink that must not be reused for video-only output.
                    m_state->videoSinkReady = false;
                    if (outHr) *outHr = MF_E_TOPO_CODEC_NOT_FOUND;
                    return false;
                }

                if (outHr) *outHr = S_OK;
                return true;
            }

            bool BeginWriting(long* outHr = nullptr) override
            {
                m_state->beginWritingCalls++;
                if (outHr)
                {
                    *outHr = m_state->beginWritingSucceeds ? S_OK : MF_E_TOPO_CODEC_NOT_FOUND;
                }
                return m_state->beginWritingSucceeds;
            }

            long WriteFrame(ID3D11Texture2D*, int64_t) override
            {
                if (!m_state->videoSinkReady)
                {
                    return E_NOT_VALID_STATE;
                }

                m_state->writeFrameCalls++;
                return S_OK;
            }

            long WriteAudioSample(std::span<const uint8_t>, int64_t) override
            {
                return S_OK;
            }

            void Finalize() override
            {
                m_state->finalizeCalls++;
                m_state->videoSinkReady = false;
            }

        private:
            std::shared_ptr<SinkState> m_state;
        };

        static CaptureSessionConfig CreateConfig(bool desktopAudio, std::wstring inputSourceId = L"")
        {
            CaptureSessionConfig config(
                reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(1)),
                L"C:\\capturekit-optional-audio-test.mp4",
                desktopAudio);
            config.audioInputSourceId = std::move(inputSourceId);
            return config;
        }

        static std::unique_ptr<WindowsGraphicsCaptureSession> CreateSession(
            const CaptureSessionConfig& config,
            const std::shared_ptr<AudioState>& audioState,
            const std::shared_ptr<VideoState>& videoState,
            const std::shared_ptr<SinkState>& sinkState)
        {
            return std::make_unique<WindowsGraphicsCaptureSession>(
                config,
                std::make_unique<SimpleMediaClock>(),
                std::make_unique<TrackingAudioCaptureSource>(audioState),
                std::make_unique<TrackingVideoCaptureSource>(videoState),
                std::make_unique<TrackingSinkWriter>(sinkState));
        }

    public:
        TEST_METHOD(InitiallyMutedAudio_InitializesPipelineAndCanEnableDuringCapture)
        {
            auto audio = std::make_shared<AudioState>();
            auto video = std::make_shared<VideoState>();
            auto sink = std::make_shared<SinkState>();
            auto config = CreateConfig(false);
            config.audioInputVolumePercentage = 37;
            auto session = CreateSession(config, audio, video, sink);

            HRESULT hr = S_OK;
            Assert::IsTrue(session->Initialize(&hr));
            Assert::IsTrue(session->Start(&hr));

            Assert::AreEqual(1, audio->setClockWriterCalls);
            Assert::AreEqual(1, audio->initializeCalls);
            Assert::AreEqual(1, audio->startCalls);
            Assert::IsFalse(audio->enabled, L"Desktop audio must honor its initial muted state");
            Assert::AreEqual(37u, audio->volumePercentage);
            Assert::AreEqual(1, sink->initializeAudioCalls);
            Assert::AreEqual(1, sink->initializeCalls);
            Assert::AreEqual(1, sink->beginWritingCalls);
            Assert::AreEqual(1, sink->writeFrameCalls);

            session->ToggleAudioCapture(true);
            session->SetAudioInputVolume(64);

            Assert::IsTrue(audio->enabled, L"Desktop audio must support false-to-true transitions");
            Assert::AreEqual(64u, audio->volumePercentage, L"Desktop audio volume must remain mutable");
            session->Stop();
        }

        TEST_METHOD(SinkBeginWritingFailure_AbortsBeforeVideoCaptureStarts)
        {
            auto audio = std::make_shared<AudioState>();
            auto video = std::make_shared<VideoState>();
            auto sink = std::make_shared<SinkState>();
            sink->beginWritingSucceeds = false;
            auto session = CreateSession(CreateConfig(false), audio, video, sink);

            HRESULT hr = S_OK;
            Assert::IsTrue(session->Initialize(&hr));
            Assert::IsFalse(session->Start(&hr));

            Assert::AreEqual(MF_E_TOPO_CODEC_NOT_FOUND, hr);
            Assert::AreEqual(1, sink->beginWritingCalls);
            Assert::AreEqual(0, video->startCalls);
            Assert::AreEqual(0, sink->writeFrameCalls);
        }

        TEST_METHOD(MicrophoneOnly_StillInitializesAndStartsRequestedAudio)
        {
            auto audio = std::make_shared<AudioState>();
            auto video = std::make_shared<VideoState>();
            auto sink = std::make_shared<SinkState>();
            auto session = CreateSession(CreateConfig(false, L"microphone-id"), audio, video, sink);

            HRESULT hr = S_OK;
            Assert::IsTrue(session->Initialize(&hr));
            Assert::IsTrue(session->Start(&hr));

            Assert::AreEqual(1, audio->setClockWriterCalls);
            Assert::AreEqual(1, audio->initializeCalls);
            Assert::AreEqual(1, audio->startCalls);
            Assert::IsFalse(audio->enabled, L"System audio should remain muted for a microphone-only request");
            Assert::AreEqual(1, sink->initializeAudioCalls);
            Assert::AreEqual(1, sink->initializeCalls);

            session->Stop();
        }

        TEST_METHOD(AudioInitializationFailure_ContinuesWithVideoOnlySink)
        {
            auto audio = std::make_shared<AudioState>();
            audio->initializeSucceeds = false;
            auto video = std::make_shared<VideoState>();
            auto sink = std::make_shared<SinkState>();
            auto session = CreateSession(CreateConfig(true), audio, video, sink);

            HRESULT hr = S_OK;
            Assert::IsTrue(session->Initialize(&hr));
            Assert::IsTrue(session->Start(&hr));

            Assert::AreEqual(1, audio->initializeCalls);
            Assert::AreEqual(0, audio->startCalls);
            Assert::IsTrue(audio->stopCalls >= 1);
            Assert::AreEqual(0, sink->initializeAudioCalls);
            Assert::AreEqual(1, sink->initializeCalls);
            Assert::AreEqual(1, sink->writeFrameCalls);

            session->Stop();
        }

        TEST_METHOD(AacInitializationFailure_RebuildsUsableVideoOnlySink)
        {
            auto audio = std::make_shared<AudioState>();
            auto video = std::make_shared<VideoState>();
            auto sink = std::make_shared<SinkState>();
            sink->audioInitializationSucceeds = false;
            auto session = CreateSession(CreateConfig(true), audio, video, sink);

            HRESULT hr = S_OK;
            Assert::IsTrue(session->Initialize(&hr));

            Assert::AreEqual(1, sink->initializeAudioCalls);
            Assert::AreEqual(1, sink->finalizeCalls);
            Assert::AreEqual(2, sink->initializeCalls);
            Assert::IsTrue(sink->videoSinkReady);
            Assert::AreEqual(0, audio->startCalls);
            Assert::IsTrue(audio->stopCalls >= 1);

            Assert::IsTrue(session->Start(&hr));
            Assert::AreEqual(0, audio->startCalls);
            Assert::AreEqual(1, video->startCalls);
            Assert::AreEqual(1, sink->writeFrameCalls);

            session->Stop();
        }

        TEST_METHOD(AudioStartFailure_RebuildsUsableVideoOnlySink)
        {
            auto audio = std::make_shared<AudioState>();
            audio->startSucceeds = false;
            auto video = std::make_shared<VideoState>();
            auto sink = std::make_shared<SinkState>();
            auto session = CreateSession(CreateConfig(true), audio, video, sink);

            HRESULT hr = S_OK;
            Assert::IsTrue(session->Initialize(&hr));
            Assert::IsTrue(session->Start(&hr));

            Assert::AreEqual(1, audio->startCalls);
            Assert::IsTrue(audio->stopCalls >= 1);
            Assert::AreEqual(1, sink->initializeAudioCalls);
            Assert::AreEqual(1, sink->finalizeCalls);
            Assert::AreEqual(2, sink->initializeCalls);
            Assert::AreEqual(1, video->startCalls);
            Assert::AreEqual(1, sink->writeFrameCalls);

            session->Stop();
        }
    };
}
