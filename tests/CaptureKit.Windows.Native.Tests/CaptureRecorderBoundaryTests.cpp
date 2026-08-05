#include "pch.h"
#include "CppUnitTest.h"
#include "CaptureSessionConfig.h"
#include "ICaptureSessionFactory.h"
#include "NativeExceptionBoundary.h"
#include "ScreenRecorderImpl.h"

#include <memory>
#include <new>
#include <stdexcept>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CaptureInteropTests
{
    namespace
    {
        class FailingCaptureSessionFactory final : public ICaptureSessionFactory
        {
        public:
            explicit FailingCaptureSessionFactory(HRESULT failureHr)
                : m_failureHr(failureHr)
            {
            }

            std::unique_ptr<ICaptureSession> CreateSession(
                const CaptureSessionConfig&,
                HRESULT* outHr) override
            {
                if (outHr) *outHr = m_failureHr;
                return nullptr;
            }

        private:
            HRESULT m_failureHr;
        };

        class ThrowingCaptureSession final : public ICaptureSession
        {
        public:
            ThrowingCaptureSession(
                bool throwOnStart,
                bool throwOnStop,
                HRESULT videoCallbackHr = S_OK,
                HRESULT audioCallbackHr = S_OK)
                : m_throwOnStart(throwOnStart)
                , m_throwOnStop(throwOnStop)
                , m_videoCallbackHr(videoCallbackHr)
                , m_audioCallbackHr(audioCallbackHr)
            {
            }

            bool Start(HRESULT* outHr) override
            {
                if (m_throwOnStart)
                {
                    throw std::runtime_error("start failure");
                }
                if (outHr) *outHr = S_OK;
                m_active = true;
                return true;
            }

            void Stop() override
            {
                m_active = false;
                if (m_throwOnStop)
                {
                    throw std::runtime_error("stop failure");
                }
            }

            void Pause() override {}
            void Resume() override {}
            void ToggleAudioCapture(bool) override {}
            void SetSystemAudioVolume(uint32_t) override {}
            bool SetAudioInputSource(const wchar_t*) override { return true; }
            void SetAudioInputVolume(uint32_t) override {}
            bool IsActive() const override { return m_active; }
            HRESULT SetVideoFrameCallback(VideoFrameCallback) noexcept override
            {
                return m_videoCallbackHr;
            }

            HRESULT SetAudioSampleCallback(AudioSampleCallback) noexcept override
            {
                return m_audioCallbackHr;
            }

        private:
            bool m_throwOnStart;
            bool m_throwOnStop;
            HRESULT m_videoCallbackHr;
            HRESULT m_audioCallbackHr;
            bool m_active = false;
        };

        class ThrowingCaptureSessionFactory final : public ICaptureSessionFactory
        {
        public:
            ThrowingCaptureSessionFactory(
                bool throwOnStart,
                bool throwOnStop,
                HRESULT videoCallbackHr = S_OK,
                HRESULT audioCallbackHr = S_OK)
                : m_throwOnStart(throwOnStart)
                , m_throwOnStop(throwOnStop)
                , m_videoCallbackHr(videoCallbackHr)
                , m_audioCallbackHr(audioCallbackHr)
            {
            }

            std::unique_ptr<ICaptureSession> CreateSession(
                const CaptureSessionConfig&,
                HRESULT* outHr) override
            {
                if (outHr) *outHr = S_OK;
                return std::make_unique<ThrowingCaptureSession>(
                    m_throwOnStart,
                    m_throwOnStop,
                    m_videoCallbackHr,
                    m_audioCallbackHr);
            }

        private:
            bool m_throwOnStart;
            bool m_throwOnStop;
            HRESULT m_videoCallbackHr;
            HRESULT m_audioCallbackHr;
        };

        void __stdcall IgnoreVideoFrame(const VideoFrameData*)
        {
        }

        void __stdcall IgnoreAudioSample(const AudioSampleData*)
        {
        }

        template<typename TAction>
        HRESULT InvokeAndTranslate(TAction&& action)
        {
            try
            {
                action();
                return S_OK;
            }
            catch (...)
            {
                return CaptureKit::Native::HResultFromCurrentException();
            }
        }
    }

    TEST_CLASS(CaptureRecorderBoundaryTests)
    {
    public:
        TEST_METHOD(ScreenRecorder_Start_PreservesFactoryInitializationHResult)
        {
            constexpr HRESULT expected = E_ACCESSDENIED;
            ScreenRecorderImpl recorder(std::make_unique<FailingCaptureSessionFactory>(expected));
            CaptureSessionConfig config(
                reinterpret_cast<HMONITOR>(1),
                L"C:\\capturekit-boundary-test.mp4");
            HRESULT actual = S_OK;

            const bool started = recorder.StartRecording(config, &actual);

            Assert::IsFalse(started);
            Assert::AreEqual(static_cast<long>(expected), static_cast<long>(actual));
        }

        TEST_METHOD(ExceptionBoundary_PreservesWilHResult)
        {
            const HRESULT actual = InvokeAndTranslate([] {
                throw wil::ResultException(E_ACCESSDENIED);
            });

            Assert::AreEqual(static_cast<long>(E_ACCESSDENIED), static_cast<long>(actual));
        }

        TEST_METHOD(ExceptionBoundary_MapsAllocationFailure)
        {
            const HRESULT actual = InvokeAndTranslate([] {
                throw std::bad_alloc();
            });

            Assert::AreEqual(static_cast<long>(E_OUTOFMEMORY), static_cast<long>(actual));
        }

        TEST_METHOD(ExceptionBoundary_ContainsUnknownException)
        {
            const HRESULT actual = InvokeAndTranslate([] {
                throw 42;
            });

            const HRESULT expected = HRESULT_FROM_WIN32(ERROR_UNHANDLED_EXCEPTION);
            Assert::AreEqual(static_cast<long>(expected), static_cast<long>(actual));
        }

        TEST_METHOD(ScreenRecorder_ThrowingStart_DoesNotRetainPartialSession)
        {
            ScreenRecorderImpl recorder(
                std::make_unique<ThrowingCaptureSessionFactory>(true, false));
            CaptureSessionConfig config(
                reinterpret_cast<HMONITOR>(1),
                L"C:\\capturekit-boundary-test.mp4");

            Assert::ExpectException<std::runtime_error>([&] {
                recorder.StartRecording(config);
            });

            Assert::IsFalse(recorder.HasActiveSession());
        }

        TEST_METHOD(ScreenRecorder_ThrowingStop_ClearsSessionBeforePropagating)
        {
            ScreenRecorderImpl recorder(
                std::make_unique<ThrowingCaptureSessionFactory>(false, true));
            CaptureSessionConfig config(
                reinterpret_cast<HMONITOR>(1),
                L"C:\\capturekit-boundary-test.mp4");
            Assert::IsTrue(recorder.StartRecording(config));

            Assert::ExpectException<std::runtime_error>([&] {
                recorder.StopRecording();
            });

            Assert::IsFalse(recorder.HasActiveSession());
        }

        TEST_METHOD(ScreenRecorder_SecondStart_DoesNotReplaceActiveSession)
        {
            ScreenRecorderImpl recorder(
                std::make_unique<ThrowingCaptureSessionFactory>(false, false));
            CaptureSessionConfig config(
                reinterpret_cast<HMONITOR>(1),
                L"C:\\capturekit-boundary-test.mp4");
            Assert::IsTrue(recorder.StartRecording(config));
            HRESULT hr = S_OK;

            Assert::IsFalse(recorder.StartRecording(config, &hr));

            Assert::AreEqual(
                static_cast<long>(E_ILLEGAL_METHOD_CALL),
                static_cast<long>(hr));
            Assert::IsTrue(recorder.HasActiveSession());
            Assert::IsTrue(recorder.StopRecording());
        }

        TEST_METHOD(ScreenRecorder_ActiveVideoCallbackFailure_IsReturned)
        {
            constexpr HRESULT expected = E_ACCESSDENIED;
            ScreenRecorderImpl recorder(
                std::make_unique<ThrowingCaptureSessionFactory>(
                    false,
                    false,
                    expected));
            CaptureSessionConfig config(
                reinterpret_cast<HMONITOR>(1),
                L"C:\\capturekit-boundary-test.mp4");
            Assert::IsTrue(recorder.StartRecording(config));

            const HRESULT actual = recorder.SetVideoFrameCallback(&IgnoreVideoFrame);

            Assert::AreEqual(static_cast<long>(expected), static_cast<long>(actual));
            Assert::IsTrue(recorder.StopRecording());
        }

        TEST_METHOD(ScreenRecorder_ActiveAudioCallbackFailure_IsReturned)
        {
            constexpr HRESULT expected = E_OUTOFMEMORY;
            ScreenRecorderImpl recorder(
                std::make_unique<ThrowingCaptureSessionFactory>(
                    false,
                    false,
                    S_OK,
                    expected));
            CaptureSessionConfig config(
                reinterpret_cast<HMONITOR>(1),
                L"C:\\capturekit-boundary-test.mp4");
            Assert::IsTrue(recorder.StartRecording(config));

            const HRESULT actual = recorder.SetAudioSampleCallback(&IgnoreAudioSample);

            Assert::AreEqual(static_cast<long>(expected), static_cast<long>(actual));
            Assert::IsTrue(recorder.StopRecording());
        }

        TEST_METHOD(ScreenRecorder_StartupVideoCallbackFailure_AbortsSession)
        {
            constexpr HRESULT expected = E_ACCESSDENIED;
            ScreenRecorderImpl recorder(
                std::make_unique<ThrowingCaptureSessionFactory>(
                    false,
                    false,
                    expected));
            CaptureSessionConfig config(
                reinterpret_cast<HMONITOR>(1),
                L"C:\\capturekit-boundary-test.mp4");
            Assert::AreEqual(
                static_cast<long>(S_OK),
                static_cast<long>(recorder.SetVideoFrameCallback(&IgnoreVideoFrame)));
            HRESULT actual = S_OK;

            const bool started = recorder.StartRecording(config, &actual);

            Assert::IsFalse(started);
            Assert::AreEqual(static_cast<long>(expected), static_cast<long>(actual));
            Assert::IsFalse(recorder.HasActiveSession());
        }

        TEST_METHOD(ScreenRecorder_StartupAudioCallbackFailure_AbortsSession)
        {
            constexpr HRESULT expected = E_OUTOFMEMORY;
            ScreenRecorderImpl recorder(
                std::make_unique<ThrowingCaptureSessionFactory>(
                    false,
                    false,
                    S_OK,
                    expected));
            CaptureSessionConfig config(
                reinterpret_cast<HMONITOR>(1),
                L"C:\\capturekit-boundary-test.mp4");
            Assert::AreEqual(
                static_cast<long>(S_OK),
                static_cast<long>(recorder.SetAudioSampleCallback(&IgnoreAudioSample)));
            HRESULT actual = S_OK;

            const bool started = recorder.StartRecording(config, &actual);

            Assert::IsFalse(started);
            Assert::AreEqual(static_cast<long>(expected), static_cast<long>(actual));
            Assert::IsFalse(recorder.HasActiveSession());
        }
    };
}
