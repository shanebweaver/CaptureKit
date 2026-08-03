#include "pch.h"
#include "CppUnitTest.h"
#include "AudioCaptureHandler.h"
#include "WindowsLocalAudioCaptureSource.h"
#include "SimpleMediaClock.h"

#include <thread>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <future>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

class AudioCaptureHandlerTestAccess final
{
public:
    static void PreparePendingInitialization(AudioCaptureHandler& handler)
    {
        std::lock_guard<std::mutex> lock(handler.m_stateMutex);
        handler.m_initializeCompleted = false;
        handler.m_initializeSucceeded = false;
        handler.m_initializeResult = E_UNEXPECTED;
        handler.m_shutdownRequested = false;
    }

    static bool WaitForInitialization(AudioCaptureHandler& handler, HRESULT* outHr)
    {
        return handler.WaitForInitialization(outHr);
    }

    static bool IsInitializationCompleted(AudioCaptureHandler& handler)
    {
        std::lock_guard<std::mutex> lock(handler.m_stateMutex);
        return handler.m_initializeCompleted;
    }

    static void ForceInitializationCompletion(AudioCaptureHandler& handler)
    {
        {
            std::lock_guard<std::mutex> lock(handler.m_stateMutex);
            handler.m_initializeCompleted = true;
            handler.m_initializeSucceeded = false;
            handler.m_initializeResult = E_ABORT;
        }
        handler.m_stateChanged.notify_all();
    }

    static void PreparePendingThreadPublication(AudioCaptureHandler& handler)
    {
        std::lock_guard<std::mutex> lock(handler.m_stateMutex);
        handler.m_initializeCompleted = false;
        handler.m_initializeSucceeded = false;
        handler.m_initializeResult = E_UNEXPECTED;
        handler.m_shutdownRequested = false;
        handler.m_threadCreationInProgress = true;
    }

    static bool WaitForShutdown(AudioCaptureHandler& handler)
    {
        std::unique_lock<std::mutex> lock(handler.m_stateMutex);
        return handler.m_stateChanged.wait_for(
            lock,
            std::chrono::seconds(1),
            [&handler] { return handler.m_shutdownRequested; });
    }

    static void CompleteThreadPublication(AudioCaptureHandler& handler)
    {
        {
            std::lock_guard<std::mutex> lock(handler.m_stateMutex);
            handler.m_threadCreationInProgress = false;
        }
        handler.m_stateChanged.notify_all();
    }
};

namespace CaptureInteropTests
{
    TEST_CLASS(AudioCaptureSourceTests)
    {
    private:
        class ClockMonitor
        {
        public:
            SimpleMediaClock* clock;
            std::atomic<bool> running{false};
            std::vector<LONGLONG> samples;
            std::thread monitorThread;

            ClockMonitor(SimpleMediaClock* c) : clock(c) {}

            void Start()
            {
                running = true;
                monitorThread = std::thread([this]() {
                    while (running)
                    {
                        if (clock && clock->IsRunning())
                        {
                            samples.push_back(clock->GetCurrentTime());
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                });
            }

            void Stop()
            {
                running = false;
                if (monitorThread.joinable())
                {
                    monitorThread.join();
                }
            }

            ~ClockMonitor()
            {
                Stop();
            }

            bool IsClockAdvancing()
            {
                if (samples.size() < 2) return false;
                
                // Check if clock is advancing monotonically
                for (size_t i = 1; i < samples.size(); i++)
                {
                    if (samples[i] <= samples[i - 1])
                    {
                        return false;
                    }
                }
                return true;
            }

            LONGLONG GetTotalAdvancement()
            {
                if (samples.empty()) return 0;
                return samples.back() - samples.front();
            }
        };

    public:
        TEST_METHOD(AudioHandler_StopCompletesPendingInitializationWait)
        {
            SimpleMediaClock clock;
            AudioCaptureHandler handler(&clock);
            AudioCaptureHandlerTestAccess::PreparePendingInitialization(handler);

            auto initialization = std::async(std::launch::async, [&handler]() {
                HRESULT hr = E_UNEXPECTED;
                const bool initialized =
                    AudioCaptureHandlerTestAccess::WaitForInitialization(handler, &hr);
                return std::pair<bool, HRESULT>{ initialized, hr };
            });

            handler.Stop();
            const auto status = initialization.wait_for(std::chrono::seconds(1));
            if (status != std::future_status::ready)
            {
                // Keep a regression from stranding the test process itself.
                AudioCaptureHandlerTestAccess::ForceInitializationCompletion(handler);
            }

            Assert::IsTrue(
                status == std::future_status::ready,
                L"Stop must release a pending initialization wait");
            const auto [initialized, hr] = initialization.get();
            Assert::IsFalse(initialized);
            Assert::AreEqual(static_cast<long>(E_ABORT), static_cast<long>(hr));
            Assert::IsTrue(
                AudioCaptureHandlerTestAccess::IsInitializationCompleted(handler),
                L"Stop must leave the terminal initialization state latched");
        }

        TEST_METHOD(AudioHandler_StopWaitsForPendingThreadPublication)
        {
            SimpleMediaClock clock;
            AudioCaptureHandler handler(&clock);
            AudioCaptureHandlerTestAccess::PreparePendingThreadPublication(handler);

            auto stop = std::async(std::launch::async, [&handler]() {
                handler.Stop();
            });

            Assert::IsTrue(
                AudioCaptureHandlerTestAccess::WaitForShutdown(handler),
                L"Stop did not publish shutdown");
            Assert::IsTrue(
                stop.wait_for(std::chrono::milliseconds(50)) ==
                    std::future_status::timeout,
                L"Stop returned before thread publication completed");

            AudioCaptureHandlerTestAccess::CompleteThreadPublication(handler);
            Assert::IsTrue(
                stop.wait_for(std::chrono::seconds(1)) ==
                    std::future_status::ready,
                L"Stop did not continue after thread publication completed");
            stop.get();
        }

        TEST_METHOD(AudioSource_Initializes_WithValidLoopback)
        {
            SimpleMediaClock clock;
            WindowsLocalAudioCaptureSource audioSource(&clock);
            
            HRESULT hr;
            bool result = audioSource.Initialize(&hr);
            
            // Note: This might fail if no audio device is available
            if (result)
            {
                Assert::IsTrue(SUCCEEDED(hr));
                Assert::IsNotNull(audioSource.GetFormat());
            }
        }

        TEST_METHOD(AudioSource_CachesSampleRate_AfterInitialization)
        {
            SimpleMediaClock clock;
            WindowsLocalAudioCaptureSource audioSource(&clock);
            
            HRESULT hr;
            if (audioSource.Initialize(&hr))
            {
                WAVEFORMATEX* format = audioSource.GetFormat();
                Assert::IsNotNull(format);
                Assert::IsTrue(format->nSamplesPerSec > 0);
                
                char msg[128];
                sprintf_s(msg, "Detected audio sample rate: %u Hz", format->nSamplesPerSec);
                Logger::WriteMessage(msg);

                // WASAPI returns the default render device mix format, which can vary
                // by machine and user audio settings (for example, 44.1k, 48k, or 96k).
                Assert::IsTrue(format->nSamplesPerSec >= 8000 &&
                             format->nSamplesPerSec <= 384000);
            }
        }

        TEST_METHOD(AudioSource_AdvancesClock_DuringSilence)
        {
            SimpleMediaClock clock;
            WindowsLocalAudioCaptureSource audioSource(&clock);
            
            HRESULT hr;
            if (!audioSource.Initialize(&hr))
            {
                Logger::WriteMessage("Skipping test - no audio device available");
                return;
            }

            // Set up clock
            LARGE_INTEGER qpc;
            QueryPerformanceCounter(&qpc);
            clock.Start(qpc.QuadPart);
            clock.SetClockAdvancer(&audioSource);

            // Start monitor before audio source
            ClockMonitor monitor(&clock);
            monitor.Start();

            // Start audio source
            if (!audioSource.Start(&hr))
            {
                Logger::WriteMessage("Skipping test - failed to start audio capture");
                return;
            }

            // Let it run for 2 seconds
            std::this_thread::sleep_for(std::chrono::seconds(2));

            // Stop audio source
            audioSource.Stop();
            monitor.Stop();

            // Check that clock advanced
            Assert::IsTrue(monitor.IsClockAdvancing(), L"Clock should be advancing even during silence");
            
            LONGLONG advancement = monitor.GetTotalAdvancement();
            char msg[256];
            sprintf_s(msg, "Clock advanced by: %lld ticks (%.2f seconds)", 
                     advancement, advancement / 10000000.0);
            Logger::WriteMessage(msg);
            
            // Log all samples to understand the pattern
            sprintf_s(msg, "Collected %zu samples:", monitor.samples.size());
            Logger::WriteMessage(msg);
            size_t maxSamples = monitor.samples.size() > 10 ? 10 : monitor.samples.size();
            for (size_t i = 0; i < maxSamples; i++)
            {
                sprintf_s(msg, "  Sample %zu: %.3f seconds", i, monitor.samples[i] / 10000000.0);
                Logger::WriteMessage(msg);
            }

            // Should have advanced approximately 2 seconds (allow 0.5s tolerance)
            LONGLONG expectedMin = 15000000LL; // 1.5 seconds
            LONGLONG expectedMax = 25000000LL; // 2.5 seconds
            Assert::IsTrue(advancement >= expectedMin && advancement <= expectedMax,
                          L"Clock should advance approximately 2 seconds");
        }

        TEST_METHOD(AudioSource_AdvancesClock_For5Seconds)
        {
            SimpleMediaClock clock;
            WindowsLocalAudioCaptureSource audioSource(&clock);
            
            HRESULT hr;
            if (!audioSource.Initialize(&hr))
            {
                Logger::WriteMessage("Skipping test - no audio device available");
                return;
            }

            LARGE_INTEGER qpc;
            QueryPerformanceCounter(&qpc);
            clock.Start(qpc.QuadPart);
            clock.SetClockAdvancer(&audioSource);

            if (!audioSource.Start(&hr))
            {
                Logger::WriteMessage("Skipping test - failed to start audio capture");
                return;
            }

            // Sample clock at start
            LONGLONG startTime = clock.GetCurrentTime();
            
            // Run for 5 seconds
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            // Sample clock at end
            LONGLONG endTime = clock.GetCurrentTime();
            
            audioSource.Stop();

            LONGLONG advancement = endTime - startTime;
            char msg[256];
            sprintf_s(msg, "Clock advanced by: %lld ticks (%.2f seconds)", 
                     advancement, advancement / 10000000.0);
            Logger::WriteMessage(msg);

            // Should have advanced approximately 5 seconds (allow 1s tolerance)
            LONGLONG expectedMin = 40000000LL; // 4 seconds
            LONGLONG expectedMax = 60000000LL; // 6 seconds
            Assert::IsTrue(advancement >= expectedMin && advancement <= expectedMax,
                          L"Clock should advance approximately 5 seconds");
        }

        TEST_METHOD(AudioSource_DoesNotEmitSamplesWhileClockIsPaused)
        {
            SimpleMediaClock clock;
            WindowsLocalAudioCaptureSource audioSource(&clock);
            HRESULT hr;
            if (!audioSource.Initialize(&hr))
            {
                Logger::WriteMessage("Skipping test - no audio device available");
                return;
            }

            std::atomic<int> sampleCount{0};
            audioSource.SetAudioSampleReadyCallback(
                [&sampleCount](const AudioSampleReadyEventArgs&) { sampleCount++; });
            LARGE_INTEGER qpc{};
            QueryPerformanceCounter(&qpc);
            clock.Start(qpc.QuadPart);
            clock.SetClockAdvancer(&audioSource);
            if (!audioSource.Start(&hr))
            {
                Logger::WriteMessage("Skipping test - failed to start audio capture");
                return;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            clock.Pause();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            const int pausedCount = sampleCount.load();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const int laterPausedCount = sampleCount.load();

            clock.Resume();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const int resumedCount = sampleCount.load();
            audioSource.Stop();

            Assert::AreEqual(pausedCount, laterPausedCount);
            Assert::IsTrue(resumedCount > laterPausedCount);
        }

        TEST_METHOD(AudioSource_MaintainsConsistentRate)
        {
            SimpleMediaClock clock;
            WindowsLocalAudioCaptureSource audioSource(&clock);
            
            HRESULT hr;
            if (!audioSource.Initialize(&hr) || !audioSource.Start(&hr))
            {
                Logger::WriteMessage("Skipping test - audio not available");
                return;
            }

            LARGE_INTEGER qpc;
            QueryPerformanceCounter(&qpc);
            clock.Start(qpc.QuadPart);
            clock.SetClockAdvancer(&audioSource);

            // Take samples every second for 5 seconds
            std::vector<LONGLONG> samples;
            for (int i = 0; i < 6; i++)
            {
                samples.push_back(clock.GetCurrentTime());
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            audioSource.Stop();

            // Calculate intervals between samples
            std::vector<LONGLONG> intervals;
            for (size_t i = 1; i < samples.size(); i++)
            {
                intervals.push_back(samples[i] - samples[i - 1]);
            }

            // Log the intervals
            for (size_t i = 0; i < intervals.size(); i++)
            {
                char msg[256];
                sprintf_s(msg, "Interval %zu: %.2f seconds", 
                         i, intervals[i] / 10000000.0);
                Logger::WriteMessage(msg);
            }

            // Each interval should be approximately 1 second (allow 0.3s tolerance)
            for (LONGLONG interval : intervals)
            {
                Assert::IsTrue(interval >= 7000000LL && interval <= 13000000LL,
                              L"Each interval should be approximately 1 second");
            }
        }
    };
}
